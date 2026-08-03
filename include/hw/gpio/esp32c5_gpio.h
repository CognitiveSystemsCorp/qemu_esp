/*
 * ESP32-C6 GPIO
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_gpio.h"

#define TYPE_ESP32C5_GPIO "esp32c5.gpio"
#define ESP32C5_GPIO(obj)           OBJECT_CHECK(ESP32C5GPIOState, (obj), TYPE_ESP32C5_GPIO)
#define ESP32C5_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C5GPIOClass, obj, TYPE_ESP32C5_GPIO)
#define ESP32C5_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C5GPIOClass, klass, TYPE_ESP32C5_GPIO)

/* ESP32-C5 GPIO_STRAP register is at GPIO base + 0x0 (unlike the C3 which
 * uses offset 0x38).  The ROM boot_mode_check() treats bit 4 (0x10) as the
 * SPI-boot selector: when set, the boot mode string defaults to
 * "SPI_FAST_FLASH_BOOT" and the remaining checks only override it for the
 * various download modes. */
#define ESP32C5_GPIO_STRAP_OFFSET 0x0
#define ESP32C5_STRAP_MODE_FLASH_BOOT 0x10

typedef struct ESP32C5GPIOState {
    ESP32C3GPIOState parent;
} ESP32C5GPIOState;

typedef struct ESP32C5GPIOClass {
    ESP32C3GPIOClass parent_class;
} ESP32C5GPIOClass;
