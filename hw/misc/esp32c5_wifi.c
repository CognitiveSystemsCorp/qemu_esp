#include "qemu/osdep.h"
#include <math.h>
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32c5_wifi.h"
#include "exec/address-spaces.h"
#include "esp32_wlan_packet.h"
#include "hw/qdev-properties.h"

#define DEBUG 0

#define ESP32C5_WIFI_RX_CTRL_LEN 64
#define ESP32C5_WIFI_RX_CTRL_CSI_OFFSET 56
#define ESP32C5_WIFI_CSI_LEN 128
#define ESP32C5_WIFI_CSI_LEN_LOW_OFFSET 38
#define ESP32C5_WIFI_CSI_LEN_HIGH_OFFSET 39
#define ESP32C5_WIFI_CSI_VALID BIT(2)
#define ESP32C5_WIFI_FFT_GAIN_OFFSET 22
#define ESP32C5_WIFI_AGC_GAIN_OFFSET 23
#define ESP32C5_WIFI_DEFAULT_FFT_GAIN 2
#define ESP32C5_WIFI_DEFAULT_AGC_GAIN 32
#define ESP32C5_WIFI_CSI_STABLE_NS (30LL * NANOSECONDS_PER_SECOND)
#define ESP32C5_WIFI_CSI_MOTION_NS (10LL * NANOSECONDS_PER_SECOND)
#define ESP32C5_WIFI_CSI_NOISE_PEAK 2
#define ESP32C5_WIFI_TX_SRAM_BASE 0x40800000
#define ESP32C5_WIFI_TX_SRAM_MASK 0x7ffff
#define ESP32C5_WIFI_TX_DESC_FRAME_OFFSET 8
#define ESP32C5_WIFI_MAX_FRAME_LEN \
    (IEEE80211_HEADER_SIZE + sizeof(((mac80211_frame *)0)->data_and_fcs))
#define ESP32C5_WIFI_TXQ_LINK_BASE 0xd6c
#define ESP32C5_WIFI_TXQ_STRIDE 0x10
#define ESP32C5_WIFI_TXQ_COUNT 11
#define ESP32C5_WIFI_TXQ_STATE 0xcb8
#define ESP32C5_WIFI_TXQ_STATE_CLEAR 0xcb4
#define ESP32C5_WIFI_TXQ_COMPLETE_BASE 0x14e8
#define ESP32C5_WIFI_TXQ_COMPLETE_STRIDE 0x74
#define ESP32C5_WIFI_RX_DESC_RELOAD 0x80
#define ESP32C5_WIFI_RX_DESC_SIZE_MASK 0x3fff
#define ESP32C5_WIFI_RX_DESC_LENGTH_SHIFT 14
#define ESP32C5_WIFI_RX_DESC_LENGTH_MASK \
    (ESP32C5_WIFI_RX_DESC_SIZE_MASK << ESP32C5_WIFI_RX_DESC_LENGTH_SHIFT)
#define ESP32C5_WIFI_RX_DESC_EOF BIT(30)
#define ESP32C5_WIFI_RX_DESC_OWNER BIT(31)

static int8_t esp32c5_wifi_csi_clamp(double value)
{
    if (value > 127.0) {
        return 127;
    }
    if (value < -127.0) {
        return -127;
    }
    return (int8_t)lrint(value);
}

static int esp32c5_wifi_csi_white_noise(void)
{
    return rand() % (2 * ESP32C5_WIFI_CSI_NOISE_PEAK + 1) -
           ESP32C5_WIFI_CSI_NOISE_PEAK;
}

