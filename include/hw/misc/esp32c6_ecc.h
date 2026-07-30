#ifndef HW_ESP32C6_ECC_H
#define HW_ESP32C6_ECC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ESP32C6_ECC "esp32c6.ecc"
OBJECT_DECLARE_SIMPLE_TYPE(ESP32C6EccState, ESP32C6_ECC)

struct ESP32C6EccState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t conf;
    uint8_t k_mem[32];
    uint8_t px_mem[32];
    uint8_t py_mem[32];
};

#endif
