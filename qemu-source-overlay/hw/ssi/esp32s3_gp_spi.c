/*
 * ESP32-S3 GPSPI2/GPSPI3 transaction tracer.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/irq.h"
#include "hw/ssi/esp32s3_gp_spi.h"

#define GP_SPI_DEBUG 0
#define GP_SPI_WRITE_DEBUG 0

#define R_CMD        0x00
#define R_ADDR       0x04
#define R_CTRL       0x08
#define R_CLOCK      0x0c
#define R_USER       0x10
#define R_USER1      0x14
#define R_USER2      0x18
#define R_MS_DLEN    0x1c
#define R_MISC       0x20
#define R_DMA_CONF   0x30
#define R_DMA_INT_ENA 0x34
#define R_DMA_INT_RAW 0x3c
#define R_DMA_INT_CLR 0x38
#define R_DMA_INT_ST  0x40
#define R_DMA_INT_SET 0x44
#define R_W0         0x98

#define CMD_USR       BIT(24)
#define CMD_UPDATE    BIT(23)

#define USER_COMMAND  BIT(31)
#define USER_ADDR     BIT(30)
#define USER_DUMMY    BIT(29)
#define USER_MISO     BIT(28)
#define USER_MOSI     BIT(27)

static void esp32s3_gp_spi_update_irq(Esp32s3GpSpiState *s)
{
    bool level = (s->regs[R_DMA_INT_RAW / 4] &
                  s->regs[R_DMA_INT_ENA / 4]) != 0;
#if GP_SPI_WRITE_DEBUG
    fprintf(stderr, "[gp-spi] irq=%d raw=%08x ena=%08x\n", level,
            s->regs[R_DMA_INT_RAW / 4], s->regs[R_DMA_INT_ENA / 4]);
#endif
    qemu_set_irq(s->irq, level);
}

static uint32_t bitfield(uint32_t v, unsigned int shift, unsigned int width)
{
    return (v >> shift) & ((1u << width) - 1);
}


/* Functional CO5300 subset: QSPI commands, address windows, RGB565 RAM writes. */
static void panel_update(void *opaque)
{
    Esp32s3GpSpiState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint32_t *dst = surface_data(surface);
    for (int y = 0; y < 448; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)dst + y * surface_stride(surface));
        for (int x = 0; x < 368; x++) {
            uint16_t c = s->display_on ? s->fb[y*368+x] : 0;
            row[x] = ((c >> 11) * 255 / 31 << 16) |
                     (((c >> 5) & 63) * 255 / 63 << 8) | ((c & 31) * 255 / 31);
        }
    }
    dpy_gfx_update(s->con, 0, 0, 368, 448);
}
static const GraphicHwOps panel_ops = { .gfx_update = panel_update };

static void panel_transfer(Esp32s3GpSpiState *s, bool command, uint8_t cmd,
                           const uint8_t *data, unsigned len)
{
    if (!s->con) return;
    if (command) {
        s->lcd_cmd = cmd;
        if (cmd == 0x2c) { s->px = s->x0; s->py = s->y0; }
        if (cmd == 0x29) s->display_on = true;
        if (cmd == 0x28 || cmd == 0x10) s->display_on = false;
        if (cmd == 0x01) {
            memset(s->fb, 0, sizeof(s->fb));
            s->display_on = false;
        }
    }
    cmd = s->lcd_cmd;
    if ((cmd == 0x2a || cmd == 0x2b) && len >= 4) {
        uint16_t start = (data[0]<<8)|data[1], end = (data[2]<<8)|data[3];
        if (cmd == 0x2a) { s->x0=start; s->x1=end; }
        else { s->y0=start; s->y1=end; }
    } else if (cmd == 0x36 && len) {
        s->madctl = data[0];
    } else if (cmd == 0x2c || cmd == 0x3c) {
        for (unsigned i=0; i+1<len; i+=2) {
            int x=s->px, y=s->py;
            if (s->madctl & 0x20) { int t=x; x=y; y=t; }
            if (s->madctl & 0x40) x=367-x;
            if (s->madctl & 0x80) y=447-y;
            uint16_t c=(data[i]<<8)|data[i+1];
            if (s->madctl & 8) c=((c&31)<<11)|(c&0x7e0)|(c>>11);
            if (x>=0 && x<368 && y>=0 && y<448) s->fb[y*368+x]=c;
            if (++s->px > s->x1) { s->px=s->x0; if (++s->py > s->y1) s->py=s->y0; }
            s->pixels++;
        }
        if (s->pixels && s->pixels % (368*448) == 0)
            fprintf(stderr, "[co5300] pixels=%" PRIu64 " display=%d\n", s->pixels,s->display_on);
    }
}

