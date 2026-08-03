/*
 * ESP32-C6 SPI_MEM (SPI0) controller
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"
#include "exec/memory.h"
#include "sysemu/block-backend.h"

#define TYPE_ESP32C5_SPI_MEM "esp32c5.spi_mem"
#define ESP32C5_SPI_MEM(obj) OBJECT_CHECK(ESP32C5SpiMemState, (obj), TYPE_ESP32C5_SPI_MEM)

#define ESP32C5_SPI_MEM_IO_SIZE 0x400
#define ESP32C5_SPI_MEM_BASE    0x60002000

/* ESP32-C5 MMU entry format (from soc/esp32c5/include/soc/ext_mem_defs.h):
 *   bits 0..8   : page number
 *   bit  9      : access type (0=flash, 1=SPIRAM)
 *   bit  10     : valid
 *   bit  11     : sensitive
 * There are 512 entries. */
#define C6_MMU_VALID_BIT       (1 << 10)
#define C6_MMU_PAGE_NUM_MASK   0x1FF
#define C6_MMU_ENTRY_COUNT     512
#define C6_MMU_PAGE_SIZE       (64 * 1024)

typedef struct {
    SysBusDevice parent;
    MemoryRegion iomem;

    uint32_t mmu_item_index;
    uint32_t mmu_entries[C6_MMU_ENTRY_COUNT];

    uint32_t mmu_power_ctrl;
    uint32_t cache_fctrl;

    BlockBackend *flash_blk;
    MemoryRegion *dcache_mr;
} ESP32C5SpiMemState;
