/*
 * ESP32-C6 MODEM_LPCON (Modem Low-Power Controller)
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"

#define TYPE_ESP32C5_MODEM "esp32c5.modem_lpcon"
#define ESP32C5_MODEM(obj) OBJECT_CHECK(ESP32C5ModemState, (obj), TYPE_ESP32C5_MODEM)

#define ESP32C5_MODEM_IO_SIZE 0x100
#define ESP32C5_MODEM_BASE    0x600AF000

#define TYPE_ESP32C5_MODEM_SYSCON "esp32c5.modem_syscon"
#define ESP32C5_MODEM_SYSCON(obj) OBJECT_CHECK(ESP32C5ModemSysconState, (obj), TYPE_ESP32C5_MODEM_SYSCON)

#define ESP32C5_MODEM_SYSCON_IO_SIZE 0x100
#define ESP32C5_MODEM_SYSCON_BASE    0x600A9C00

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
    uint32_t clk_conf;
    uint32_t clk_conf_force_on;
    uint32_t rst_conf;
} ESP32C5ModemState;

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;
} ESP32C5ModemSysconState;