static void esp32c5_wifi_generate_csi(uint8_t *buf, size_t len,
                                      int64_t now_ns)
{
    const int64_t cycle_ns = ESP32C5_WIFI_CSI_STABLE_NS +
                             ESP32C5_WIFI_CSI_MOTION_NS;
    const int64_t cycle_pos = now_ns % cycle_ns;
    const bool motion = cycle_pos >= ESP32C5_WIFI_CSI_STABLE_NS;
    double phase1 = 0.35;
    double phase2 = -0.90;
    double path1_gain = 26.0;
    double path2_gain = 15.0;
    double direct_gain = 48.0;

    if (motion) {
        double t = (cycle_pos - ESP32C5_WIFI_CSI_STABLE_NS) /
                   (double)NANOSECONDS_PER_SECOND;

        /*
         * Human motion changes reflected-path phase and amplitude quickly in
         * time, but each instantaneous frequency response remains smooth.
         */
        phase1 += 5.5 * sin(2.0 * G_PI * 1.7 * t);
        phase2 += 7.0 * cos(2.0 * G_PI * 2.3 * t);
        path1_gain = 34.0 + 10.0 * sin(2.0 * G_PI * 1.1 * t);
        path2_gain = 24.0 + 8.0 * cos(2.0 * G_PI * 1.9 * t);
        direct_gain = 42.0 + 4.0 * sin(2.0 * G_PI * 0.7 * t);
    }

    /* ESP32 CSI is interleaved imaginary/real data for each subcarrier. */
    for (size_t i = 0; i + 1 < len; i += 2) {
        double subcarrier = (double)(i / 2) - (double)(len / 4);
        double path1_phase = phase1 + subcarrier * 0.055;
        double path2_phase = phase2 - subcarrier * 0.130;
        double real = direct_gain + path1_gain * cos(path1_phase) +
                      path2_gain * cos(path2_phase) +
                      esp32c5_wifi_csi_white_noise();
        double imag = path1_gain * sin(path1_phase) +
                      path2_gain * sin(path2_phase) +
                      esp32c5_wifi_csi_white_noise();

        buf[i] = (uint8_t)esp32c5_wifi_csi_clamp(imag);
        buf[i + 1] = (uint8_t)esp32c5_wifi_csi_clamp(real);
    }
}

static uint64_t esp32C3_wifi_read(void *opaque, hwaddr addr, unsigned int size)
{
    
    Esp32WifiState *s = ESP32C5_WIFI(opaque);
    uint32_t r = s->mem[addr/4];
    
    switch(addr) {
        case 0xc34:
        case 0xc48:
            r = s->raw_interrupt;
            break;
        case 0xddc:
            /* C6 MAC core reset/initialization complete. */
            r |= BIT(0);
            break;
        case A_C3_WIFI_DMA_IN_STATUS:
            r=0;
            break;
        case A_C3_WIFI_DMA_INT_STATUS:
        case A_C3_WIFI_DMA_INT_CLR:
            r=s->raw_interrupt;
            break;
        case A_C3_WIFI_STATUS:
        case A_C3_WIFI_DMA_OUT_STATUS:
            r=1;
            break;           
    }

    if(DEBUG) printf("esp32C3_wifi_read  0x%04lx= 0x%08x\n",(unsigned long) addr,r);

    return r;
}
static void set_interrupt(Esp32WifiState *s,int e) {
    s->raw_interrupt |= e;
    qemu_set_irq(s->irq, 1);
}

static void esp32c5_wifi_tx_complete(Esp32WifiState *s, unsigned qid)
{
    hwaddr complete_addr = ESP32C5_WIFI_TXQ_COMPLETE_BASE -
                           qid * ESP32C5_WIFI_TXQ_COMPLETE_STRIDE;
    uint32_t complete = s->mem[complete_addr / 4];

    /* One MPDU matched and completed successfully (completion state zero). */
    complete = (complete & ~0x00ff0000) | BIT(16);
    s->mem[complete_addr / 4] = complete;
    s->mem[ESP32C5_WIFI_TXQ_STATE / 4] |= BIT(qid);
    set_interrupt(s, BIT(7));
}

static void esp32c5_wifi_update_scan_channel(const mac80211_frame *frame,
                                             uint32_t frame_len)
{
    const uint8_t *ie;
    size_t remaining;

    if (frame->frame_control.type != IEEE80211_TYPE_MGT ||
        frame->frame_control.sub_type != IEEE80211_TYPE_MGT_SUBTYPE_PROBE_REQ ||
        frame_len <= IEEE80211_HEADER_SIZE + 4) {
        return;
    }

    ie = frame->data_and_fcs;
    remaining = frame_len - IEEE80211_HEADER_SIZE - 4;
    while (remaining >= 2) {
        size_t ie_len = ie[1];

        if (ie_len + 2 > remaining) {
            break;
        }
        if (ie[0] == IEEE80211_BEACON_PARAM_CHANNEL && ie_len == 1 &&
            ie[2] >= 1 && ie[2] <= 14) {
            esp32_wifi_channel = ie[2];
            return;
        }
        ie += ie_len + 2;
        remaining -= ie_len + 2;
    }
}

