/*
 * ESP32-C6 Timer Group
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/timer/esp32c3_timg.h"

#define TYPE_ESP32C5_TIMG           "timer.esp32c5.timg"
#define ESP32C5_TIMG(obj)           OBJECT_CHECK(ESP32C5TimgState, (obj), TYPE_ESP32C5_TIMG)
#define ESP32C5_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5TimgClass, obj, TYPE_ESP32C5_TIMG)
#define ESP32C5_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5TimgClass, klass, TYPE_ESP32C5_TIMG)

#define ESP32C5_T0_IRQ_INTERRUPT        ESP_T0_IRQ_INTERRUPT
#define ESP32C5_WDT_IRQ_INTERRUPT       ESP_WDT_IRQ_INTERRUPT
#define ESP32C5_WDT_IRQ_RESET           ESP_WDT_IRQ_RESET

typedef struct ESP32C5TimgState {
    ESP32C3TimgState parent;
} ESP32C5TimgState;

typedef struct ESP32C5TimgClass {
    ESP32C3TimgClass parent_class;
} ESP32C5TimgClass;
