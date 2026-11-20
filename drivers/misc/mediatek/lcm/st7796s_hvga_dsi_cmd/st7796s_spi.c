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
#include <linux/ktime.h>
#include <linux/moduleparam.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include "mt_spi.h"
#include <mt-plat/mt_gpio.h>
#include "st7796s_spi.h"

#define PIN(n) ((n) | 0x80000000UL)
#define WIDTH 240
#define HEIGHT 320
#define CONTROL_DATA_SIZE 64
#define DMA_BAND_ROWS 32
#define DMA_BANDS (HEIGHT / DMA_BAND_ROWS)
static DEFINE_MUTEX(panel_lock);
static bool partial_refresh = true;
module_param(partial_refresh, bool, 0600);
static bool skip_clean_bands = true;
module_param(skip_clean_bands, bool, 0600);
MODULE_PARM_DESC(skip_clean_bands, "Skip unchanged SPI DMA bands inside the dirty window");
static void refresh(struct work_struct *work);
static DECLARE_DELAYED_WORK(refresh_work, refresh);
#ifdef CONFIG_MTK_YUXIN_SPI_COMPOSITION
static DEFINE_MUTEX(composed_lock);
static u16 composed_pixels[WIDTH * HEIGHT];
static u8 composed_row[WIDTH * 4] __aligned(8);
static bool composed_valid;
static ktime_t composed_ready;
static s64 composed_gap_us;
static s64 composed_convert_us;
static unsigned long composed_seq;

static void st7796s_spi_submit(const void *frame, unsigned int pitch,
			      unsigned int step, unsigned int red)
{
	unsigned int x, y;
	const u8 *src = frame;
	ktime_t start = ktime_get(), now;

	mutex_lock(&composed_lock);
	for (y = 0; y < HEIGHT; y++) {
		const u8 *p = composed_row;

		memcpy(composed_row, src + y * pitch, WIDTH * step);
		for (x = 0; x < WIDTH; x++, p += step)
			composed_pixels[y * WIDTH + x] =
				((p[red] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[2 - red] >> 3);
	}
	if (!composed_valid)
		pr_info("st7796s-spi: received first WDMA composition\n");
	now = ktime_get();
	composed_gap_us = composed_valid ? ktime_to_us(ktime_sub(now, composed_ready)) : 0;
	composed_convert_us = ktime_to_us(ktime_sub(now, start));
	composed_ready = now;
	composed_seq++;
	composed_valid = true;
	mutex_unlock(&composed_lock);
	mod_delayed_work(system_highpri_wq, &refresh_work, 0);
}

void st7796s_spi_submit_rgb888(const void *frame, unsigned int pitch)
{
	st7796s_spi_submit(frame, pitch, 3, 0);
}

void st7796s_spi_submit_x8888(const void *frame, unsigned int pitch, unsigned int red)
{
	st7796s_spi_submit(frame, pitch, 4, red);
}
#endif
static atomic_t selected = ATOMIC_INIT(0);
static bool prepared, enabled, paused;
static unsigned long frames;
static u32 last_checksum;
static bool have_frame;
static u16 *shadow;
static unsigned char *tx_pixels;
static dma_addr_t tx_dma;
static bool shadow_valid;
static struct spi_device *hw_spi;
static unsigned char *control_cmd;
static unsigned char *control_data;
static dma_addr_t control_cmd_dma;
static dma_addr_t control_data_dma;
static bool hw_attempted;
#ifdef CONFIG_DEBUG_FS
static size_t last_full_tx;

static int st7796s_tx_open(struct inode *inode, struct file *file)
{
	void *snapshot = vmalloc(WIDTH * HEIGHT * 2);

	if (!snapshot)
		return -ENOMEM;
	mutex_lock(&panel_lock);
	if (!last_full_tx) {
		mutex_unlock(&panel_lock);
		vfree(snapshot);
		return -ENODATA;
	}
	memcpy(snapshot, tx_pixels, last_full_tx);
	mutex_unlock(&panel_lock);
	file->private_data = snapshot;
	return 0;
}

static ssize_t st7796s_tx_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
	return simple_read_from_buffer(buf, count, pos, file->private_data,
				       WIDTH * HEIGHT * 2);
}

static int st7796s_tx_release(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
	return 0;
}

