#pragma once

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/dma/esp_gdma.h"
#include "ui/console.h"

#define TYPE_ESP32S3_GP_SPI "esp32s3.gp-spi"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3GpSpiState, ESP32S3_GP_SPI)

#define ESP32S3_GP_SPI_IO_SIZE  0x1000
#define ESP32S3_GP_SPI_BUF_WORDS 16

typedef struct Esp32s3GpSpiState {
    SysBusDevice parent_obj;

    ESPGdmaState *gdma;
    int peripheral;
    QemuConsole *con;
    uint16_t fb[368*448];
    uint16_t x0, x1, y0, y1, px, py;
    uint8_t lcd_cmd, madctl;
    bool display_on;
    uint64_t pixels;
    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t regs[ESP32S3_GP_SPI_IO_SIZE / 4];
} Esp32s3GpSpiState;
