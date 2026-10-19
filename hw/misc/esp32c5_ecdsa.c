/*
 * ESP32-C5 ECDSA Accelerator emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c5_ecdsa.h"

#ifdef CONFIG_GCRYPT
#include <gcrypt.h>
#endif

#define ECDSA_CONF_REG          0x04
#define ECDSA_CLK_REG           0x08
#define ECDSA_INT_RAW_REG       0x0c
#define ECDSA_INT_ST_REG        0x10
#define ECDSA_INT_ENA_REG       0x14
#define ECDSA_INT_CLR_REG       0x18
#define ECDSA_START_REG         0x1c
#define ECDSA_STATE_REG         0x20
#define ECDSA_RESULT_REG        0x24
#define ECDSA_DATE_REG          0xfc

#define ECDSA_MESSAGE_MEM       0x280
#define ECDSA_R_MEM             0x3e0
#define ECDSA_S_MEM             0x410
#define ECDSA_Z_MEM             0x440
#define ECDSA_QAX_MEM           0x470
#define ECDSA_QAY_MEM           0x4a0
#define ECDSA_PARAM_MEM_SIZE    48

#define ECDSA_STATE_IDLE        0
#define ECDSA_STATE_LOAD        1
#define ECDSA_STATE_GET         2
#define ECDSA_STATE_BUSY        3

static void esp32c5_ecdsa_update_irq(ESP32C5EcdsaState *s)
{
    bool level = (s->int_raw & s->int_ena) != 0;
    qemu_set_irq(s->irq, level);
}

#ifdef CONFIG_GCRYPT
static uint32_t esp32c5_ecdsa_verify(ESP32C5EcdsaState *s)
{
    unsigned curve = (s->conf >> 2) & 0x3;
    const char *curve_name;
    size_t key_len;
    switch (curve) {
    case 0:
        curve_name = "NIST P-192";
        key_len = 24;
        break;
    case 1:
        curve_name = "NIST P-256";
        key_len = 32;
        break;
    case 2:
        curve_name = "NIST P-384";
        key_len = 48;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32c5.ecdsa: unsupported curve %u\n", curve);
        return 0;
    }

    uint8_t z_be[48], r_be[48], s_be[48];
    uint8_t q_uncompressed[1 + 2 * 48];

    for (size_t i = 0; i < key_len; i++) {
        z_be[i] = s->z_mem[key_len - 1 - i];
        r_be[i] = s->r_mem[key_len - 1 - i];
        s_be[i] = s->s_mem[key_len - 1 - i];
    }

    q_uncompressed[0] = 0x04;
    for (size_t i = 0; i < key_len; i++) {
        q_uncompressed[1 + i] = s->qax_mem[key_len - 1 - i];
        q_uncompressed[1 + key_len + i] = s->qay_mem[key_len - 1 - i];
    }

    gcry_mpi_t r_mpi = NULL, s_mpi = NULL;
    gcry_sexp_t data_sexp = NULL, sig_sexp = NULL, pub_sexp = NULL;
    int rc = -1;

    if (gcry_mpi_scan(&r_mpi, GCRYMPI_FMT_USG, r_be, key_len, NULL) ||
        gcry_mpi_scan(&s_mpi, GCRYMPI_FMT_USG, s_be, key_len, NULL)) {
        goto out;
    }

    if (gcry_sexp_build(&data_sexp, NULL, "(data(flags raw)(value %b))",
                        (int)key_len, z_be) ||
        gcry_sexp_build(&sig_sexp, NULL, "(sig-val(ecc(r %m)(s %m)))",
                        r_mpi, s_mpi) ||
        gcry_sexp_build(&pub_sexp, NULL, "(public-key(ecc(curve %s)(q %b)))",
                        curve_name, (int)(1 + 2 * key_len), q_uncompressed)) {
        goto out;
    }

    rc = gcry_pk_verify(sig_sexp, data_sexp, pub_sexp);

out:
    gcry_sexp_release(data_sexp);
    gcry_sexp_release(sig_sexp);
    gcry_sexp_release(pub_sexp);
    gcry_mpi_release(r_mpi);
    gcry_mpi_release(s_mpi);

    return (rc == 0) ? 1 : 0;
}
#else
static uint32_t esp32c5_ecdsa_verify(ESP32C5EcdsaState *s)
{
    return 1;
}
#endif

static uint64_t esp32c5_ecdsa_read(void *opaque, hwaddr addr, unsigned size)
{
    ESP32C5EcdsaState *s = ESP32C5_ECDSA(opaque);

    if (addr >= ECDSA_MESSAGE_MEM && addr < ECDSA_MESSAGE_MEM + 64) {
        uint32_t val = 0;
        memcpy(&val, &s->message_mem[addr - ECDSA_MESSAGE_MEM], size);
        return val;
    }
    if (addr >= ECDSA_R_MEM && addr < ECDSA_R_MEM + ECDSA_PARAM_MEM_SIZE) {
        uint32_t val = 0;
        memcpy(&val, &s->r_mem[addr - ECDSA_R_MEM], size);
        return val;
    }
    if (addr >= ECDSA_S_MEM && addr < ECDSA_S_MEM + ECDSA_PARAM_MEM_SIZE) {
        uint32_t val = 0;
        memcpy(&val, &s->s_mem[addr - ECDSA_S_MEM], size);
        return val;
    }
    if (addr >= ECDSA_Z_MEM && addr < ECDSA_Z_MEM + ECDSA_PARAM_MEM_SIZE) {
        uint32_t val = 0;
        memcpy(&val, &s->z_mem[addr - ECDSA_Z_MEM], size);
        return val;
    }
    if (addr >= ECDSA_QAX_MEM && addr < ECDSA_QAX_MEM + ECDSA_PARAM_MEM_SIZE) {
        uint32_t val = 0;
        memcpy(&val, &s->qax_mem[addr - ECDSA_QAX_MEM], size);
        return val;
    }
    if (addr >= ECDSA_QAY_MEM && addr < ECDSA_QAY_MEM + ECDSA_PARAM_MEM_SIZE) {
        uint32_t val = 0;
        memcpy(&val, &s->qay_mem[addr - ECDSA_QAY_MEM], size);
        return val;
    }

    switch (addr) {
    case ECDSA_CONF_REG:
        return s->conf;
    case ECDSA_CLK_REG:
        return s->clk;
    case ECDSA_INT_RAW_REG:
        return s->int_raw;
    case ECDSA_INT_ST_REG:
        return s->int_raw & s->int_ena;
    case ECDSA_INT_ENA_REG:
        return s->int_ena;
    case ECDSA_INT_CLR_REG:
        return 0;
    case ECDSA_STATE_REG:
        return s->state;
    case ECDSA_RESULT_REG:
        return s->result;
    case ECDSA_DATE_REG:
        return 0x02407120;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

static void esp32c5_ecdsa_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    ESP32C5EcdsaState *s = ESP32C5_ECDSA(opaque);

    if (addr >= ECDSA_MESSAGE_MEM && addr < ECDSA_MESSAGE_MEM + 64) {
        memcpy(&s->message_mem[addr - ECDSA_MESSAGE_MEM], &val, size);
        return;
    }
    if (addr >= ECDSA_R_MEM && addr < ECDSA_R_MEM + ECDSA_PARAM_MEM_SIZE) {
        memcpy(&s->r_mem[addr - ECDSA_R_MEM], &val, size);
        return;
    }
    if (addr >= ECDSA_S_MEM && addr < ECDSA_S_MEM + ECDSA_PARAM_MEM_SIZE) {
        memcpy(&s->s_mem[addr - ECDSA_S_MEM], &val, size);
        return;
    }
    if (addr >= ECDSA_Z_MEM && addr < ECDSA_Z_MEM + ECDSA_PARAM_MEM_SIZE) {
        memcpy(&s->z_mem[addr - ECDSA_Z_MEM], &val, size);
        return;
    }
    if (addr >= ECDSA_QAX_MEM && addr < ECDSA_QAX_MEM + ECDSA_PARAM_MEM_SIZE) {
        memcpy(&s->qax_mem[addr - ECDSA_QAX_MEM], &val, size);
        return;
    }
    if (addr >= ECDSA_QAY_MEM && addr < ECDSA_QAY_MEM + ECDSA_PARAM_MEM_SIZE) {
        memcpy(&s->qay_mem[addr - ECDSA_QAY_MEM], &val, size);
        return;
    }

    switch (addr) {
    case ECDSA_CONF_REG:
        s->conf = val;
        break;
    case ECDSA_CLK_REG:
        s->clk = val;
        break;
    case ECDSA_INT_ENA_REG:
        s->int_ena = val;
        esp32c5_ecdsa_update_irq(s);
        break;
    case ECDSA_INT_CLR_REG:
        s->int_raw &= ~val;
        esp32c5_ecdsa_update_irq(s);
        break;
    case ECDSA_START_REG:
        if (val & 1) { /* ECDSA_START */
            s->state = ECDSA_STATE_LOAD;
        } else if (val & 2) { /* ECDSA_LOAD_DONE */
            uint32_t work_mode = s->conf & 0x3;
            if (work_mode == 0) { /* Verify */
                s->result = esp32c5_ecdsa_verify(s);
                s->state = ECDSA_STATE_IDLE;
                s->int_raw |= 1; /* PREP_DONE */
                esp32c5_ecdsa_update_irq(s);
            } else {
                s->state = ECDSA_STATE_GET;
                s->result = 1;
            }
        } else if (val & 4) { /* ECDSA_GET_DONE */
            s->state = ECDSA_STATE_IDLE;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps esp32c5_ecdsa_ops = {
    .read = esp32c5_ecdsa_read,
    .write = esp32c5_ecdsa_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void esp32c5_ecdsa_realize(DeviceState *dev, Error **errp)
{
    ESP32C5EcdsaState *s = ESP32C5_ECDSA(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &esp32c5_ecdsa_ops, s,
                          TYPE_ESP32C5_ECDSA, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void esp32c5_ecdsa_reset(DeviceState *dev)
{
    ESP32C5EcdsaState *s = ESP32C5_ECDSA(dev);

    s->conf = 0;
    s->clk = 0;
    s->int_raw = 0;
    s->int_ena = 0;
    s->state = ECDSA_STATE_IDLE;
    s->result = 0;
    memset(s->message_mem, 0, sizeof(s->message_mem));
    memset(s->r_mem, 0, sizeof(s->r_mem));
    memset(s->s_mem, 0, sizeof(s->s_mem));
    memset(s->z_mem, 0, sizeof(s->z_mem));
    memset(s->qax_mem, 0, sizeof(s->qax_mem));
    memset(s->qay_mem, 0, sizeof(s->qay_mem));
}

static void esp32c5_ecdsa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32c5_ecdsa_realize;
    device_class_set_legacy_reset(dc, esp32c5_ecdsa_reset);
}

static const TypeInfo esp32c5_ecdsa_info = {
    .name          = TYPE_ESP32C5_ECDSA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5EcdsaState),
    .class_init    = esp32c5_ecdsa_class_init,
};

static void esp32c5_ecdsa_register_types(void)
{
    type_register_static(&esp32c5_ecdsa_info);
}

type_init(esp32c5_ecdsa_register_types)
