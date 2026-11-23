/*
 * Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2) board I2C devices:
 *   - CST816S capacitive touch @ 0x15 with touch injection via chardev
 *   - generic stubs: AXP2101 @0x34, TCA9554 @0x20, QMI8658 @0x6A,
 *     PCF85063A @0x51, ES8311 @0x18
 *
 * Touch injection: connect a chardev (e.g. -chardev socket) and send
 * ASCII lines "x,y,1\n" (press at x,y) or "x,y,0\n" (release).
 * Coordinates use the panel resolution 368x448.
 *
 * Copyright (c) 2026
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "qemu/module.h"
#include "qemu/queue.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/i2c/esp32s3_board_i2c.h"
#include "qapi/error.h"

/* ------------------- CST816S ------------------- */

#define CST816S_DATA_REG      0x02
#define CST816S_CHIP_ID_REG   0xA7
#define CST816S_CHIP_ID       0xB5
#define CST816S_PANEL_W       368
#define CST816S_PANEL_H       448

static void cst816s_set_touch(Cst816sState *s, uint16_t x, uint16_t y, bool down)
{
    uint8_t num = down ? 1 : 0;
    if (x >= CST816S_PANEL_W) x = CST816S_PANEL_W - 1;
    if (y >= CST816S_PANEL_H) y = CST816S_PANEL_H - 1;

    memset(&s->regs[CST816S_DATA_REG], 0, 6);
    s->regs[0x02] = num;
    /* the driver's bitfield x_h:4 occupies the LOW 4 bits of the byte */
    s->regs[0x03] = (x >> 8) & 0x0f;   /* x high 4 bits */
    s->regs[0x04] = x & 0xff;          /* x low */
    s->regs[0x05] = (y >> 8) & 0x0f;   /* y high 4 bits */
    qemu_set_irq(s->irq,1);
    s->regs[0x06] = y & 0xff;          /* y low */
    qemu_set_irq(s->irq,0); qemu_set_irq(s->irq,1);
}

static int cst816s_can_receive(void *opaque)
{
    return 128;
}

static void cst816s_receive(void *opaque, const uint8_t *buf, int size)
{
    Cst816sState *s = CST816S(opaque);
    static char line[64];
    static int len = 0;

    for (int i = 0; i < size; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
            line[len] = 0;
            if (len > 0) {
                int x = 0, y = 0, p = 0;
                if (sscanf(line, "%d,%d,%d", &x, &y, &p) >= 2) {
                    cst816s_set_touch(s, x, y, p != 0);
                }
                len = 0;
            }
        } else if (len < (int)sizeof(line) - 1) {
            line[len++] = c;
        }
    }
}

static void cst816s_event(void *opaque, QEMUChrEvent event)
{
    /* nothing needed */
}

static uint8_t cst816s_recv(I2CSlave *i2c)
{
    Cst816sState *s = CST816S(i2c);
    uint8_t v = s->regs[s->reg];
    s->reg = (s->reg + 1) & 0xff;
    return v;
}

static int cst816s_send(I2CSlave *i2c, uint8_t data)
{
    Cst816sState *s = CST816S(i2c);
    if (s->expect_register) { s->reg=data; s->expect_register=false; }
    else { s->regs[s->reg++]=data; }
    return 0;
}

static int cst816s_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    Cst816sState *s=CST816S(i2c);
    if (event==I2C_START_SEND) s->expect_register=true;
    return 0;
}

static void cst816s_do_reset(I2CSlave *i2c);

static void cst816s_reset_dev(DeviceState *dev)
{
    cst816s_do_reset(I2C_SLAVE(dev));
}

static void cst816s_do_reset(I2CSlave *i2c)
{
    Cst816sState *s = CST816S(i2c);
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[CST816S_CHIP_ID_REG] = CST816S_CHIP_ID;
    s->reg = 0;
}

static Property cst816s_properties[] = {
    DEFINE_PROP_CHR("touchdev", Cst816sState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void cst816s_realize(DeviceState *dev, Error **errp)
{
    Cst816sState *s = CST816S(dev);

    qdev_init_gpio_out(dev,&s->irq,1);
    if (s->chr.chr) {
        qemu_chr_fe_set_handlers(&s->chr, cst816s_can_receive, cst816s_receive,
                                 cst816s_event, NULL, s, NULL, true);
    }
    cst816s_do_reset(I2C_SLAVE(dev));
}

static void cst816s_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = cst816s_realize;
    device_class_set_legacy_reset(dc, cst816s_reset_dev);
    device_class_set_props(dc, cst816s_properties);
    k->event = cst816s_i2c_event;
    k->recv = cst816s_recv;
    k->send = cst816s_send;
}

static const TypeInfo cst816s_info = {
    .name = TYPE_CST816S,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(Cst816sState),
    .class_init = cst816s_class_init,
};

/* ------------------- generic stub ------------------- */

static uint8_t stub_recv(I2CSlave *i2c)
{
    Esp32s3I2cStubState *s = ESP32S3_I2C_STUB(i2c);
    uint8_t v = s->regs[s->reg];
    s->reg = (s->reg + 1) & 0xff;
    return v;
}

static int stub_send(I2CSlave *i2c, uint8_t data)
{
    Esp32s3I2cStubState *s = ESP32S3_I2C_STUB(i2c);
    if (s->expect_register) { s->reg=data; s->expect_register=false; }
    else { s->regs[s->reg++]=data; }
    return 0;
}

static int stub_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    Esp32s3I2cStubState *s=ESP32S3_I2C_STUB(i2c);
    if (event==I2C_START_SEND) s->expect_register=true;
    return 0;
}

static void stub_do_reset(I2CSlave *i2c);

static void stub_reset_dev(DeviceState *dev)
{
    stub_do_reset(I2C_SLAVE(dev));
}

static void stub_do_reset(I2CSlave *i2c)
{
    Esp32s3I2cStubState *s = ESP32S3_I2C_STUB(i2c);
    s->reg = 0;
    memset(s->regs,0,sizeof(s->regs));
    s->expect_register=true;
    s->regs[0] = s->chip_id;
    /* Minimum documented register identities; dynamic peripherals remain stubs. */
    if (s->chip_id==0x4a) s->regs[0x03]=0x4a; /* AXP2101 IC type */
    if (s->chip_id==0x83) { s->regs[0xfd]=0x83; s->regs[0xfe]=0x11; }

}

static void stub_realize(DeviceState *dev,Error **errp)
{
    stub_do_reset(I2C_SLAVE(dev));
}

static Property stub_properties[] = {
    DEFINE_PROP_UINT8("chip_id", Esp32s3I2cStubState, chip_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void stub_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize=stub_realize;
    device_class_set_legacy_reset(dc, stub_reset_dev);
    device_class_set_props(dc, stub_properties);
    k->event = stub_i2c_event;
    k->recv = stub_recv;
    k->send = stub_send;
}

static const TypeInfo stub_info = {
    .name = TYPE_ESP32S3_I2C_STUB,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(Esp32s3I2cStubState),
    .class_init = stub_class_init,
};

static void esp32s3_board_i2c_register_types(void)
{
    type_register_static(&cst816s_info);
    type_register_static(&stub_info);
}

type_init(esp32s3_board_i2c_register_types)
