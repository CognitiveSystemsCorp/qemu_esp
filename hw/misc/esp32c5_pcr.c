/*
 * ESP32-C6 PCR (Peripheral Clock and Reset) controller emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/esp32c5_pcr.h"
#include <stdlib.h>
#include <time.h>

/* ======================================================================
 * ESP32-C6 PCR / SYSCON register layout.
 * From ESP-IDF: components/soc/esp32c5/register/soc/pcr_reg.h
 *               components/soc/esp32c5/register/soc/syscon_reg.h
 * ====================================================================== */

REG32(PCR_UART0_CONF,    0x000)
    FIELD(PCR_UART0_CONF, CLK_EN, 0, 1)
    FIELD(PCR_UART0_CONF, RST_EN, 1, 1)
    FIELD(PCR_UART0_CONF, READY,  2, 1)
REG32(PCR_MSPI_CLK_CONF, 0x01C)
    FIELD(PCR_MSPI_CLK_CONF, FAST_LS_DIV_NUM, 0,  8)
    FIELD(PCR_MSPI_CLK_CONF, FAST_HS_DIV_NUM, 8,  8)
REG32(SYSCON_RND_DATA,   0x0B0)
REG32(PCR_ECC_PD_CTRL,   0x0E0)
REG32(PCR_SYSCLK_CONF,   0x110)
    FIELD(PCR_SYSCLK_CONF, HS_DIV_NUM,    8,  8)
    FIELD(PCR_SYSCLK_CONF, SOC_CLK_SEL,   16, 2)
    FIELD(PCR_SYSCLK_CONF, CLK_XTAL_FREQ, 24, 7)
REG32(PCR_BUS_CLK_UPDATE, 0x144)
    FIELD(PCR_BUS_CLK_UPDATE, BUS_CLOCK_UPDATE, 0, 1)
REG32(SYSCON_ORIGIN,     0x3F8) /* QEMU magic identifier "QEMU" */

/* SOC_CLK_SEL values (matching IDF's SOC_CPU_CLK_SRC_*) */
#define PCR_SYSCLK_SEL_PLL  1

/* PCR_MSPI fast-clock configuration: divider 5 (480 MHz PLL / 6 = 80 MHz) */
#define PCR_MSPI_FAST_HS_DIV_DEFAULT 5
#define PCR_MSPI_FAST_LS_DIV_DEFAULT 0

/* Crystal frequency reported to IDF (40 MHz) */
#define PCR_SYSCLK_CRYSTAL_FREQ_MHZ  40
/* Default high-speed divider value */
#define PCR_SYSCLK_HS_DIV_DEFAULT    2

/* "QEMU" magic word in little-endian, returned at SYSCON_ORIGIN */
#define SYSCON_ORIGIN_QEMU_MAGIC     0x51454d55

static uint64_t esp32c5_pcr_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C5PcrState *s = ESP32C5_PCR(opaque);
    const hwaddr index = addr / sizeof(uint32_t);

    switch (addr) {
    case A_PCR_MSPI_CLK_CONF: {
        uint32_t r = 0;
        r = FIELD_DP32(r, PCR_MSPI_CLK_CONF, FAST_HS_DIV_NUM,
                       PCR_MSPI_FAST_HS_DIV_DEFAULT);
        r = FIELD_DP32(r, PCR_MSPI_CLK_CONF, FAST_LS_DIV_NUM,
                       PCR_MSPI_FAST_LS_DIV_DEFAULT);
        return r;
    }

    case A_SYSCON_RND_DATA: {
        static bool init;
        if (!init) {
            srand(time(NULL));
            init = true;
        }
        return rand();
    }

    case A_PCR_ECC_PD_CTRL:
        /* Bits 0 and 2 are real ECC memory power-down controls, not generic
         * peripheral READY indications.  Preserve software writes exactly. */
        return s->regs[index];

    case A_PCR_SYSCLK_CONF: {
        uint32_t r = 0;
        r = FIELD_DP32(r, PCR_SYSCLK_CONF, CLK_XTAL_FREQ,
                       PCR_SYSCLK_CRYSTAL_FREQ_MHZ);
        r = FIELD_DP32(r, PCR_SYSCLK_CONF, SOC_CLK_SEL, PCR_SYSCLK_SEL_PLL);
        r = FIELD_DP32(r, PCR_SYSCLK_CONF, HS_DIV_NUM,
                       PCR_SYSCLK_HS_DIV_DEFAULT);
        return r;
    }

    case A_SYSCON_ORIGIN:
        return SYSCON_ORIGIN_QEMU_MAGIC;

    default:
        if (index < ARRAY_SIZE(s->regs)) {
            /* Peripheral clock/reset CONF registers: the ROM bootloader
             * asserts RST_EN (bit 1) and then polls the READY bit until the
             * reset completes.  Report READY immediately (bit 2, and bit 3
             * for MSPI which uses bit 3) so those poll loops terminate. */
            return s->regs[index] | BIT(2) | BIT(3);
        }
        return 0;
    }
}

static void esp32c5_pcr_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    ESP32C5PcrState *s = ESP32C5_PCR(opaque);
    const hwaddr index = addr / sizeof(uint32_t);

    if (index < ARRAY_SIZE(s->regs)) {
        s->regs[index] = (uint32_t)value;
    }

    /* PCR_BUS_CLK_UPDATE: the BUS_CLOCK_UPDATE bit is automatically cleared
     * once the clock configuration has been applied.  The bootloader sets it
     * and polls until it reads back 0, so clear it immediately. */
    if (addr == A_PCR_BUS_CLK_UPDATE) {
        s->regs[index] &= ~R_PCR_BUS_CLK_UPDATE_BUS_CLOCK_UPDATE_MASK;
    }
}

static const MemoryRegionOps esp32c5_pcr_ops = {
    .read  = esp32c5_pcr_read,
    .write = esp32c5_pcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c5_pcr_init(Object *obj)
{
    ESP32C5PcrState *s = ESP32C5_PCR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c5_pcr_ops, s,
                          TYPE_ESP32C5_PCR, ESP32C5_PCR_IO_SIZE);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32c5_pcr_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo esp32c5_pcr_info = {
    .name = TYPE_ESP32C5_PCR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C5PcrState),
    .instance_init = esp32c5_pcr_init,
    .class_init = esp32c5_pcr_class_init,
};

static void esp32c5_pcr_register_types(void)
{
    type_register_static(&esp32c5_pcr_info);
}

type_init(esp32c5_pcr_register_types)
