/* Board-specific SPI fallback derived from the analyzed kernel's pinctrl names.
 * Deliberately slow diagnostic transport; no guessed SPI controller binding.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>
#include <linux/dma-mapping.h>
#include <linux/atomic.h>
#include "mt_spi.h"
#include <mt-plat/mt_gpio.h>
#include "st7796s_spi.h"

#define PIN(n) ((n) | 0x80000000UL)
#define WIDTH 240
#define HEIGHT 320
static DEFINE_MUTEX(panel_lock);
static atomic_t selected = ATOMIC_INIT(0);
static bool prepared, enabled, initialized;
static unsigned long frames;
static u32 last_checksum;
static bool have_frame;
static u16 *shadow;
static unsigned char *tx_pixels;
static dma_addr_t tx_dma;
static bool tx_dma_coherent;
static bool shadow_valid;
static struct spi_device *hw_spi;
static bool hw_attempted;
static void refresh(struct work_struct *work);
static DECLARE_DELAYED_WORK(refresh_work, refresh);

static void out(unsigned int pin, unsigned int value)
{
	mt_set_gpio_out(PIN(pin), value);
}

void st7796s_spi_lock(void) { mutex_lock(&panel_lock); }
void st7796s_spi_unlock(void) { mutex_unlock(&panel_lock); }
void st7796s_spi_select(void)
{
	atomic_set(&selected, 1);
	pr_info("st7796s-spi: LCM selected, hardware SPI probe pending\n");
}

static void try_hw_spi(void)
{
	struct spi_master *master;
	struct spi_board_info info = { };
	struct mt_chip_conf *conf;

	if (hw_attempted)
		return;
	hw_attempted = true;
	master = spi_busnum_to_master(0);
	if (!master) {
		pr_warn("st7796s-spi: SPI0 unavailable, GPIO fallback\n");
		return;
	}
	strlcpy(info.modalias, "st7796s-panel", sizeof(info.modalias));
	/* MT6735 SPI source is 134.3 MHz; 1+1 ticks gives ~67 MHz.
	 * 153600 bytes then take ~18.3 ms, leaving headroom for 50 Hz. */
	info.max_speed_hz = 67000000;
	info.bus_num = 0;
	info.chip_select = 0;
	info.mode = SPI_MODE_3;
	conf = kzalloc(sizeof(*conf), GFP_KERNEL);
	if (!conf)
		return;
	conf->setuptime = 1; conf->holdtime = 1;
	conf->high_time = 1; conf->low_time = 1;
	conf->cs_idletime = 2; conf->sample_sel = POSEDGE;
	conf->cs_pol = ACTIVE_LOW; conf->cpol = SPI_CPOL_1;
	conf->cpha = SPI_CPHA_1; conf->tx_mlsb = SPI_MSB;
	conf->rx_mlsb = SPI_MSB; conf->tx_endian = SPI_LENDIAN;
	conf->rx_endian = SPI_LENDIAN; conf->com_mod = DMA_TRANSFER;
	conf->pause = PAUSE_MODE_DISABLE; conf->finish_intr = FINISH_INTR_EN;
	conf->deassert = DEASSERT_DISABLE; conf->ulthigh = ULTRA_HIGH_DISABLE;
	conf->tckdly = TICK_DLY0;
	info.controller_data = conf;
	hw_spi = spi_new_device(master, &info);
	if (hw_spi)
		hw_spi->bits_per_word = 8;
	if (hw_spi) {
		int setup_ret = spi_setup(hw_spi);
		if (setup_ret) {
			pr_warn("st7796s-spi: SPI0 setup failed %d\n", setup_ret);
			spi_unregister_device(hw_spi);
			hw_spi = NULL;
		}
	}
	if (!hw_spi) {
		kfree(conf);
		pr_warn("st7796s-spi: SPI0 device create failed, GPIO fallback\n");
		return;
	}
	mt_set_gpio_mode(PIN(65), 1);
	mt_set_gpio_mode(PIN(66), 1);
	mt_set_gpio_mode(PIN(68), 1);
	mt_set_gpio_mode(PIN(67), 0);
	pr_info("st7796s-spi: MTK SPI0 DMA enabled requested=%uHz timing=1/1 frame=153600 bytes\n",
		info.max_speed_hz);
}

void st7796s_spi_prepare(void)
{
	static const unsigned int pins[] = {65, 66, 67, 68, 80, 146};
	unsigned int i;
	int ret;

	if (prepared)
		return;
	try_hw_spi();
	for (i = 0; i < ARRAY_SIZE(pins); i++) {
		/* Hardware SPI owns CS/CLK/MOSI. DC and reset remain GPIOs. */
		if (hw_spi && (pins[i] == 65 || pins[i] == 66 || pins[i] == 68))
			continue;
		ret = mt_set_gpio_mode(PIN(pins[i]), 0);
		ret |= mt_set_gpio_dir(PIN(pins[i]), 1);
		if (ret)
			pr_err("st7796s-spi: GPIO%u setup failed %d\n", pins[i], ret);
	}
	if (hw_spi) {
		mt_set_gpio_dir(PIN(67), 1);
		out(67, 1);
	} else {
		out(65, 1);
		out(66, 0);
		out(67, 1);
		out(68, 0);
	}
	/* Backlight is controlled by the existing PWM path; don't toggle GPIO80
	 * here because it may be shared with that path. */
	prepared = true;
	pr_info("st7796s-spi: transport=%s CS65 CLK66 DC67 MOSI68 RST146\n",
		hw_spi ? "MTK-SPI-DMA" : "GPIO-bitbang");
}