static void esp32c5_wifi_tx(Esp32WifiState *s, uint32_t kick, unsigned qid)
{
    mac80211_frame frame = { 0 };
    uint8_t queue_entry[8];
    uint8_t tx_desc[ESP32C5_WIFI_TX_DESC_FRAME_OFFSET];
    hwaddr queue_addr = ESP32C5_WIFI_TX_SRAM_BASE |
                        (kick & ESP32C5_WIFI_TX_SRAM_MASK);
    hwaddr tx_desc_addr;
    uint32_t frame_len;

    /*
     * MAC-v3 queue links contain an embedded-buffer pointer in their second
     * word.  The pointed-to TX descriptor starts with the MPDU length and a
     * control word; the actual 802.11 MPDU immediately follows those words.
     */
    address_space_read(&address_space_memory, queue_addr,
                       MEMTXATTRS_UNSPECIFIED, queue_entry,
                       sizeof(queue_entry));
    tx_desc_addr = ldl_le_p(queue_entry + 4);
    if ((tx_desc_addr & 0xfff80000) != ESP32C5_WIFI_TX_SRAM_BASE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32c5_wifi: invalid TX descriptor %08" HWADDR_PRIx
                      " from queue %08" HWADDR_PRIx "\n",
                      tx_desc_addr, queue_addr);
        esp32c5_wifi_tx_complete(s, qid);
        return;
    }

    address_space_read(&address_space_memory, tx_desc_addr,
                       MEMTXATTRS_UNSPECIFIED, tx_desc, sizeof(tx_desc));
    frame_len = ldl_le_p(tx_desc) & 0x3fff;
    if (frame_len < IEEE80211_HEADER_SIZE ||
        frame_len > ESP32C5_WIFI_MAX_FRAME_LEN) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32c5_wifi: invalid TX length %u at %08" HWADDR_PRIx
                      "\n", frame_len, tx_desc_addr);
        esp32c5_wifi_tx_complete(s, qid);
        return;
    }

    address_space_read(&address_space_memory,
                       tx_desc_addr + ESP32C5_WIFI_TX_DESC_FRAME_OFFSET,
                       MEMTXATTRS_UNSPECIFIED, &frame, frame_len);
    frame.frame_length = frame_len;
    frame.next_frame = NULL;
    esp32c5_wifi_update_scan_channel(&frame, frame_len);
    esp32c5_wifi_tx_complete(s, qid);
    Esp32_WLAN_handle_frame(s, &frame);
}

__attribute__((weak)) void Esp32_WLAN_frame_delivered(Esp32WifiState *s){
    s->raw_interrupt |= 0x80;
    qemu_set_irq(s->irq, 1);
}

static void esp32C3_wifi_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size) {
    Esp32WifiState *s = ESP32C5_WIFI(opaque);
    if(DEBUG) printf("esp32C3_wifi_write 0x%04lx= 0x%08lx\n",(unsigned long) addr, (unsigned long) value);

    if (addr <= ESP32C5_WIFI_TXQ_LINK_BASE &&
        addr > ESP32C5_WIFI_TXQ_LINK_BASE -
               ESP32C5_WIFI_TXQ_COUNT * ESP32C5_WIFI_TXQ_STRIDE &&
        (ESP32C5_WIFI_TXQ_LINK_BASE - addr) % ESP32C5_WIFI_TXQ_STRIDE == 0 &&
        (value & 0xc0000000)) {
        unsigned qid = (ESP32C5_WIFI_TXQ_LINK_BASE - addr) /
                       ESP32C5_WIFI_TXQ_STRIDE;

        esp32c5_wifi_tx(s, value, qid);
    }

    switch (addr) {
        case ESP32C5_WIFI_RX_DESC_RELOAD:
            /*
             * Bit 0 is a command: hardware reloads the appended RX
             * descriptors and clears it.  Leaving it set makes the Wi-Fi
             * library time out and dump its RX linked-list diagnostics.
             */
            value &= ~BIT(0);
            break;
        case 0xd6c:
            break;
        case 0xc34:
        case 0xc48:
        case 0xc4c:
            s->raw_interrupt &= ~value;
            if (s->raw_interrupt == 0) {
                qemu_set_irq(s->irq, 0);
            }
            break;
        case A_C3_WIFI_DMA_IN_STATUS:
            /* C6 supplies the full receive descriptor address here. */
            s->dma_inlink_address = value;
            break;
        case A_C3_WIFI_DMA_INLINK:
            s->dma_inlink_address = 0x40800000 | (value & 0x7ffff);
            break;
        case A_C3_WIFI_DMA_INT_CLR:
            s->raw_interrupt &= ~value;
            if(s->raw_interrupt == 0)
                qemu_set_irq(s->irq, 0);
            break;
        case ESP32C5_WIFI_TXQ_STATE_CLEAR:
            s->mem[ESP32C5_WIFI_TXQ_STATE / 4] &= ~value;
            break;
        case A_C3_WIFI_DMA_OUTLINK:
            if (value & 0xc0000000) {                        
                // do a DMA transfer to the hardware from esp32 memory
                mac80211_frame frame;
                dma_list_item item;
                unsigned memaddr = 0x40800000 | (value & 0x7ffff);
                address_space_read(&address_space_memory, memaddr,
                            MEMTXATTRS_UNSPECIFIED, &item, 12);
                address_space_read(&address_space_memory, item.address,
                            MEMTXATTRS_UNSPECIFIED, &frame, item.length);
                // frame from esp32 to ap
                frame.frame_length=item.length;
                frame.next_frame=0;
                Esp32_WLAN_handle_frame(s, &frame);
                set_interrupt(s,0x80);
            }
    }
    s->mem[addr/4]=value;
}

