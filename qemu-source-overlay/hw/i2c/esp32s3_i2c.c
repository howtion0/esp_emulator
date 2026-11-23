/*
 * ESP32-S3 I2C controller emulation (master mode, simplified)
 *
 * Emulates the ESP32-S3 "I2C v2" peripheral enough for ESP-IDF's
 * esp_driver_i2c master implementation (polling & interrupt paths):
 *   - command queue (COMD0..7) executed synchronously on TRANS_START
 *   - TX/RX FIFOs via I2C_DATA_REG
 *   - int_raw/int_ena/int_status/int_clr
 *
 * Copyright (c) 2026
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/i2c/esp32s3_i2c.h"

static bool i2c_debug = false;

/* register offsets */
#define R_SCL_LOW_PERIOD  0x00
#define R_CTR             0x04
#define R_SR              0x08
#define R_TO              0x0C
#define R_SLAVE_ADDR      0x10
#define R_FIFO_ST         0x14
#define R_FIFO_CONF       0x18
#define R_DATA            0x1C
#define R_INT_RAW         0x20
#define R_INT_CLR         0x24
#define R_INT_ENA         0x28
#define R_INT_STATUS      0x2C
#define R_TIMING_BASE     0x30   /* sda_hold..clk_conf, 10 regs, skip 0x3C reserved */
#define R_COMD0           0x58
#define R_SCL_ST_TO       0x78
#define R_SCL_MAIN_ST_TO  0x7C
#define R_SCL_SP_CONF     0x80
#define R_SCL_STRETCH     0x84
#define R_DATE            0xF8

/* CTR bits */
#define CTR_TRANS_START   (BIT(5))
#define CTR_TX_FIFO_RST   (BIT(6))
#define CTR_RX_FIFO_RST   (BIT(7))
#define CTR_MS_MODE       (BIT(4))

/* command op codes */
#define CMD_RSTART 6
#define CMD_WRITE  1
#define CMD_READ   3
#define CMD_STOP   2
#define CMD_END    4

/* int_raw bits */
#define INT_RXFIFO_WM      (BIT(0))
#define INT_TXFIFO_WM      (BIT(1))
#define INT_RXFIFO_OVF     (BIT(2))
#define INT_END_DETECT     (BIT(3))
#define INT_BYTE_DONE      (BIT(4))
#define INT_ARB_LOST       (BIT(5))
#define INT_TRANS_COMPLETE (BIT(7))
#define INT_TIME_OUT       (BIT(8))
#define INT_TRANS_START    (BIT(9))
#define INT_NACK           (BIT(10))

/* SR bits */
#define SR_RESP_REC       (BIT(0))
#define SR_SLAVE_RW       (BIT(1))
#define SR_ARB_LOST       (BIT(3))
#define SR_BUS_BUSY       (BIT(4))
#define SR_SLAVE_ADDRESSED (BIT(5))
#define SR_RXFIFO_CNT_SHIFT 8
#define SR_TXFIFO_CNT_SHIFT 18

static unsigned txfifo_count(Esp32s3I2cState *s)
{
    return (s->txfifo_w - s->txfifo_r) % (ESP32S3_I2C_FIFO_SIZE * 2);
}

static unsigned rxfifo_count(Esp32s3I2cState *s)
{
    return (s->rxfifo_w - s->rxfifo_r) % (ESP32S3_I2C_FIFO_SIZE * 2);
}

static void txfifo_reset(Esp32s3I2cState *s)
{
    s->txfifo_w = s->txfifo_r = 0;
}

static void rxfifo_reset(Esp32s3I2cState *s)
{
    s->rxfifo_w = s->rxfifo_r = 0;
}

static void txfifo_push(Esp32s3I2cState *s, uint8_t b)
{
    s->txfifo[s->txfifo_w % ESP32S3_I2C_FIFO_SIZE] = b;
    s->txfifo_w++;
}

static uint8_t txfifo_pop(Esp32s3I2cState *s)
{
    uint8_t b = s->txfifo[s->txfifo_r % ESP32S3_I2C_FIFO_SIZE];
    s->txfifo_r++;
    return b;
}

static void rxfifo_push(Esp32s3I2cState *s, uint8_t b)
{
    if (rxfifo_count(s) < ESP32S3_I2C_FIFO_SIZE) {
        s->rxfifo[s->rxfifo_w % ESP32S3_I2C_FIFO_SIZE] = b;
        s->rxfifo_w++;
    } else {
        s->int_raw |= INT_RXFIFO_OVF;
    }
}

static uint8_t rxfifo_pop(Esp32s3I2cState *s)
{
    if (rxfifo_count(s) == 0) {
        return 0xff;
    }
    uint8_t b = s->rxfifo[s->rxfifo_r % ESP32S3_I2C_FIFO_SIZE];
    s->rxfifo_r++;
    return b;
}

static void esp32s3_i2c_update_irq(Esp32s3I2cState *s)
{
    qemu_set_irq(s->irq, (s->int_raw & s->int_ena) != 0);
}