void st7796s_spi_reset(unsigned int value) { out(146, value); }

static void byte(unsigned char value)
{
	unsigned int bit;

	/* Original software writer: MSB first, CLK low then high. */
	for (bit = 0; bit < 8; bit++) {
		out(68, !!(value & 0x80));
		out(66, 0);
		out(66, 1);
		value <<= 1;
	}
}

void st7796s_spi_write(unsigned int cmd, unsigned int count,
			const unsigned char *data)
{
	unsigned int i;
	int ret;

	if (hw_spi) {
		{
			u8 command = (u8)cmd;
			out(67, 0);
			ret = spi_write(hw_spi, &command, 1);
		}
		out(67, 1);
		if (!ret && count)
			ret = spi_write(hw_spi, data, count);
		if (ret)
			pr_warn_ratelimited("st7796s-spi: command transfer failed %d\n", ret);
		return;
	}
	out(65, 0); out(67, 0); byte(cmd); out(67, 1);
	for (i = 0; i < count; i++) byte(data[i]);
	out(65, 1);
}

void st7796s_spi_pixels(const unsigned char *data, unsigned int count)
{
	int ret;

	if (hw_spi) {
		if (count != WIDTH * HEIGHT * 2 || (count & 1023)) {
			pr_warn_ratelimited("st7796s-spi: reject non-DMA-sized frame %u\n", count);
			return;
		}
		ret = spi_write(hw_spi, data, count);
		if (ret)
			pr_warn_ratelimited("st7796s-spi: pixel DMA failed %d\n", ret);
		return;
	}
	while (count--)
		byte(*data++);
}

void st7796s_spi_pause(void)
{
	enabled = false;
	/* Do not cancel_work_sync under panel_lock: worker takes same lock. */
}

void st7796s_spi_enable(void)
{
	initialized = true;
	enabled = true;
	pr_info("st7796s-spi: panel init complete, refresh enabled\n");
	schedule_delayed_work(&refresh_work, msecs_to_jiffies(20));
}

static unsigned int component(u32 value, struct fb_bitfield field)
{
	u32 mask;

	if (!field.length || field.length > 16 || field.offset >= 32)
		return 0;
	mask = (1U << field.length) - 1;
	return ((value >> field.offset) & mask) * 255 / mask;
}

