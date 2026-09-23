/*
 * ESP32-C6 RMT (Remote Control) peripheral emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/misc/esp32c6_rmt.h"

#define RMT_DATE_VALUE 0x02107233
#define RMT_TX_DELAY_NS 10000 /* 10 microseconds */

static void esp32c6_rmt_update_irq(ESP32C6RmtState *s)
{
    s->int_st = s->int_raw & s->int_ena;
    qemu_set_irq(s->irq, (s->int_st != 0) ? 1 : 0);
}

static void esp32c6_rmt_timer_cb(void *opaque)
{
    ESP32C6RmtTimerContext *ctx = (ESP32C6RmtTimerContext *)opaque;
    ESP32C6RmtState *s = ctx->s;
    int ch = ctx->ch;

    s->int_raw |= (1 << ch);
    esp32c6_rmt_update_irq(s);
}

static uint64_t esp32c6_rmt_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C6RmtState *s = ESP32C6_RMT(opaque);

    if (addr >= 0x400 && addr < 0x800) {
        return s->ram[(addr - 0x400) / 4];
    }

    switch (addr) {
    case 0x00:
        return s->chndata[0];
    case 0x04:
        return s->chndata[1];
    case 0x08:
        return s->chmdata[0];
    case 0x0c:
        return s->chmdata[1];
    case 0x10:
        /* tx_start_chn (bit 0) is write-only / self-clearing, always reads 0 */
        return s->chnconf0[0] & ~1u;
    case 0x14:
        /* tx_start_chn (bit 0) is write-only / self-clearing, always reads 0 */
        return s->chnconf0[1] & ~1u;
    case 0x18:
        return s->chmconf0[0];
    case 0x1c:
        return s->chmconf1[0];
    case 0x20:
        return s->chmconf0[1];
    case 0x24:
        return s->chmconf1[1];
    case 0x28:
        return s->chnstatus[0];
    case 0x2c:
        return s->chnstatus[1];
    case 0x30:
        return s->chmstatus[0];
    case 0x34:
        return s->chmstatus[1];
    case 0x38:
        return s->int_raw;
    case 0x3c:
        return s->int_raw & s->int_ena;
    case 0x40:
        return s->int_ena;
    case 0x48:
        return s->chncarrier_duty[0];
    case 0x4c:
        return s->chncarrier_duty[1];
    case 0x50:
        return s->chm_rx_carrier_rm[0];
    case 0x54:
        return s->chm_rx_carrier_rm[1];
    case 0x58:
        return s->chn_tx_lim[0];
    case 0x5c:
        return s->chn_tx_lim[1];
    case 0x60:
        return s->chm_rx_lim[0];
    case 0x64:
        return s->chm_rx_lim[1];
    case 0x68:
        return s->sys_conf;
    case 0x6c:
        return s->tx_sim;
    case 0x70:
        return s->ref_cnt_rst;
    case 0xcc:
        return RMT_DATE_VALUE;
    default:
        return 0;
    }
}

static void esp32c6_rmt_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned int size)
{
    ESP32C6RmtState *s = ESP32C6_RMT(opaque);

    if (addr >= 0x400 && addr < 0x800) {
        s->ram[(addr - 0x400) / 4] = value;
        return;
    }

    switch (addr) {
    case 0x00:
        s->chndata[0] = value;
        break;
    case 0x04:
        s->chndata[1] = value;
        break;
    case 0x10: // chnconf0[0]
        s->chnconf0[0] = value & ~1u;
        if (value & 1) { // tx_start_chn
            timer_mod(s->timer[0], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + RMT_TX_DELAY_NS);
        }
        break;
    case 0x14: // chnconf0[1]
        s->chnconf0[1] = value & ~1u;
        if (value & 1) { // tx_start_chn
            timer_mod(s->timer[1], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + RMT_TX_DELAY_NS);
        }
        break;
    case 0x18:
        s->chmconf0[0] = value;
        break;
    case 0x1c:
        s->chmconf1[0] = value;
        break;
    case 0x20:
        s->chmconf0[1] = value;
        break;
    case 0x24:
        s->chmconf1[1] = value;
        break;
    case 0x38: // int_raw
        s->int_raw |= value;
        esp32c6_rmt_update_irq(s);
        break;
    case 0x40: // int_ena
        s->int_ena = value;
        esp32c6_rmt_update_irq(s);
        break;
    case 0x44: // int_clr
        s->int_raw &= ~value;
        esp32c6_rmt_update_irq(s);
        break;
    case 0x48:
        s->chncarrier_duty[0] = value;
        break;
    case 0x4c:
        s->chncarrier_duty[1] = value;
        break;
    case 0x50:
        s->chm_rx_carrier_rm[0] = value;
        break;
    case 0x54:
        s->chm_rx_carrier_rm[1] = value;
        break;
    case 0x58:
        s->chn_tx_lim[0] = value;
        break;
    case 0x5c:
        s->chn_tx_lim[1] = value;
        break;
    case 0x60:
        s->chm_rx_lim[0] = value;
        break;
    case 0x64:
        s->chm_rx_lim[1] = value;
        break;
    case 0x68:
        s->sys_conf = value;
        break;
    case 0x6c: // tx_sim
        s->tx_sim = value;
        if (value & 1) {
            timer_mod(s->timer[0], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + RMT_TX_DELAY_NS);
        }
        if (value & 2) {
            timer_mod(s->timer[1], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + RMT_TX_DELAY_NS);
        }
        break;
    case 0x70:
        s->ref_cnt_rst = value;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps esp32c6_rmt_ops = {
    .read = esp32c6_rmt_read,
    .write = esp32c6_rmt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c6_rmt_reset_hold(Object *obj, ResetType type)
{
    ESP32C6RmtState *s = ESP32C6_RMT(obj);
    for (int i = 0; i < 2; i++) {
        timer_del(s->timer[i]);
    }
    memset(s->chnconf0, 0, sizeof(s->chnconf0));
    memset(s->chmconf0, 0, sizeof(s->chmconf0));
    memset(s->chmconf1, 0, sizeof(s->chmconf1));
    memset(s->chnstatus, 0, sizeof(s->chnstatus));
    memset(s->chmstatus, 0, sizeof(s->chmstatus));
    s->int_raw = 0;
    s->int_st = 0;
    s->int_ena = 0;
    s->sys_conf = 0x00000000;
    s->tx_sim = 0;
    s->ref_cnt_rst = 0;
    qemu_set_irq(s->irq, 0);
}

static void esp32c6_rmt_init(Object *obj)
{
    ESP32C6RmtState *s = ESP32C6_RMT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c6_rmt_ops, s,
                          TYPE_ESP32C6_RMT, ESP32C6_RMT_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    for (int i = 0; i < 2; i++) {
        s->timer_ctx[i].s = s;
        s->timer_ctx[i].ch = i;
        s->timer[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL, esp32c6_rmt_timer_cb, &s->timer_ctx[i]);
    }
}

static void esp32c6_rmt_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = esp32c6_rmt_reset_hold;
}

static const TypeInfo esp32c6_rmt_info = {
    .name          = TYPE_ESP32C6_RMT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32C6RmtState),
    .instance_init = esp32c6_rmt_init,
    .class_init    = esp32c6_rmt_class_init,
};

static void esp32c6_rmt_register_types(void)
{
    type_register_static(&esp32c6_rmt_info);
}

type_init(esp32c6_rmt_register_types)