/* Execute the command queue synchronously. */
static void esp32s3_i2c_execute(Esp32s3I2cState *s)
{
    bool in_transfer = false;
    bool aborted = false;

    for (int i = 0; i < ESP32S3_I2C_COMD_COUNT && !aborted; i++) {
        uint32_t cmd = s->comd[i];
        uint32_t op = (cmd >> 11) & 0x7;
        uint32_t byte_num = cmd & 0xff;

        switch (op) {
        case CMD_RSTART:
            /* repeated start: end any open transfer; the address phase
             * begins at the next WRITE command */
            if (in_transfer) {
                i2c_end_transfer(s->bus);
                in_transfer = false;
            }
            s->int_raw |= INT_TRANS_START;
            s->comd[i] |= BIT(31);
            break;

        case CMD_WRITE: {
            if (byte_num == 0) {
                s->comd[i] |= BIT(31);
                break;
            }
            if (!in_transfer) {
                /* First byte in FIFO is the address byte */
                if (txfifo_count(s) == 0) {
                    aborted = true;
                    s->int_raw |= INT_TIME_OUT;
                    break;
                }
                uint8_t addr_byte = txfifo_pop(s);
                uint8_t addr = addr_byte >> 1;
                bool read = addr_byte & 1;
                int r = i2c_start_transfer(s->bus, addr, read);
                if (i2c_debug) {
                    fprintf(stderr, "[i2c] start addr=0x%02x read=%d -> %d\n", addr, read, r);
                }
                if (r) {
                    /* no device ACKed the address */
                    i2c_end_transfer(s->bus);
                    s->int_raw |= INT_NACK | INT_TRANS_COMPLETE;
                    aborted = true;
                    break;
                }
                in_transfer = true;
                byte_num--;
                s->int_raw |= INT_TRANS_START;
            }
            bool nack = false;
            for (uint32_t b = 0; b < byte_num; b++) {
                if (txfifo_count(s) == 0) {
                    nack = true;
                    break;
                }
                uint8_t data = txfifo_pop(s);
                if (i2c_send(s->bus, data)) {
                    nack = true;
                    break;
                }
                s->int_raw |= INT_BYTE_DONE;
            }
            s->comd[i] |= BIT(31);
            if (nack) {
                s->int_raw |= INT_NACK | INT_TRANS_COMPLETE;
                aborted = true;
            }
            break;
        }

        case CMD_READ:
            if (!in_transfer) {
                aborted = true;
                s->int_raw |= INT_TIME_OUT;
                break;
            }
            for (uint32_t b = 0; b < byte_num; b++) {
                uint8_t data = i2c_recv(s->bus);
                rxfifo_push(s, data);
                s->int_raw |= INT_BYTE_DONE;
            }
            s->comd[i] |= BIT(31);
            break;

        case CMD_STOP:
            if (in_transfer) {
                i2c_end_transfer(s->bus);
                in_transfer = false;
            }
            s->comd[i] |= BIT(31);
            s->int_raw |= INT_TRANS_COMPLETE;
            goto finished;

        case CMD_END:
            s->comd[i] |= BIT(31);
            s->int_raw |= INT_END_DETECT;
            goto finished;

        default:
            /* empty command slot: stop queue */
            aborted = true;
            break;
        }
    }

finished:
    if (in_transfer) {
        /* driver forgot STOP; close the bus anyway */
        i2c_end_transfer(s->bus);
    }
    esp32s3_i2c_update_irq(s);
}

static uint64_t esp32s3_i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp32s3I2cState *s = ESP32S3_I2C(opaque);
    if (i2c_debug) {
        fprintf(stderr, "[i2c] read off=0x%02llx\n", (unsigned long long)addr);
    }

    switch (addr) {
    case R_CTR:
        return s->ctr;
    case R_SR:
        /* transfers complete synchronously, so the bus is always idle */
        return (rxfifo_count(s) << SR_RXFIFO_CNT_SHIFT) |
               (txfifo_count(s) << SR_TXFIFO_CNT_SHIFT);
    case R_TO:
        return s->to;
    case R_SLAVE_ADDR:
        return s->slave_addr;
    case R_FIFO_ST:
        return rxfifo_count(s) | (txfifo_count(s) << 5);
    case R_FIFO_CONF:
        return s->fifo_conf;
    case R_DATA:
        return rxfifo_pop(s);
    case R_INT_RAW:
        return s->int_raw;
    case R_INT_STATUS:
        return s->int_raw & s->int_ena;
    case R_INT_ENA:
        return s->int_ena;
    case R_COMD0 ... (R_COMD0 + 4 * ESP32S3_I2C_COMD_COUNT - 1):
        return s->comd[(addr - R_COMD0) / 4];
    case R_SCL_ST_TO:
        return s->scl_st_to;
    case R_SCL_MAIN_ST_TO:
        return s->scl_main_st_to;
    case R_SCL_SP_CONF:
        return s->scl_sp_conf;
    case R_SCL_STRETCH:
        return s->scl_stretch_conf;
    case R_DATE:
        return 0x24061900; /* arbitrary version */
    default:
        if (addr >= R_TIMING_BASE && addr < R_COMD0) {
            unsigned idx = (addr - R_TIMING_BASE) / 4;
            if ((addr - R_TIMING_BASE) % 4 == 0 && idx < 10) {
                return s->timing[idx];
            }
        }
        return 0;
    }
}