static void refresh(struct work_struct *work)
{
	struct fb_info *info;
	unsigned int x, y, bytes;
	unsigned int min_x = WIDTH, min_y = HEIGHT, max_x = 0, max_y = 0;
	unsigned int tx_len;
	u32 pixel;
	u16 rgb;
	u32 checksum = 0;
	u32 source_checksum = 0;
	unsigned long offset, limit, started = jiffies, delay;
	unsigned char window[4], rows[4];

	if (!atomic_read(&selected))
		goto again;
	/* Late LK handover may skip lcm_init. Initialize once fb0 exists. */
	if (!num_registered_fb || !(info = registered_fb[0]))
		goto again;
	/* Do not call lcm_init from this worker: lcm_init takes panel_lock and
	 * would recurse into the same mutex. Normal LCM selection initializes
	 * the panel before this worker is enabled. */
	if (!initialized)
		goto again;
	mutex_lock(&panel_lock);
	if (!enabled || !info->screen_base)
		goto unlock;
	bytes = info->var.bits_per_pixel / 8;
	limit = info->screen_size ? info->screen_size : info->fix.smem_len;
	if ((bytes != 2 && bytes != 3 && bytes != 4) ||
	    info->var.xres < WIDTH || info->var.yres < HEIGHT ||
	    !info->fix.line_length || !limit) {
		pr_warn_ratelimited("st7796s-spi: unsupported fb %ux%u bpp%u stride%u size%lu\n",
			info->var.xres, info->var.yres, info->var.bits_per_pixel,
			info->fix.line_length, limit);
		goto unlock;
	}
	offset = (info->var.yoffset + HEIGHT - 1UL) * info->fix.line_length +
		 (info->var.xoffset + WIDTH) * bytes;
	if (offset > limit) {
		pr_warn_ratelimited("st7796s-spi: fb bounds %lu > %lu\n", offset, limit);
		goto unlock;
	}
	if (!shadow)
		shadow = kzalloc(WIDTH * HEIGHT * sizeof(*shadow), GFP_KERNEL);
	if (!shadow)
		goto unlock;
	if (!tx_pixels) {
		if (hw_spi) {
			tx_pixels = dma_alloc_coherent(&hw_spi->dev,
				WIDTH * HEIGHT * 2, &tx_dma, GFP_KERNEL);
			tx_dma_coherent = !!tx_pixels;
		} else {
			tx_pixels = kmalloc(WIDTH * HEIGHT * 2, GFP_KERNEL);
		}
	}
	if (!tx_pixels)
		goto unlock;
	/* Convert once into a shadow buffer and find the smallest changed
	 * rectangle. Hardware DMA uses the original 153600-byte transaction;
	 * software fallback uses the smaller dirty rectangle. */
	for (y = 0; y < HEIGHT; y++) {
		const unsigned char __iomem *line = info->screen_base +
			(info->var.yoffset + y) * info->fix.line_length +
			info->var.xoffset * bytes;
		for (x = 0; x < WIDTH; x++) {
			if (bytes == 4 && info->var.red.offset == 16 &&
			    info->var.green.offset == 8 && info->var.blue.offset == 0) {
				const unsigned char __iomem *p = line + x * 4;
				rgb = (readb(p + 2) & 0xf8) << 8 |
				      (readb(p + 1) & 0xfc) << 3 |
				      (readb(p) >> 3);
			} else {
				pixel = readb(line + x * bytes) | readb(line + x * bytes + 1) << 8;
				if (bytes >= 3)
					pixel |= readb(line + x * bytes + 2) << 16;
				if (bytes == 4)
					pixel |= (u32)readb(line + x * bytes + 3) << 24;
				rgb = (component(pixel, info->var.red) >> 3) << 11 |
				      (component(pixel, info->var.green) >> 2) << 5 |
				      (component(pixel, info->var.blue) >> 3);
			}
			source_checksum = (source_checksum << 5) - source_checksum + rgb;
			if (!shadow_valid || shadow[y * WIDTH + x] != rgb) {
				shadow[y * WIDTH + x] = rgb;
				if (x < min_x) min_x = x;
				if (x > max_x) max_x = x;
				if (y < min_y) min_y = y;
				if (y > max_y) max_y = y;
			}
		}
	}
	shadow_valid = true;
	last_checksum = source_checksum;
	have_frame = true;
	if (min_x == WIDTH)
		goto unlock;
	/* MT6735 DMA requires every transfer length to be a multiple of
	 * PACKET_SIZE=1024. The original path always sends 150*1024 bytes.
	 * Arbitrary dirty rectangles are therefore unsafe on hardware SPI;
	 * use the proven full-frame transaction there. */
	if (hw_spi) {
		min_x = 0; max_x = WIDTH - 1;
		min_y = 0; max_y = HEIGHT - 1;
	}
	window[0] = min_x >> 8; window[1] = min_x;
	window[2] = max_x >> 8; window[3] = max_x;
	rows[0] = min_y >> 8; rows[1] = min_y;
	rows[2] = max_y >> 8; rows[3] = max_y;
	st7796s_spi_write(0x2a, sizeof(window), window);
	st7796s_spi_write(0x2b, sizeof(rows), rows);
	if (hw_spi) {
		unsigned char gram = 0x2c;
		out(67, 0);
		if (spi_write(hw_spi, &gram, 1))
			pr_warn_ratelimited("st7796s-spi: GRAM command failed\n");
		out(67, 1);
	} else {
		out(65, 0);
		out(67, 0);
		byte(0x2c);
		out(67, 1);
	}
	tx_len = 0;
	for (y = min_y; y <= max_y; y++) {
		for (x = min_x; x <= max_x; x++) {
			rgb = shadow[y * WIDTH + x];
			checksum = (checksum << 5) - checksum + rgb;
			tx_pixels[tx_len++] = rgb >> 8;
			tx_pixels[tx_len++] = rgb;
		}
		cond_resched();
	}
	st7796s_spi_pixels(tx_pixels, tx_len);
	if (!hw_spi)
		out(65, 1);
	if (!frames++)
		pr_info("st7796s-spi: first frame sent fb%ux%u bpp%u stride%u offset%u,%u rect=%u,%u-%u,%u transfer_ms=%u\n",
			info->var.xres, info->var.yres, info->var.bits_per_pixel,
			info->fix.line_length, info->var.xoffset, info->var.yoffset,
			min_x, min_y, max_x, max_y,
			jiffies_to_msecs(jiffies - started));
	else
		pr_info_ratelimited("st7796s-spi: frames=%lu checksum=%08x source=%08x transfer_ms=%u\n",
			frames, checksum, source_checksum,
			jiffies_to_msecs(jiffies - started));
unlock:
	mutex_unlock(&panel_lock);
again:
	/* 50-Hz cadence is measured from the beginning of this refresh, not
	 * 20 ms after the transfer. Otherwise an 18-ms DMA transfer caps us
	 * near 26 Hz. */
	delay = msecs_to_jiffies(20);
	if (time_after(started + delay, jiffies))
		delay = started + delay - jiffies;
	else
		delay = 1;
	schedule_delayed_work(&refresh_work, delay);
}

static int __init st7796s_spi_start(void)
{
	/* set_util_funcs is called for the selected LCM, not every registered LCM. */
	schedule_delayed_work(&refresh_work, msecs_to_jiffies(1000));
	return 0;
}
late_initcall(st7796s_spi_start);
