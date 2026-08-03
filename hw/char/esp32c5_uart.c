/*
 * ESP32-C6 UART emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/char/esp32c5_uart.h"

static uint64_t esp32c5_uart_read(void *opaque, hwaddr addr,
                                  unsigned int size)
{
    ESP32C5UARTState *s = ESP32C5_UART(opaque);
    ESP32C3UARTClass *parent_class = ESP32C3_UART_GET_CLASS(opaque);
    ESP32UARTState *uart = &s->parent.parent;
    uint32_t value = 0;

    switch (addr) {
    case A_ESP32C5_UART_CONF1:
        return s->parent.conf1;

    case A_ESP32C5_UART_MEM_CONF:
    case A_ESP32C5_UART_TOUT_CONF:
        return uart->reg[addr / sizeof(uint32_t)];

    case A_ESP32C5_UART_MEM_RX_STATUS: {
        uint32_t fifo_size = fifo8_num_used(&uart->rx_fifo);

        /* Keep RD_ADDR at zero and expose the current occupancy as WR_ADDR. */
        value = FIELD_DP32(value, ESP32C5_UART_MEM_RX_STATUS, RD_ADDR, 0);
        value = FIELD_DP32(value, ESP32C5_UART_MEM_RX_STATUS, WR_ADDR,
                           fifo_size == UART_FIFO_LENGTH ? 0 : fifo_size);
        return value;
    }

    default:
        /* Bypass the ESP32-C3 address translations for the C6 register map. */
        return parent_class->parent_uart_read(opaque, addr, size);
    }
}

static void esp32c5_uart_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    ESP32C5UARTState *s = ESP32C5_UART(opaque);
    ESP32C3UARTClass *parent_class = ESP32C3_UART_GET_CLASS(opaque);
    ESP32UARTState *uart = &s->parent.parent;

    switch (addr) {
    case A_ESP32C5_UART_CONF0: {
        uint32_t autobaud = FIELD_EX32(value, ESP32C5_UART_CONF0,
                                       AUTOBAUD_EN);

        parent_class->parent_uart_write(opaque, A_UART_AUTOBAUD, autobaud,
                                        sizeof(uint32_t));
        if (FIELD_EX32(value, ESP32C5_UART_CONF0, RXFIFO_RST)) {
            fifo8_reset(&uart->rx_fifo);
        }
        if (FIELD_EX32(value, ESP32C5_UART_CONF0, TXFIFO_RST)) {
            fifo8_reset(&uart->tx_fifo);
        }
        parent_class->parent_uart_write(opaque, addr, value, size);
        break;
    }

    case A_ESP32C5_UART_CONF1:
        s->parent.conf1 = value;
        uart->rx_full_threshold = FIELD_EX32(value, ESP32C5_UART_CONF1,
                                             RXFIFO_FULL_THRHD);
        uart->tx_empty_threshold = FIELD_EX32(value, ESP32C5_UART_CONF1,
                                              TXFIFO_EMPTY_THRHD);
        esp32_uart_update_irq(uart);
        break;

    case A_ESP32C5_UART_MEM_CONF:
        /* MEM_CONF contains only memory power controls on ESP32-C6. */
        parent_class->parent_uart_write(opaque, addr, value, size);
        break;

    case A_ESP32C5_UART_TOUT_CONF:
        uart->reg[addr / sizeof(uint32_t)] = value;
        uart->rx_tout_ena = FIELD_EX32(value, ESP32C5_UART_TOUT_CONF,
                                       RX_TOUT_EN);
        uart->rx_tout_thres = FIELD_EX32(value, ESP32C5_UART_TOUT_CONF,
                                         RX_TOUT_THRHD);
        esp32_uart_set_rx_timeout(uart);
        esp32_uart_update_irq(uart);
        break;

    default:
        /* Bypass the ESP32-C3 address translations for the C6 register map. */
        parent_class->parent_uart_write(opaque, addr, value, size);
        break;
    }
}

static void esp32c5_uart_init(Object *obj)
{
}

static void esp32c5_uart_class_init(ObjectClass *klass, void *data)
{
    ESP32UARTClass *uart_class = ESP32_UART_CLASS(klass);

    uart_class->uart_read = esp32c5_uart_read;
    uart_class->uart_write = esp32c5_uart_write;
}

static const TypeInfo esp32c5_uart_info = {
    .name = TYPE_ESP32C5_UART,
    .parent = TYPE_ESP32C3_UART,
    .instance_size = sizeof(ESP32C5UARTState),
    .instance_init = esp32c5_uart_init,
    .class_init = esp32c5_uart_class_init,
    .class_size = sizeof(ESP32C5UARTClass),
};

static void esp32c5_uart_register_types(void)
{
    type_register_static(&esp32c5_uart_info);
}

type_init(esp32c5_uart_register_types)