static const struct file_operations st7796s_tx_fops = {
	.open = st7796s_tx_open,
	.read = st7796s_tx_read,
	.release = st7796s_tx_release,
	.llseek = default_llseek,
};
#endif


static int panel_dma_write(const void *data, dma_addr_t dma, unsigned int len)
{
	struct spi_transfer xfer = {
		.tx_buf = data,
		.tx_dma = dma,
		.len = len,
	};
	struct spi_message msg;

	spi_message_init(&msg);
	msg.is_dma_mapped = 1;
	spi_message_add_tail(&xfer, &msg);
	dma_wmb();
	return spi_sync(hw_spi, &msg);
}

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
	struct device *dma_dev;
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
	info.max_speed_hz = 33575000;
	info.bus_num = 0;
	info.chip_select = 0;
	info.mode = SPI_MODE_3;
	conf = kzalloc(sizeof(*conf), GFP_KERNEL);
	if (!conf)
		return;
	conf->setuptime = 4; conf->holdtime = 4;
	conf->high_time = 2; conf->low_time = 2;
	conf->cs_idletime = 8; conf->sample_sel = POSEDGE;
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
	/* DMA is also selected for the MTK controller's small transfers. Never
	 * give it stack or kmalloc addresses: the old driver requires 4-byte
	 * aligned DMA buffers, and the command path otherwise produces the
	 * unaligned Tx_DMA warnings seen on the target. */
	dma_dev = hw_spi->master->dev.parent;
	control_cmd = dma_alloc_coherent(dma_dev, 4, &control_cmd_dma, GFP_KERNEL);
	control_data = dma_alloc_coherent(dma_dev, CONTROL_DATA_SIZE,
					  &control_data_dma, GFP_KERNEL);
	if (!control_cmd || !control_data) {
		pr_warn("st7796s-spi: control DMA buffer unavailable, GPIO fallback\n");
		if (control_cmd)
			dma_free_coherent(dma_dev, 4, control_cmd, control_cmd_dma);
		if (control_data)
			dma_free_coherent(dma_dev, CONTROL_DATA_SIZE, control_data, control_data_dma);
		control_cmd = NULL;
		control_data = NULL;
		spi_unregister_device(hw_spi);
		hw_spi = NULL;
		kfree(conf);
		return;
	}
	mt_set_gpio_mode(PIN(65), 1);
	mt_set_gpio_mode(PIN(66), 1);
	mt_set_gpio_mode(PIN(68), 1);
	mt_set_gpio_mode(PIN(67), 0);
	pr_info("st7796s-spi: MTK SPI0 DMA enabled requested=%uHz timing=2/2 frame=153600 bytes\n",
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

int st7796s_spi_write(unsigned int cmd, unsigned int count,
			const unsigned char *data)
{
	unsigned int i;
	int ret;

	if (hw_spi) {
		if (count > CONTROL_DATA_SIZE)
			return -EINVAL;
		control_cmd[0] = (u8)cmd;
		if (count)
			memcpy(control_data, data, count);
		out(67, 0);
		ret = panel_dma_write(control_cmd, control_cmd_dma, 1);
		out(67, 1);
		if (!ret && count)
			ret = panel_dma_write(control_data, control_data_dma, count);
		if (ret)
			pr_warn_ratelimited("st7796s-spi: command transfer failed %d\n", ret);
		return ret;
	}
	out(65, 0); out(67, 0); byte(cmd); out(67, 1);
	for (i = 0; i < count; i++) byte(data[i]);
	out(65, 1);
	return 0;
}

static int st7796s_spi_pixels(const unsigned char *data, unsigned int count)
{
	int ret;

	if (hw_spi) {
		if (!count || count > WIDTH * HEIGHT * 2 || (count & 1023)) {
			pr_warn_ratelimited("st7796s-spi: reject non-DMA-sized frame %u\n", count);
			return -EINVAL;
		}
		ret = panel_dma_write(data, tx_dma, count);
		if (ret)
			pr_warn_ratelimited("st7796s-spi: pixel DMA failed %d\n", ret);
		return ret;
	}
	while (count--)
		byte(*data++);
	return 0;
}

void st7796s_spi_pause(void)
{
	paused = true;
	enabled = false;
	/* Do not cancel_work_sync under panel_lock: worker takes same lock. */
}

void st7796s_spi_enable(void)
{
	shadow_valid = false;
	paused = false;
	enabled = true;
	pr_info("st7796s-spi: panel init complete, refresh enabled\n");
	mod_delayed_work(system_highpri_wq, &refresh_work, 0);
}

static unsigned int component(u32 value, struct fb_bitfield field)
{
	u32 mask;

	if (!field.length || field.length > 16 || field.offset >= 32)
		return 0;
	mask = (1U << field.length) - 1;
	return ((value >> field.offset) & mask) * 255 / mask;
}

static int push_window(unsigned int x0, unsigned int y0,
		       unsigned int x1, unsigned int y1,
		       unsigned int *bytes, s64 *dma_us, ktime_t ready,
		       s64 *ready_to_dma_us)
{
	unsigned char window[] = { x0 >> 8, x0, x1 >> 8, x1 };
	unsigned char rows[] = { y0 >> 8, y0, y1 >> 8, y1 };
	unsigned int x, y, len = 0;
	ktime_t start;
	u16 rgb;
	int ret;

	ret = st7796s_spi_write(0x2a, sizeof(window), window);
	if (!ret)
		ret = st7796s_spi_write(0x2b, sizeof(rows), rows);
	if (ret)
		return ret;
	if (hw_spi) {
		ret = st7796s_spi_write(0x2c, 0, NULL);
		if (ret)
			return ret;
	} else {
		out(65, 0);
		out(67, 0);
		byte(0x2c);
		out(67, 1);
	}
#ifdef CONFIG_DEBUG_FS
	last_full_tx = 0;
#endif
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) {
			rgb = shadow[y * WIDTH + x];
			tx_pixels[len++] = rgb >> 8;
			tx_pixels[len++] = rgb;
		}
	}
	start = ktime_get();
	if (!*bytes && ktime_to_ns(ready))
		*ready_to_dma_us = ktime_to_us(ktime_sub(start, ready));
	ret = st7796s_spi_pixels(tx_pixels, len);
	*dma_us += ktime_to_us(ktime_sub(ktime_get(), start));
	if (!hw_spi)
		out(65, 1);
	if (ret)
		return ret;