static void esp32s3_gp_spi_execute(Esp32s3GpSpiState *s)
{
    const uint32_t user = s->regs[R_USER / 4];
    const uint32_t user1 = s->regs[R_USER1 / 4];
    const uint32_t user2 = s->regs[R_USER2 / 4];
    const uint32_t ctrl = s->regs[R_CTRL / 4];
    const uint32_t cmd_bits = bitfield(user2, 28, 4) + 1;
    const uint32_t cmd = bitfield(user2, 0, 16);
    const uint32_t addr_bits = bitfield(user1, 27, 5) + 1;
    const uint32_t addr = s->regs[R_ADDR / 4];
    const uint32_t dummy_bits = bitfield(user1, 0, 8) + 1;
    const uint32_t data_bits = bitfield(s->regs[R_MS_DLEN / 4], 0, 18) + 1;
    unsigned int tx_bytes = 0;
    unsigned int rx_bytes = 0;
    const uint8_t *buf = (const uint8_t *)&s->regs[R_W0 / 4];

    if (user & USER_MOSI) {
        tx_bytes = (data_bits + 7) / 8;
    }
    if (user & USER_MISO) {
        rx_bytes = (data_bits + 7) / 8;
    }

    if (GP_SPI_DEBUG) {
        fprintf(stderr,
                "[gp-spi] cmd=%02x(%u) addr=%08x(%u) dummy=%u tx=%u rx=%u "
                "user=%08x ctrl=%08x dlen=%u dma=%08x data:",
                cmd, cmd_bits, addr, addr_bits, dummy_bits,
                tx_bytes, rx_bytes, user, ctrl, data_bits,
                s->regs[R_DMA_CONF / 4]);
        for (unsigned int i = 0; i < 16 && i < (tx_bytes ? tx_bytes : rx_bytes); ++i) {
            fprintf(stderr, " %02x", buf[i]);
        }
        fprintf(stderr, "\n");
    }

    uint8_t *payload = NULL;
    if (tx_bytes) {
        payload = g_malloc0(tx_bytes);
        if (s->regs[R_DMA_CONF/4] & BIT(28)) {
            uint32_t channel;
            if (!esp_gdma_get_channel_periph(s->gdma, s->peripheral, ESP_GDMA_OUT_IDX, &channel) ||
                !esp_gdma_read_channel(s->gdma, channel, payload, tx_bytes)) {
                fprintf(stderr, "[gp-spi] DMA read failed\n");
                g_free(payload);
                return;
            }
        } else {
            memcpy(payload, buf, MIN(tx_bytes, 64));
        }
    }
    /* IDF sends CO5300 QSPI header as opcode + 24-bit address. */
    uint8_t lcd = (user & USER_ADDR) ? ((addr >> (32 - addr_bits + 8)) & 0xff) : cmd;
    if (tx_bytes == 4 && !(user & BIT(13)) && payload &&
        (payload[0] == 0x02 || payload[0] == 0x32) && payload[1] == 0 && payload[3] == 0) {
        panel_transfer(s, true, payload[2], NULL, 0);
    } else {
        panel_transfer(s, (user & (USER_COMMAND|USER_ADDR)) != 0, lcd, payload, tx_bytes);
    }
    g_free(payload);
    s->regs[R_CMD / 4] &= ~CMD_USR;
}

static uint64_t esp32s3_gp_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32s3GpSpiState *s = ESP32S3_GP_SPI(opaque);

    if (addr >= ESP32S3_GP_SPI_IO_SIZE) {
        return 0;
    }
    if (addr == R_CMD) {
        return s->regs[addr / 4] & ~CMD_USR;
    }
    if (addr == R_DMA_INT_ST) {
        return s->regs[R_DMA_INT_RAW / 4] & s->regs[R_DMA_INT_ENA / 4];
    }
    return s->regs[addr / 4];
}

static void esp32s3_gp_spi_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned int size)
{
    Esp32s3GpSpiState *s = ESP32S3_GP_SPI(opaque);

    if (addr >= ESP32S3_GP_SPI_IO_SIZE) {
        return;
    }
#if GP_SPI_WRITE_DEBUG
    fprintf(stderr, "[gp-spi] write off=%03llx val=%08llx\n",
            (unsigned long long)addr, (unsigned long long)value);
#endif
    s->regs[addr / 4] = value;
    if (addr == R_CMD && (value & CMD_UPDATE)) s->regs[addr / 4] &= ~CMD_UPDATE;
    if (addr == R_DMA_INT_SET) {
        s->regs[R_DMA_INT_RAW / 4] |= value;
        esp32s3_gp_spi_update_irq(s);
    } else if (addr == R_DMA_INT_CLR) {
        s->regs[R_DMA_INT_RAW / 4] &= ~value;
        esp32s3_gp_spi_update_irq(s);
    } else if (addr == R_DMA_INT_ENA) {
        esp32s3_gp_spi_update_irq(s);
    }

    if (addr == R_CMD && (value & CMD_USR)) {
        esp32s3_gp_spi_execute(s);
        s->regs[R_DMA_INT_RAW / 4] |= BIT(12);
        esp32s3_gp_spi_update_irq(s);
    }
}

static const MemoryRegionOps esp32s3_gp_spi_ops = {
    .read = esp32s3_gp_spi_read,
    .write = esp32s3_gp_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void esp32s3_gp_spi_reset(DeviceState *dev)
{
    Esp32s3GpSpiState *s = ESP32S3_GP_SPI(dev);
    memset(s->regs, 0, sizeof(s->regs));
    s->x0=s->y0=s->px=s->py=0; s->x1=367; s->y1=447;
    s->pixels=0; s->display_on=false; s->lcd_cmd=0; s->madctl=0;
    memset(s->fb,0,sizeof(s->fb));
    qemu_irq_lower(s->irq);
}

static void esp32s3_gp_spi_realize(DeviceState *dev, Error **errp)
{
    Esp32s3GpSpiState *s = ESP32S3_GP_SPI(dev);

    if (s->peripheral == GDMA_SPI2) {
        s->con = graphic_console_init(dev, 0, &panel_ops, s);
        qemu_console_resize(s->con, 368, 448);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(dev), &esp32s3_gp_spi_ops, s,
                          TYPE_ESP32S3_GP_SPI, ESP32S3_GP_SPI_IO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void esp32s3_gp_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = esp32s3_gp_spi_realize;
    device_class_set_legacy_reset(dc, esp32s3_gp_spi_reset);
}

static const TypeInfo esp32s3_gp_spi_info = {
    .name = TYPE_ESP32S3_GP_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3GpSpiState),
    .class_init = esp32s3_gp_spi_class_init,
};

static void esp32s3_gp_spi_register_types(void)
{
    type_register_static(&esp32s3_gp_spi_info);
}

type_init(esp32s3_gp_spi_register_types);
