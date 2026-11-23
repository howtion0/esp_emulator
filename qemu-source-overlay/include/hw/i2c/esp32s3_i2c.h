/*
 * ESP32-S3 I2C controller emulation (master mode, simplified)
 *
 * Copyright (c) 2026
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_ESP32S3_I2C "esp32s3.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3I2cState, ESP32S3_I2C)

#define ESP32S3_I2C_IO_SIZE  0x100
#define ESP32S3_I2C_COMD_COUNT 8
#define ESP32S3_I2C_FIFO_SIZE 32

typedef struct Esp32s3I2cState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    /* registers */
    uint32_t ctr;
    uint32_t to;
    uint32_t slave_addr;
    uint32_t fifo_conf;
    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t timing[10];   /* scl_low..clk_conf raw storage */
    uint32_t comd[ESP32S3_I2C_COMD_COUNT];
    uint32_t scl_st_to, scl_main_st_to, scl_sp_conf, scl_stretch_conf;
    uint32_t date;

    /* FIFOs */
    uint8_t  txfifo[ESP32S3_I2C_FIFO_SIZE];
    unsigned txfifo_w, txfifo_r;
    uint8_t  rxfifo[ESP32S3_I2C_FIFO_SIZE];
    unsigned rxfifo_w, rxfifo_r;
} Esp32s3I2cState;