#ifdef CONFIG_DEBUG_FS
	if (hw_spi && len == WIDTH * HEIGHT * 2)
		last_full_tx = len;
#endif
	*bytes += len;
	return 0;
}

static void refresh(struct work_struct *work)
{
	struct fb_info *info;
	const u16 *composed = NULL;
	unsigned int x, y, bytes;
	unsigned int min_x = WIDTH, min_y = HEIGHT, max_x = 0, max_y = 0;
	unsigned int tx_len = 0, dirty_bands = 0;
	int ret;
	u32 pixel;
	u16 rgb;
	u32 source_checksum = 0;
	unsigned long offset, limit, started = jiffies, delay;
	ktime_t start_time = ktime_get();
	s64 dma_us = 0;
	s64 gap_us = 0, convert_us = 0, worker_us = 0, ready_to_dma_us = 0;
	ktime_t ready = ktime_set(0, 0);
	unsigned long seq = 0;
	bool poll = true, partial = READ_ONCE(partial_refresh);

	if (!atomic_read(&selected))
		goto again;
	if (!num_registered_fb || !(info = registered_fb[0]))
		goto again;
	mutex_lock(&panel_lock);
	if (paused || !info->screen_base)
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
	if (!enabled) {
		pr_info("st7796s-spi: taking over panel from LK\n");
		st7796s_panel_init_locked();
	}
	if (!shadow)
		shadow = kzalloc(WIDTH * HEIGHT * sizeof(*shadow), GFP_KERNEL);
	if (!shadow)
		goto unlock;
	if (!tx_pixels) {
		if (hw_spi) {
			tx_pixels = dma_alloc_coherent(hw_spi->master->dev.parent,
				WIDTH * HEIGHT * 2, &tx_dma, GFP_KERNEL);
		} else {
			tx_pixels = kmalloc(WIDTH * HEIGHT * 2, GFP_KERNEL);
		}
	}
	if (!tx_pixels)
		goto unlock;
#ifdef CONFIG_MTK_YUXIN_SPI_COMPOSITION
	mutex_lock(&composed_lock);
	if (composed_valid) {
		composed = composed_pixels;
		seq = composed_seq;
		ready = composed_ready;
		gap_us = composed_gap_us;
		convert_us = composed_convert_us;
		worker_us = max_t(s64, 0, ktime_to_us(ktime_sub(start_time, ready)));
		poll = false;
		if (shadow_valid && !memcmp(shadow, composed, sizeof(composed_pixels))) {
			mutex_unlock(&composed_lock);
			goto unlock;
		}
	} else {
		mutex_unlock(&composed_lock);
	}
#endif
	/* Find dirty pixels; SPI DMA sends full-width bands, while the GPIO
	 * fallback sends the bounding rectangle. */
	for (y = 0; y < HEIGHT; y++) {
		bool row_dirty = false;
		const unsigned char __iomem *line = info->screen_base +
			(info->var.yoffset + y) * info->fix.line_length +
			info->var.xoffset * bytes;
		for (x = 0; x < WIDTH; x++) {
			if (composed) {
				rgb = composed[y * WIDTH + x];
			} else if (bytes == 4 && info->var.red.offset == 16 &&
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
				row_dirty = true;
				if (x < min_x) min_x = x;
				if (x > max_x) max_x = x;
				if (y < min_y) min_y = y;
				if (y > max_y) max_y = y;
			}
		}
		if (row_dirty)
			dirty_bands |= BIT(y / DMA_BAND_ROWS);
	}
#ifdef CONFIG_MTK_YUXIN_SPI_COMPOSITION
	if (composed)
		mutex_unlock(&composed_lock);
#endif
	shadow_valid = true;
	last_checksum = source_checksum;
	have_frame = true;
	if (min_x == WIDTH)
		goto unlock;
	if (hw_spi) {
		min_x = 0;
		max_x = WIDTH - 1;
		if (partial) {
			min_y = round_down(min_y, DMA_BAND_ROWS);
			max_y = round_up(max_y + 1, DMA_BAND_ROWS) - 1;
		} else {
			min_y = 0;
			max_y = HEIGHT - 1;
		}
	}
	if (hw_spi && partial && READ_ONCE(skip_clean_bands)) {
		unsigned int first, end;

		/* Each band is 15360 bytes, divisible by the SPI DMA packet size.
		 * Merge adjacent dirty bands; only split across a full clean band. */
		for (first = 0; first < DMA_BANDS; first = end) {
			if (!(dirty_bands & BIT(first))) {
				end = first + 1;
				continue;
			}
			for (end = first + 1; end < DMA_BANDS &&
			     (dirty_bands & BIT(end)); end++)
				;
			ret = push_window(0, first * DMA_BAND_ROWS, WIDTH - 1,
					  end * DMA_BAND_ROWS - 1, &tx_len, &dma_us,
					  ready, &ready_to_dma_us);
			if (ret)
				goto retry;
		}
	} else {
		ret = push_window(min_x, min_y, max_x, max_y, &tx_len, &dma_us,
				  ready, &ready_to_dma_us);
		if (ret)
			goto retry;
	}
	frames++;
	pr_info_ratelimited("st7796s-spi: frames=%lu source=%08x bytes=%u dma_us=%lld frame_us=%lld seq=%lu gap_us=%lld convert_us=%lld worker_us=%lld ready_to_dma_us=%lld\n",
		frames, source_checksum, tx_len,
		(long long)dma_us,
		(long long)ktime_to_us(ktime_sub(ktime_get(), start_time)), seq,
		(long long)gap_us, (long long)convert_us, (long long)worker_us,
		(long long)ready_to_dma_us);
	goto unlock;
retry:
	shadow_valid = false;
	poll = true;
unlock:
	mutex_unlock(&panel_lock);
again:
	if (!poll)
		return;
	/* 50-Hz cadence is measured from the beginning of this refresh, not
	 * 20 ms after the transfer. Otherwise an 18-ms DMA transfer caps us
	 * near 26 Hz. */
	delay = msecs_to_jiffies(20);
	if (time_after(started + delay, jiffies))
		delay = started + delay - jiffies;
	else
		delay = 1;
	queue_delayed_work(system_highpri_wq, &refresh_work, delay);
}

static int __init st7796s_spi_start(void)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_create_file("st7796s_tx_rgb565", 0400, NULL, NULL, &st7796s_tx_fops);
#endif
	/* set_util_funcs is called for the selected LCM, not every registered LCM. */
	queue_delayed_work(system_highpri_wq, &refresh_work, msecs_to_jiffies(1000));
	return 0;
}
late_initcall(st7796s_spi_start);
