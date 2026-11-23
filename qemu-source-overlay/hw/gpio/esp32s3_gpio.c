/*
 * ESP32-S3 GPIO emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32s3_gpio.h"


static uint32_t enabled_status(ESP32S3GPIOState *s, int bank)
{
    uint32_t result=0;
    for(int bit=0;bit<32;bit++) {
        int pin=bank*32+bit;
        if(pin<49 && ((s->regs[(0x74+pin*4)/4]>>13)&0x1f) &&
           (s->regs[(bank?0x50:0x44)/4]&BIT(bit))) result|=BIT(bit);
    }
    return result;
}
static void gpio_irq_update(ESP32S3GPIOState *s)
{
    qemu_set_irq(s->parent.irq, enabled_status(s,0)||enabled_status(s,1));
}
static void board_gpio_input(void *opaque,int pin,int level)
{
    ESP32S3GPIOState *s=opaque;
    unsigned bank=pin/32,bit=pin%32;
    uint32_t *input=&s->regs[(bank?0x40:0x3c)/4];
    bool old=!!(*input&BIT(bit));
    if(level) *input|=BIT(bit); else *input&=~BIT(bit);
    int type=(s->regs[(0x74+pin*4)/4]>>7)&7;
    bool trigger=(type==1&&!old&&level)||(type==2&&old&&!level)||
        (type==3&&old!=!!level)||(type==4&&!level)||(type==5&&level);
    if(trigger) s->regs[(bank?0x50:0x44)/4]|=BIT(bit);
    gpio_irq_update(s);
}
static uint64_t board_gpio_read(void *opaque,hwaddr addr,unsigned size)
{
    ESP32S3GPIOState *s=opaque;
    if(addr==0x38)return s->parent.strap_mode;
    if(addr==0x5c||addr==0x60||addr==0x64)return enabled_status(s,0);
    if(addr==0x68||addr==0x6c||addr==0x70)return enabled_status(s,1);
    return addr<sizeof(s->regs)?s->regs[addr/4]:0;
}
static void board_gpio_write(void *opaque,hwaddr addr,uint64_t value,unsigned size)
{
    ESP32S3GPIOState *s=opaque;
    if(addr>=sizeof(s->regs))return;
    switch(addr) {
    case 0x48:s->regs[0x44/4]|=value;break;
    case 0x4c:s->regs[0x44/4]&=~value;break;
    case 0x54:s->regs[0x50/4]|=value;break;
    case 0x58:s->regs[0x50/4]&=~value;break;
    default:s->regs[addr/4]=value;break;
    }
    gpio_irq_update(s);
}
static const MemoryRegionOps board_gpio_ops={
    .read=board_gpio_read,.write=board_gpio_write,.endianness=DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size=4,.valid.max_access_size=4,
};
static void board_gpio_reset(Object *obj,ResetType type)
{
    ESP32S3GPIOState *s=ESP32S3_GPIO(obj);
    memset(s->regs,0,sizeof(s->regs));gpio_irq_update(s);
}

static void esp32s3_gpio_init(Object *obj)
{
    ESP32S3GPIOState *s=ESP32S3_GPIO(obj);
    memory_region_init_io(&s->board_io,obj,&board_gpio_ops,s,"esp32s3.board-gpio",0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj),&s->board_io);
    qdev_init_gpio_in(DEVICE(obj),board_gpio_input,49);
    /* Set the default value for the property */
    object_property_set_int(obj, "strap_mode", ESP32S3_STRAP_MODE_FLASH_BOOT, &error_fatal);
}

/* If we need to override any function from the parent (reset, realize, ...), it shall be done
 * in this class_init function */
static void esp32s3_gpio_class_init(ObjectClass *klass, void *data)
{
    RESETTABLE_CLASS(klass)->phases.hold=board_gpio_reset;
}

static const TypeInfo esp32s3_gpio_info = {
    .name = TYPE_ESP32S3_GPIO,
    .parent = TYPE_ESP32_GPIO,
    .instance_size = sizeof(ESP32S3GPIOState),
    .instance_init = esp32s3_gpio_init,
    .class_init = esp32s3_gpio_class_init,
    .class_size = sizeof(ESP32S3GPIOClass),
};

static void esp32s3_gpio_register_types(void)
{
    type_register_static(&esp32s3_gpio_info);
}

type_init(esp32s3_gpio_register_types)
