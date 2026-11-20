/*
 * ST7796S 240x320 panel reconstructed from kernel SHA256
 * d0f193f83eef10aa7e7827e3dcec6054b9606245de082eb4066c2467efae66f7.
 * Despite its name, get_params selects sync-pulse video mode.
 * File offsets: descriptor e7fc40, params 3f73c8, init 3f6f58.
 * Linux panel commands/pixels use the board GPIO SPI diagnostic transport.
 * See ST7796S_RECONSTRUCTION.md for transport/board limitations.
 */
#ifndef BUILD_LK
#include <linux/string.h>
#include "st7796s_spi.h"
#endif

#include "lcm_drv.h"

#define FRAME_WIDTH  (240)
#define FRAME_HEIGHT (320)

#define REGFLAG_DELAY          0xFE
#define REGFLAG_END_OF_TABLE   0xFF

static LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v) 	(lcm_util.set_reset_pin((v)))
#define MDELAY(n) 	(lcm_util.mdelay((n)))
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) 	lcm_util.dsi_set_cmdq_V2((cmd), (count), (ppara), (force_update))

struct LCM_setting_table {
	unsigned cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting[] = {
	{0x11, 0, { }},
	{REGFLAG_DELAY, 200, { }},
	{0x36, 1, {0x00}},
	{0x3A, 1, {0x05}},
	{0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},
	{0xB7, 1, {0x35}},
	{0xBB, 1, {0x29}},
	{0xC2, 1, {0x01}},
	{0xC3, 1, {0x19}},
	{0xC4, 1, {0x20}},
	{0xC5, 1, {0x1A}},
	{0xC6, 1, {0x14}},
	{0xD0, 2, {0xA4, 0xA1}},
	{0x35, 1, {0x00}},
	{0x44, 2, {0x00, 0x10}},
	{0xE0, 14, {0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31,
			 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34}},
	{0xE1, 14, {0xD0, 0x08, 0x0E, 0x09, 0x09, 0x15, 0x31,
			 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34}},
	{0xE7, 1, {0x10}},
	{0x21, 0, { }},
	{0x29, 0, { }},
	{REGFLAG_END_OF_TABLE, 0, { }}
};

static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, { }},
	{REGFLAG_DELAY, 20, { }},
	{0x10, 0, { }},
	/* GPIO146 is cleared explicitly by lcm_suspend after sleep-in. */
	{REGFLAG_END_OF_TABLE, 0, { }}
};

static struct LCM_setting_table lcm_resume_setting[] = {
	{0x29, 0, { }},
	{REGFLAG_DELAY, 50, { }},
	{REGFLAG_END_OF_TABLE, 0, { }}
};

static void push_table(struct LCM_setting_table *table, unsigned int count,
		       unsigned char force_update)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		switch (table[i].cmd) {
		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			return;
		default:
#ifndef BUILD_LK
			st7796s_spi_write(table[i].cmd, table[i].count,
					   table[i].para_list);
#else
			if (lcm_util.send_cmd && lcm_util.send_data) {
				unsigned int j;

				lcm_util.send_cmd(table[i].cmd);
				for (j = 0; j < table[i].count; j++)
					lcm_util.send_data(table[i].para_list[j]);
			} else {
				dsi_set_cmdq_V2(table[i].cmd, table[i].count,
						table[i].para_list, force_update);
			}
#endif
			break;
		}
	}
}

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
#ifndef BUILD_LK
	st7796s_spi_select();
#endif
}

static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	params->dsi.LANE_NUM = LCM_ONE_LANE;
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	/* Original zero-initialized format/PS are RGB565, not RGB888. */
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB565;
	params->dsi.intermediat_buffer_num = 2;
	params->dsi.PS = LCM_PACKED_PS_16BIT_RGB565;
	/* Preserve the original 720 even though RGB565 would normally use 480. */
	params->dsi.word_count = 720;
	params->lcm_if = LCM_INTERFACE_DSI0;
	params->dsi.packet_size = 256;
	params->dsi.vertical_sync_active = 8;
	params->dsi.vertical_backporch = 12;
	params->dsi.vertical_frontporch = 10;
	params->dsi.vertical_frontporch_for_low_power = 0;
	params->dsi.vertical_active_line = FRAME_HEIGHT;
	params->dsi.horizontal_sync_active = 10;
	params->dsi.horizontal_backporch = 51;
	params->dsi.horizontal_frontporch = 40;
	params->dsi.horizontal_blanking_pixel = 0;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.horizontal_bllp = 0;
	params->dsi.PLL_CLOCK = 90;
}

#ifndef BUILD_LK
void st7796s_panel_init_locked(void)
{
	st7796s_spi_prepare();
	st7796s_spi_reset(1);
	MDELAY(1);
	st7796s_spi_reset(0);
	MDELAY(50);
	st7796s_spi_reset(1);
	MDELAY(120);
	push_table(lcm_initialization_setting,
		   sizeof(lcm_initialization_setting) /
		   sizeof(struct LCM_setting_table), 1);
	st7796s_spi_enable();
}
#endif

static void lcm_init(void)
{
#ifndef BUILD_LK
	st7796s_spi_lock();
	st7796s_panel_init_locked();
	st7796s_spi_unlock();
#else
	SET_RESET_PIN(1);
	MDELAY(1);
	SET_RESET_PIN(0);
	MDELAY(50);
	SET_RESET_PIN(1);
	MDELAY(120);
	push_table(lcm_initialization_setting,
		   sizeof(lcm_initialization_setting) /
		   sizeof(struct LCM_setting_table), 1);
#endif
}

static void lcm_suspend(void)
{
#ifndef BUILD_LK
	st7796s_spi_lock();
	st7796s_spi_prepare();
	st7796s_spi_pause();
#endif
	push_table(lcm_suspend_setting,
		   sizeof(lcm_suspend_setting) /
		   sizeof(struct LCM_setting_table), 1);
#ifndef BUILD_LK
	st7796s_spi_reset(0);
#else
	if (lcm_util.set_gpio_out)
		lcm_util.set_gpio_out(146U | 0x80000000U, 0);
#endif
	MDELAY(100);
#ifndef BUILD_LK
	st7796s_spi_unlock();
#endif
}

static void lcm_resume(void)
{
	lcm_init();
#ifndef BUILD_LK
	st7796s_spi_lock();
#endif
	MDELAY(120);
	push_table(lcm_resume_setting,
		   sizeof(lcm_resume_setting) /
		   sizeof(struct LCM_setting_table), 1);
#ifndef BUILD_LK
	st7796s_spi_unlock();
#endif
}

static unsigned int lcm_compare_id(void)
{
	/* Original callback unconditionally returns 1; LK name is authoritative. */
	return 1;
}

LCM_DRIVER st7796s_hvga_dsi_cmd_lcm_drv = {
	.name = "st7796s_hvga_dsi_cmd",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.compare_id = lcm_compare_id,
};