static void esp32s3_i2c_write(void *opaque, hwaddr addr, uint64_t v, unsigned size)
{
    Esp32s3I2cState *s = ESP32S3_I2C(opaque);
    if (i2c_debug) {
        fprintf(stderr, "[i2c] write off=0x%02llx val=0x%llx\n", (unsigned long long)addr, (unsigned long long)v);
    }

    switch (addr) {
    case R_CTR:
        s->ctr = v & ~CTR_TRANS_START; /* trans_start is write-trigger */
        if (v & BIT(10)) { /* FSM reset, not the byte-order bits 6/7 */
            i2c_end_transfer(s->bus);
            txfifo_reset(s); rxfifo_reset(s);
            s->int_raw = 0;
            esp32s3_i2c_update_irq(s);
        }
        s->ctr &= ~BIT(11); /* configuration update completed */
        if (v & CTR_TRANS_START) {
            esp32s3_i2c_execute(s);
        }
        break;
    case R_FIFO_CONF:
        s->fifo_conf = v;
        if (v & BIT(12)) { /* rx fifo rst */
            rxfifo_reset(s);
        }
        if (v & BIT(13)) { /* tx fifo rst */
            txfifo_reset(s);
        }
        break;
    case R_DATA:
        txfifo_push(s, v & 0xff);
        break;
    case R_INT_CLR:
        s->int_raw &= ~v;
        esp32s3_i2c_update_irq(s);
        break;
    case R_INT_ENA:
        s->int_ena = v;
        esp32s3_i2c_update_irq(s);
        break;
    case R_COMD0 ... (R_COMD0 + 4 * ESP32S3_I2C_COMD_COUNT - 1):
        s->comd[(addr - R_COMD0) / 4] = v;
        break;
    case R_TO:
        s->to = v;
        break;
    case R_SLAVE_ADDR:
        s->slave_addr = v;
        break;
    case R_SCL_ST_TO:
        s->scl_st_to = v;
        break;
    case R_SCL_MAIN_ST_TO:
        s->scl_main_st_to = v;
        break;
    case R_SCL_SP_CONF:
        s->scl_sp_conf = v;
        break;
    case R_SCL_STRETCH:
        s->scl_stretch_conf = v;
        break;
    default:
        if (addr >= R_TIMING_BASE && addr < R_COMD0) {
            unsigned idx = (addr - R_TIMING_BASE) / 4;
            if ((addr - R_TIMING_BASE) % 4 == 0 && idx < 10) {
                s->timing[idx] = v;
                break;
            }
        }
        break;
    }
}

static const MemoryRegionOps esp32s3_i2c_ops = {
    .read = esp32s3_i2c_read,
    .write = esp32s3_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void esp32s3_i2c_reset(DeviceState *dev)
{
    Esp32s3I2cState *s = ESP32S3_I2C(dev);

    s->ctr = 0;
    s->to = 0;
    s->fifo_conf = 0;
    s->int_raw = 0;
    s->int_ena = 0;
    memset(s->comd, 0, sizeof(s->comd));
    txfifo_reset(s);
    rxfifo_reset(s);
}

static void esp32s3_i2c_realize(DeviceState *dev, Error **errp)
{
    Esp32s3I2cState *s = ESP32S3_I2C(dev);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(dev), &esp32s3_i2c_ops, s,
                          "esp32s3.i2c", ESP32S3_I2C_IO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    s->bus = i2c_init_bus(dev, "i2c");
}

static const VMStateDescription esp32s3_i2c_vmstate = {
    .name = TYPE_ESP32S3_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctr, Esp32s3I2cState),
        VMSTATE_UINT32(int_raw, Esp32s3I2cState),
        VMSTATE_UINT32(int_ena, Esp32s3I2cState),
        VMSTATE_UINT32_ARRAY(comd, Esp32s3I2cState, ESP32S3_I2C_COMD_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static void esp32s3_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = esp32s3_i2c_realize;
    device_class_set_legacy_reset(dc, esp32s3_i2c_reset);
    dc->vmsd = &esp32s3_i2c_vmstate;
}

static const TypeInfo esp32s3_i2c_info = {
    .name = TYPE_ESP32S3_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3I2cState),
    .class_init = esp32s3_i2c_class_init,
};

static void esp32s3_i2c_register_types(void)
{
    type_register_static(&esp32s3_i2c_info);
}

type_init(esp32s3_i2c_register_types)
