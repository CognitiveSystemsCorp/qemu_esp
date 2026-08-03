/*
 * ESP32-C6 PCR (Peripheral Clock and Reset) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32C5_PCR "esp32c5.pcr"
#define ESP32C5_PCR(obj) OBJECT_CHECK(ESP32C5PcrState, (obj), TYPE_ESP32C5_PCR)

#define ESP32C5_PCR_IO_SIZE 0x1000

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;

    /* Stored register values.  The ROM bootloader resets peripherals via the
     * PCR_*_CONF registers (CLK_EN bit 0, RST_EN bit 1) and then polls the
     * READY bit (bit 2) until the reset completes.  We model the registers as
     * plain storage and always report READY=1. */
    uint32_t regs[ESP32C5_PCR_IO_SIZE / sizeof(uint32_t)];
} ESP32C5PcrState;
