/*
 * Waveshare ESP32-S3-Touch-AMOLED-1.8 board I2C devices
 */
#pragma once

#include "hw/i2c/i2c.h"
#include "qom/object.h"
#include "chardev/char-fe.h"

/* CST816S capacitive touch controller (V2 board) */
#define TYPE_CST816S "cst816s"
OBJECT_DECLARE_SIMPLE_TYPE(Cst816sState, CST816S)

typedef struct Cst816sState {
    I2CSlave parent_obj;
    bool expect_register;
    uint8_t reg;             /* currently selected register */
    uint8_t regs[256];
    qemu_irq irq;
    CharBackend chr;         /* touch event source: "x,y,press" lines */
} Cst816sState;

/* Generic stub device responding on any register read */
#define TYPE_ESP32S3_I2C_STUB "esp32s3.i2c.stub"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3I2cStubState, ESP32S3_I2C_STUB)

typedef struct Esp32s3I2cStubState {
    I2CSlave parent_obj;
    bool expect_register;
    uint8_t reg;
    uint8_t regs[256];
    uint8_t chip_id;
} Esp32s3I2cStubState;