// frame from ap to esp32
static void esp32c5_wifi_send_frame(Esp32WifiState *s,
                                    mac80211_frame *frame, int length,
                                    int signal_strength) {
    if (DEBUG) {
        printf("esp32c5 RX frame len=%d inlink=0x%08x\n",
               length, s->dma_inlink_address);
    }
    if(s->dma_inlink_address==0) return;

    bool is_mgmt = frame->frame_control.type == IEEE80211_TYPE_MGT;
    bool is_beacon = is_mgmt && frame->frame_control.sub_type ==
                     IEEE80211_TYPE_MGT_SUBTYPE_BEACON;
    bool is_espnow = is_mgmt && frame->frame_control.sub_type ==
                     IEEE80211_TYPE_MGT_SUBTYPE_ACTION;
    bool has_csi = is_beacon || is_espnow;
    int csi_len = has_csi ? ESP32C5_WIFI_CSI_LEN : 0;
    int total_len = ESP32C5_WIFI_RX_CTRL_LEN + csi_len + length;
    uint8_t *header=malloc(total_len);
    memset(header,0,total_len);

    /* ESP32-C5 MAC v3 hardware RX-control layout (64 bytes). */
    header[0] = signal_strength + (rand() % 10) - 60;
    /* CSI requires an OFDM L-LTF; model CSI-bearing packets as 6 Mbps 11g. */
    header[1] = has_csi ? 0x0b : 0; /* 6 Mbps OFDM or 1 Mbps DSSS */
    /* Report exactly the active virtual interface, as real MAC filtering does. */
    header[3] = s->mode == Esp32_Mode_Station ? BIT(4) : BIT(5);
    stl_le_p(header + 12, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000);
    header[20] = (uint8_t)-97;
    header[28] = esp32_wifi_channel;
    header[37] = has_csi ? 0x10 : 0; /* RX_BB_FORMAT_11G or RX_BB_FORMAT_11B */
    stw_le_p(header + 56, length & 0x3fff);
    stw_le_p(header + 58, (length - 4) & 0x3fff);
    if (frame->destination_address[0] & 1) {
        header[11] |= BIT(7); /* is_group */
    }

    /*
     * esp_csi_gain_ctrl_get_rx_gain() reads the signed FFT gain from byte 22
     * and the unsigned AGC gain from byte 23 of the reconstructed C6 RX-control
     * block.  These are reserved in the public esp_wifi_rxctrl_t declaration,
     * but are populated by real C6 MAC hardware.
     */
    header[ESP32C5_WIFI_FFT_GAIN_OFFSET] =
        (uint8_t)(int8_t)ESP32C5_WIFI_DEFAULT_FFT_GAIN;
    header[ESP32C5_WIFI_AGC_GAIN_OFFSET] =
        ESP32C5_WIFI_DEFAULT_AGC_GAIN;

    if (csi_len) {
        /*
         * ESP32-C5 MAC v3 inserts CSI before the final eight bytes of its
         * hardware RX-control block:
         *
         *   RX control[0..83], CSI, RX control[84..91], 802.11 frame
         *
         * IDF's RX path copies the two RX-control fragments back together and
         * skips rx_channel_estimate_len bytes between them.  This differs from
         * C3, whose split is at byte 44.  The C5 CSI length is the 10-bit field
         * at bytes 38..39; bit 2 of byte 39 marks the estimate as valid.
         */
        header[ESP32C5_WIFI_CSI_LEN_LOW_OFFSET] = csi_len & 0xff;
        header[ESP32C5_WIFI_CSI_LEN_HIGH_OFFSET] =
            ((csi_len >> 8) & 0x03) | ESP32C5_WIFI_CSI_VALID;

        memmove(header + ESP32C5_WIFI_RX_CTRL_CSI_OFFSET + csi_len,
                header + ESP32C5_WIFI_RX_CTRL_CSI_OFFSET,
                ESP32C5_WIFI_RX_CTRL_LEN -
                ESP32C5_WIFI_RX_CTRL_CSI_OFFSET);

        esp32c5_wifi_generate_csi(
                                  header + ESP32C5_WIFI_RX_CTRL_CSI_OFFSET,
                                  csi_len,
                                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    }

    memcpy(header + ESP32C5_WIFI_RX_CTRL_LEN + csi_len,
           frame, length);

    // do a DMA transfer from the hardware to esp32 memory
    uint8_t desc[12];
    uint32_t desc_ctrl;
    uint32_t desc_size;
    uint32_t buffer_addr;
    uint32_t next_desc;

    address_space_read(&address_space_memory, s->dma_inlink_address,
                       MEMTXATTRS_UNSPECIFIED, desc, sizeof(desc));
    desc_ctrl = ldl_le_p(desc);
    desc_size = desc_ctrl & ESP32C5_WIFI_RX_DESC_SIZE_MASK;
    buffer_addr = ldl_le_p(desc + 4);
    next_desc = ldl_le_p(desc + 8);
    if (DEBUG) {
        printf("esp32c5 RX desc ctrl=%03x/%03x owner=%u addr=%08x next=%08x\n",
               desc_size,
               (desc_ctrl & ESP32C5_WIFI_RX_DESC_LENGTH_MASK) >>
                   ESP32C5_WIFI_RX_DESC_LENGTH_SHIFT,
               !!(desc_ctrl & ESP32C5_WIFI_RX_DESC_OWNER), buffer_addr,
               next_desc);
    }
    if (total_len > desc_size) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "esp32c5_wifi: RX frame %d exceeds descriptor size %u\n",
                      total_len, desc_size);
        free(header);
        return;
    }
    address_space_write(&address_space_memory, buffer_addr,
                        MEMTXATTRS_UNSPECIFIED, header, total_len);
    desc_ctrl &= ~(ESP32C5_WIFI_RX_DESC_LENGTH_MASK |
                   ESP32C5_WIFI_RX_DESC_EOF |
                   ESP32C5_WIFI_RX_DESC_OWNER);
    desc_ctrl |= (uint32_t)total_len << ESP32C5_WIFI_RX_DESC_LENGTH_SHIFT;
    desc_ctrl |= ESP32C5_WIFI_RX_DESC_EOF;
    stl_le_p(desc, desc_ctrl);
    address_space_write(&address_space_memory, s->dma_inlink_address,
                        MEMTXATTRS_UNSPECIFIED, desc, sizeof(desc_ctrl));
    s->mem[0x88 / 4] = next_desc;
    s->mem[0x8c / 4] = s->dma_inlink_address & 0x000fffff;
    s->mem[0xc70 / 4] = s->dma_inlink_address & 0xfff00000;
    s->dma_inlink_address = next_desc;
    set_interrupt(s, BIT(14)); /* C6 MAC RX-success event */
    free(header);
}

