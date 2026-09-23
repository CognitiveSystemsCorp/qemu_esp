/*
 * ESP32-C6 RMT (Remote Control) peripheral emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"
#include "qemu/timer.h"

#define TYPE_ESP32C6_RMT "esp32c6.rmt"
#define ESP32C6_RMT(obj) OBJECT_CHECK(ESP32C6RmtState, (obj), TYPE_ESP32C6_RMT)

#define ESP32C6_RMT_IO_SIZE 0x1000

typedef struct ESP32C6RmtState ESP32C6RmtState;

typedef struct {
    ESP32C6RmtState *s;
    int ch;
} ESP32C6RmtTimerContext;

struct ESP32C6RmtState {
    SysBusDevice parent;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t chndata[2];
    uint32_t chmdata[2];
    uint32_t chnconf0[2];
    uint32_t chmconf0[2];
    uint32_t chmconf1[2];
    uint32_t chnstatus[2];
    uint32_t chmstatus[2];
    uint32_t int_raw;
    uint32_t int_st;
    uint32_t int_ena;
    uint32_t chncarrier_duty[2];
    uint32_t chm_rx_carrier_rm[2];
    uint32_t chn_tx_lim[2];
    uint32_t chm_rx_lim[2];
    uint32_t sys_conf;
    uint32_t tx_sim;
    uint32_t ref_cnt_rst;

    QEMUTimer *timer[2];
    ESP32C6RmtTimerContext timer_ctx[2];

    uint32_t ram[256];
};
