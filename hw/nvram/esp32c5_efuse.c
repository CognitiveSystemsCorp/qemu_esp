/*
 * ESP32-C5 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/nvram/esp32c5_efuse.h"

/* ESP32-C5 chip revision eFuse fields (from
 * soc/esp32c5/register/soc/efuse_reg.h): the revision lives in
 * EFUSE_RD_MAC_SYS2 (offset 0x4c, mapped onto rd_mac_spi_sys_2):
 *   WAFER_VERSION_MINOR : bits [3:0]
 *   WAFER_VERSION_MAJOR : bits [5:4]
 * The bootloader requires >= v1.0. */
#define ESP32C5_EFUSE_WAFER_VERSION_MINOR_MASK 0x0F
#define ESP32C5_EFUSE_WAFER_VERSION_MINOR_S    0
#define ESP32C5_EFUSE_WAFER_VERSION_MAJOR_MASK 0x03
#define ESP32C5_EFUSE_WAFER_VERSION_MAJOR_S    4
#define ESP32C5_CHIP_REV_MAJOR                 1
#define ESP32C5_CHIP_REV_MINOR                 0

static void esp32c5_efuse_realize(DeviceState *dev, Error **errp)
{
    ESP32C5EfuseClass *c5_class = ESP32C5_EFUSE_GET_CLASS(dev);
    ESPEfuseState *s = ESP_EFUSE(dev);

    /* Call the realize function of the parent class, which will initialize
     * the efuse blocks or the efuse mirror (in RAM) */
    c5_class->parent_realize(dev, errp);

    /* If no file was given as efuses, create a temporary one (in RAM). */
    if (s->blk == NULL) {
        assert(s->mirror != NULL);

        /* Set the chip revision to v1.0 in rd_mac_spi_sys_2
         * (EFUSE_RD_MAC_SYS2 at offset 0x4c). */
        uint32_t rev =
            (ESP32C5_CHIP_REV_MAJOR << ESP32C5_EFUSE_WAFER_VERSION_MAJOR_S) |
            (ESP32C5_CHIP_REV_MINOR << ESP32C5_EFUSE_WAFER_VERSION_MINOR_S);
        s->efuses.blocks.rd_mac_spi_sys_2 = rev;

        memcpy(s->mirror, &s->efuses.blocks, sizeof(ESPEfuseBlocks));
    }
}

static void esp32c5_efuse_init(Object *obj)
{
}

static void esp32c5_efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32C5EfuseClass *c5_class = ESP32C5_EFUSE_CLASS(klass);

    device_class_set_parent_realize(dc, esp32c5_efuse_realize,
                                    &c5_class->parent_realize);
}

static const TypeInfo esp32c5_efuse_info = {
    .name = TYPE_ESP32C5_EFUSE,
    .parent = TYPE_ESP32C3_EFUSE,
    .instance_size = sizeof(ESP32C5EfuseState),
    .instance_init = esp32c5_efuse_init,
    .class_init = esp32c5_efuse_class_init,
    .class_size = sizeof(ESP32C5EfuseClass),
};

static void esp32c5_efuse_register_types(void)
{
    type_register_static(&esp32c5_efuse_info);
}

type_init(esp32c5_efuse_register_types)
