/*
 * ESP32-C6 eFuse emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_efuse.h"

#define TYPE_ESP32C5_EFUSE "nvram.esp32c5.efuse"
#define ESP32C5_EFUSE(obj)           OBJECT_CHECK(ESP32C5EfuseState, (obj), TYPE_ESP32C5_EFUSE)
#define ESP32C5_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5EfuseClass, obj, TYPE_ESP32C5_EFUSE)
#define ESP32C5_EFUSE_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5EfuseClass, klass, TYPE_ESP32C5_EFUSE)

typedef struct ESP32C5EfuseState {
    ESP32C3EfuseState parent;
} ESP32C5EfuseState;

typedef struct ESP32C5EfuseClass {
    ESP32C3EfuseClass parent_class;
    DeviceRealize parent_realize;
} ESP32C5EfuseClass;
