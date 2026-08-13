/*
 * ESP32-C5 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/timer/esp32c5_timg.h"

static void esp32c5_timg_class_init(ObjectClass *klass, void *data)
{
    ESPTimgClass* esp = ESP_TIMG_CLASS(klass);
    /* The C5 has no T1, but its INT_ENA/RAW/ST/CLR registers keep the WDT
     * bits at the two-timer position (bit 2; bit 1 is reserved). */
    esp->m_wdt_int_bit2 = true;
    /* MWDT clock mux is in PCR (not modelled); reset default is the C5's
     * 48 MHz XTAL and IDF keeps it (MWDT_CLK_SRC_DEFAULT = XTAL,
     * prescaler 24000 -> 500 us tick). */
    esp->m_wdt_clk_freq = 48000000UL;
}

static const TypeInfo esp32c5_timg_info = {
    .name = TYPE_ESP32C5_TIMG,
    .parent = TYPE_ESP32C3_TIMG,
    .instance_size = sizeof(ESP32C5TimgState),
    .class_size = sizeof(ESP32C5TimgClass),
    .class_init = esp32c5_timg_class_init,
};

static void esp32c5_timg_register_types(void)
{
    type_register_static(&esp32c5_timg_info);
}

type_init(esp32c5_timg_register_types)
