#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c6_ecc.h"

#ifdef CONFIG_GCRYPT
#include <gcrypt.h>
#endif

#define ECC_MULT_INT_RAW_REG 0xc
#define ECC_MULT_INT_ST_REG  0x10
#define ECC_MULT_INT_ENA_REG 0x14
#define ECC_MULT_INT_CLR_REG 0x18
#define ECC_MULT_CONF_REG    0x1c
#define ECC_MULT_DATE_REG    0xfc

#define ECC_MULT_K_MEM       0x100
#define ECC_MULT_PX_MEM      0x120
#define ECC_MULT_PY_MEM      0x140

#define ECC_MULT_START_BIT   (1 << 0)
#define ECC_MULT_CALC_DONE_INT (1 << 0)
#define ECC_MULT_KEY_LENGTH_BIT (1 << 2)
#define ECC_MULT_WORK_MODE_SHIFT 5
#define ECC_MULT_WORK_MODE_MASK  (7 << ECC_MULT_WORK_MODE_SHIFT)
#define ECC_MULT_VERIFY_RESULT_BIT (1 << 8)

#define ECC_MODE_POINT_MUL 0
#define ECC_MODE_VERIFY 2
#define ECC_MODE_VERIFY_THEN_POINT_MUL 3

static void esp32c6_ecc_update_irq(ESP32C6EccState *s)
{
    bool level = (s->int_raw & s->int_ena) != 0;
    qemu_set_irq(s->irq, level);
}

#ifdef CONFIG_GCRYPT
static bool esp32c6_ecc_do_math(ESP32C6EccState *s)
{
    gcry_ctx_t ctx = NULL;
    gcry_mpi_t px = NULL, py = NULL, scalar = NULL, pz = NULL;
    gcry_mpi_t qx = NULL, qy = NULL;
    gcry_mpi_point_t p = NULL, q = NULL;
    const char *curve_name = (s->conf & ECC_MULT_KEY_LENGTH_BIT) ?
                             "NIST P-256" : "NIST P-192";
    unsigned mode = (s->conf & ECC_MULT_WORK_MODE_MASK) >>
                    ECC_MULT_WORK_MODE_SHIFT;
    uint8_t k_buf[32], px_buf[32], py_buf[32];
    uint8_t px_out[32] = { 0 }, py_out[32] = { 0 };
    size_t x_len = 0, y_len = 0;
    bool valid;
    bool success = false;

    if (gcry_mpi_ec_new(&ctx, NULL, curve_name)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32c6.ecc: cannot create %s context\n", curve_name);
        return false;
    }

    /* ESP-IDF writes little-endian integers directly to the accelerator RAM;
     * libgcrypt's USG import/export format is big-endian. */
    for (int i = 0; i < 32; i++) {
        k_buf[i] = s->k_mem[31 - i];
        px_buf[i] = s->px_mem[31 - i];
        py_buf[i] = s->py_mem[31 - i];
    }

    if (gcry_mpi_scan(&scalar, GCRYMPI_FMT_USG, k_buf, 32, NULL) ||
        gcry_mpi_scan(&px, GCRYMPI_FMT_USG, px_buf, 32, NULL) ||
        gcry_mpi_scan(&py, GCRYMPI_FMT_USG, py_buf, 32, NULL)) {
        goto out;
    }

    pz = gcry_mpi_set_ui(NULL, 1);
    p = gcry_mpi_point_new(0);
    gcry_mpi_point_set(p, px, py, pz);
    valid = gcry_mpi_ec_curve_point(p, ctx) != 0;

    /* Modes 2 and 3 expose point validity in CONF bit 8.  The ESP-IDF
     * mbedTLS glue explicitly tests this bit and returns an accelerator error
     * when it remains clear. */
    if (mode == ECC_MODE_VERIFY || mode == ECC_MODE_VERIFY_THEN_POINT_MUL) {
        if (valid) {
            s->conf |= ECC_MULT_VERIFY_RESULT_BIT;
        } else {
            s->conf &= ~ECC_MULT_VERIFY_RESULT_BIT;
        }
    }

    if (mode == ECC_MODE_VERIFY) {
        success = true;
        goto out;
    }
    if (mode != ECC_MODE_POINT_MUL &&
        mode != ECC_MODE_VERIFY_THEN_POINT_MUL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32c6.ecc: unsupported work mode %u\n", mode);
        goto out;
    }
    if (mode == ECC_MODE_VERIFY_THEN_POINT_MUL && !valid) {
        success = true;
        goto out;
    }

    q = gcry_mpi_point_new(0);
    gcry_mpi_ec_mul(q, scalar, p, ctx);
    qx = gcry_mpi_new(0);
    qy = gcry_mpi_new(0);
    if (gcry_mpi_ec_get_affine(qx, qy, q, ctx) ||
        gcry_mpi_print(GCRYMPI_FMT_USG, px_buf, 32, &x_len, qx) ||
        gcry_mpi_print(GCRYMPI_FMT_USG, py_buf, 32, &y_len, qy)) {
        goto out;
    }

    if (x_len <= 32) {
        memcpy(px_out + (32 - x_len), px_buf, x_len);
    }
    if (y_len <= 32) {
        memcpy(py_out + (32 - y_len), py_buf, y_len);
    }
    for (int i = 0; i < 32; i++) {
        s->px_mem[31 - i] = px_out[i];
        s->py_mem[31 - i] = py_out[i];
    }
    success = true;

out:
    gcry_mpi_release(px);
    gcry_mpi_release(py);
    gcry_mpi_release(scalar);
    gcry_mpi_release(pz);
    gcry_mpi_release(qx);
    gcry_mpi_release(qy);
    gcry_mpi_point_release(p);
    gcry_mpi_point_release(q);
    gcry_ctx_release(ctx);
    return success;
}
#else
static bool esp32c6_ecc_do_math(ESP32C6EccState *s)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "esp32c6.ecc: QEMU was built without libgcrypt\n");
    return false;
}
#endif

