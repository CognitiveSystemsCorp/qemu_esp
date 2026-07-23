/*
 * ESP32-C6 UART
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp32c3_uart.h"

#define TYPE_ESP32C6_UART "esp32c6_soc.uart"
#define ESP32C6_UART(obj)           OBJECT_CHECK(ESP32C6UARTState, (obj), TYPE_ESP32C6_UART)
#define ESP32C6_UART_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C6UARTClass, obj, TYPE_ESP32C6_UART)
#define ESP32C6_UART_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C6UARTClass, klass, TYPE_ESP32C6_UART)

typedef struct ESP32C6UARTState {
    ESP32C3UARTState parent;
} ESP32C6UARTState;

typedef struct ESP32C6UARTClass {
    ESP32C3UARTClass parent_class;
} ESP32C6UARTClass;

/*
 * ESP32-C6 UART registers whose layout differs from ESP32-C3.  In
 * particular, RX timeout configuration moved out of CONF1/MEM_CONF into a
 * dedicated TOUT_CONF register.  The ESP-IDF UART driver relies on the RX
 * timeout interrupt to deliver short interactive inputs.
 */
REG32(ESP32C6_UART_CONF0, 0x20)
    FIELD(ESP32C6_UART_CONF0, AUTOBAUD_EN, 19, 1)
    FIELD(ESP32C6_UART_CONF0, RXFIFO_RST, 22, 1)
    FIELD(ESP32C6_UART_CONF0, TXFIFO_RST, 23, 1)

REG32(ESP32C6_UART_CONF1, 0x24)
    FIELD(ESP32C6_UART_CONF1, RXFIFO_FULL_THRHD, 0, 8)
    FIELD(ESP32C6_UART_CONF1, TXFIFO_EMPTY_THRHD, 8, 8)

REG32(ESP32C6_UART_MEM_CONF, 0x60)

REG32(ESP32C6_UART_TOUT_CONF, 0x64)
    FIELD(ESP32C6_UART_TOUT_CONF, RX_TOUT_EN, 0, 1)
    FIELD(ESP32C6_UART_TOUT_CONF, RX_TOUT_THRHD, 2, 10)

REG32(ESP32C6_UART_MEM_RX_STATUS, 0x6c)
    FIELD(ESP32C6_UART_MEM_RX_STATUS, RD_ADDR, 0, 8)
    FIELD(ESP32C6_UART_MEM_RX_STATUS, WR_ADDR, 9, 8)