static const MemoryRegionOps esp32C3_wifi_ops = {
    .read =  esp32C3_wifi_read,
    .write = esp32C3_wifi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c5_wifi_reset_enter(Object *obj, ResetType type)
{
    Esp32WifiState *s = ESP32C5_WIFI(obj);

    s->dma_inlink_address=0;
    memset(s->mem,0,sizeof(s->mem));
    Esp32_WLAN_reset_ap(s);
}

static void esp32C3_wifi_realize(DeviceState *dev, Error **errp)
{
    Esp32WifiState *s = ESP32C5_WIFI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    s->dma_inlink_address=0;

    memory_region_init_io(&s->iomem, OBJECT(dev), &esp32C3_wifi_ops, s,
                          TYPE_ESP32C5_WIFI, 0x5000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    memset(s->mem,0,sizeof(s->mem));
    s->send_frame = esp32c5_wifi_send_frame;
    Esp32_WLAN_setup_ap(dev, s);
}
static Property esp32C3_wifi_properties[] = {
    DEFINE_NIC_PROPERTIES(Esp32WifiState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32C3_wifi_class_init(ObjectClass *klass, void *data)
{
	ResettablePhases rp;
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32C3_wifi_realize;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "Esp32C3 WiFi";
    device_class_set_props(dc, esp32C3_wifi_properties);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    resettable_class_set_parent_phases(rc, esp32c5_wifi_reset_enter, NULL, NULL, &rp);
}


static const TypeInfo esp32C3_wifi_info = {
    .name = TYPE_ESP32C5_WIFI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32WifiState),
    .class_init    = esp32C3_wifi_class_init,
};

static void esp32C3_wifi_register_types(void)
{
    type_register_static(&esp32C3_wifi_info);
}

type_init(esp32C3_wifi_register_types)
__attribute__((weak)) int esp32_wifi_channel = 5;