static uint64_t esp32c6_ecc_read(void *opaque, hwaddr addr, unsigned size)
{
    ESP32C6EccState *s = ESP32C6_ECC(opaque);

    if (addr >= ECC_MULT_K_MEM && addr < ECC_MULT_K_MEM + 32) {
        uint32_t val;
        memcpy(&val, &s->k_mem[addr - ECC_MULT_K_MEM], size);
        return val;
    }
    if (addr >= ECC_MULT_PX_MEM && addr < ECC_MULT_PX_MEM + 32) {
        uint32_t val;
        memcpy(&val, &s->px_mem[addr - ECC_MULT_PX_MEM], size);
        return val;
    }
    if (addr >= ECC_MULT_PY_MEM && addr < ECC_MULT_PY_MEM + 32) {
        uint32_t val;
        memcpy(&val, &s->py_mem[addr - ECC_MULT_PY_MEM], size);
        return val;
    }

    switch (addr) {
    case ECC_MULT_INT_RAW_REG:
        return s->int_raw;
    case ECC_MULT_INT_ST_REG:
        return s->int_raw & s->int_ena;
    case ECC_MULT_INT_ENA_REG:
        return s->int_ena;
    case ECC_MULT_CONF_REG:
        return s->conf;
    case ECC_MULT_DATE_REG:
        return 0x20260730;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

static void esp32c6_ecc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    ESP32C6EccState *s = ESP32C6_ECC(opaque);

    if (addr >= ECC_MULT_K_MEM && addr < ECC_MULT_K_MEM + 32) {
        memcpy(&s->k_mem[addr - ECC_MULT_K_MEM], &val, size);
        return;
    }
    if (addr >= ECC_MULT_PX_MEM && addr < ECC_MULT_PX_MEM + 32) {
        memcpy(&s->px_mem[addr - ECC_MULT_PX_MEM], &val, size);
        return;
    }
    if (addr >= ECC_MULT_PY_MEM && addr < ECC_MULT_PY_MEM + 32) {
        memcpy(&s->py_mem[addr - ECC_MULT_PY_MEM], &val, size);
        return;
    }

    switch (addr) {
    case ECC_MULT_INT_ENA_REG:
        s->int_ena = val;
        esp32c6_ecc_update_irq(s);
        break;
    case ECC_MULT_INT_CLR_REG:
        s->int_raw &= ~val;
        esp32c6_ecc_update_irq(s);
        break;
    case ECC_MULT_CONF_REG:
        s->conf = val;
        if (val & ECC_MULT_START_BIT) {
            if (!esp32c6_ecc_do_math(s)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "esp32c6.ecc: ECC operation failed\n");
            }
            s->conf &= ~ECC_MULT_START_BIT;
            s->int_raw |= ECC_MULT_CALC_DONE_INT;
            esp32c6_ecc_update_irq(s);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps esp32c6_ecc_ops = {
    .read = esp32c6_ecc_read,
    .write = esp32c6_ecc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void esp32c6_ecc_realize(DeviceState *dev, Error **errp)
{
    ESP32C6EccState *s = ESP32C6_ECC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &esp32c6_ecc_ops, s,
                          TYPE_ESP32C6_ECC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void esp32c6_ecc_reset(DeviceState *dev)
{
    ESP32C6EccState *s = ESP32C6_ECC(dev);

    s->int_raw = 0;
    s->int_ena = 0;
    s->conf = 0;
    memset(s->k_mem, 0, sizeof(s->k_mem));
    memset(s->px_mem, 0, sizeof(s->px_mem));
    memset(s->py_mem, 0, sizeof(s->py_mem));
}

static void esp32c6_ecc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32c6_ecc_realize;
    device_class_set_legacy_reset(dc, esp32c6_ecc_reset);
}

static const TypeInfo esp32c6_ecc_info = {
    .name          = TYPE_ESP32C6_ECC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6EccState),
    .class_init    = esp32c6_ecc_class_init,
};

static void esp32c6_ecc_register_types(void)
{
    type_register_static(&esp32c6_ecc_info);
}

type_init(esp32c6_ecc_register_types)
