/*
 * ESP32-C5 ECDSA Accelerator emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#ifndef HW_ESP32C5_ECDSA_H
#define HW_ESP32C5_ECDSA_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ESP32C5_ECDSA "esp32c5.ecdsa"
OBJECT_DECLARE_SIMPLE_TYPE(ESP32C5EcdsaState, ESP32C5_ECDSA)

struct ESP32C5EcdsaState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t conf;
    uint32_t clk;
    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t state;
    uint32_t result;

    uint8_t message_mem[64];
    uint8_t r_mem[48];
    uint8_t s_mem[48];
    uint8_t z_mem[48];
    uint8_t qax_mem[48];
    uint8_t qay_mem[48];
};

#endif
