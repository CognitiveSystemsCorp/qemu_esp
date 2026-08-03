#ifndef HW_ESP32C5_ECC_H
#define HW_ESP32C5_ECC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ESP32C5_ECC "esp32c5.ecc"
OBJECT_DECLARE_SIMPLE_TYPE(ESP32C5EccState, ESP32C5_ECC)

struct ESP32C5EccState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t conf;
    uint8_t k_mem[48];
    uint8_t px_mem[48];
    uint8_t py_mem[48];
    uint8_t qx_mem[48];
    uint8_t qy_mem[48];
    uint8_t qz_mem[48];
};

#endif
