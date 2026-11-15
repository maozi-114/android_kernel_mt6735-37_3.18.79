/*
 * Reconstructed from ./kernel only.
 * LCM_DRIVER: 0x40E5B920; init: 0x4041342C / table 0x40E5ADE0;
 * suspend: 0x404133C0 / table 0x40E5AD08; get_params: 0x40413258.
 */
#include "lcm_drv.h"

#define FRAME_WIDTH  480
#define FRAME_HEIGHT 854
#define REGFLAG_DELAY        0xFFC
#define REGFLAG_END_OF_TABLE 0xFFD

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static LCM_UTIL_FUNCS lcm_util;
#define SET_RESET_PIN(v) (lcm_util.set_reset_pin((v)))
#define MDELAY(n)        (lcm_util.mdelay((n)))
#define dsi_set_cmdq_V2(c, n, p, f) lcm_util.dsi_set_cmdq_V2((c), (n), (p), (f))

static const struct LCM_setting_table lcm_initialization_setting[] = {
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}},
	{0xEF, 1, {0x08}},
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}},
	{0xC0, 2, {0xE9, 0x03}},
	{0xC1, 2, {0x10, 0x0C}},
	{0xC2, 2, {0x07, 0x0A}},
	{0xCC, 1, {0x10}},
	{0xB0, 16, {0x07, 0x14, 0x9C, 0x0B, 0x10, 0x06, 0x08, 0x09, 0x08, 0x22, 0x02, 0x4F, 0x0E, 0x66, 0x2D, 0x1C}},
	{0xB1, 16, {0x09, 0x17, 0x9E, 0x0F, 0x11, 0x06, 0x0C, 0x08, 0x08, 0x26, 0x04, 0x51, 0x10, 0x6A, 0x33, 0x1D}},
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}},
	{0xB0, 1, {0x30}},
	{0xB1, 1, {0x57}},
	{0xB2, 1, {0x84}},
	{0xB3, 1, {0x80}},
	{0xB5, 1, {0x4E}},
	{0xB7, 1, {0x85}},
	{0xB8, 1, {0x20}},
	{0xC1, 1, {0x78}},
	{0xC2, 1, {0x78}},
	{0xD0, 1, {0x88}},
	{0xE0, 3, {0x00, 0x00, 0x02}},
	{0xE1, 11, {0x06, 0xA0, 0x08, 0xA0, 0x05, 0xA0, 0x07, 0xA0, 0x00, 0x44, 0x44}},
	{0xE2, 12, {0x30, 0x30, 0x44, 0x44, 0x6E, 0xA0, 0x00, 0x00, 0x6E, 0xA0, 0x00, 0x00}},
	{0xE3, 4, {0x00, 0x00, 0x33, 0x33}},
	{0xE4, 2, {0x44, 0x44}},
	{0xE5, 16, {0x0D, 0x69, 0x0A, 0xA0, 0x0F, 0x6B, 0x0A, 0xA0, 0x09, 0x65, 0x0A, 0xA0, 0x0B, 0x67, 0x0A, 0xA0}},
	{0xE6, 4, {0x00, 0x00, 0x33, 0x33}},
	{0xE7, 2, {0x44, 0x44}},
	{0xE8, 16, {0x0C, 0x68, 0x0A, 0xA0, 0x0E, 0x6A, 0x0A, 0xA0, 0x08, 0x64, 0x0A, 0xA0, 0x0A, 0x66, 0x0A, 0xA0}},
	{0xE9, 2, {0x36, 0x00}},
	{0xEB, 7, {0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40}},
	{0xED, 16, {0xFF, 0x45, 0x67, 0xFA, 0x01, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x10, 0xAF, 0x76, 0x54, 0xFF}},
	{0xEF, 6, {0x10, 0x0D, 0x04, 0x0A, 0x3F, 0x1F}},
	{0xB9, 1, {0x80}},
	{0x35, 1, {0x00}},
	{0x11, 1, {0x00}},
	{REGFLAG_DELAY, 120, {0}},
	{0x29, 1, {0x00}},
	{REGFLAG_DELAY, 20, {0}},
	{REGFLAG_END_OF_TABLE, 0, {0}},
};

static const struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {0}},
	{0x10, 0, {0}},
	{REGFLAG_END_OF_TABLE, 0, {0}},
};

static void push_table(const struct LCM_setting_table *table,
	unsigned int count, unsigned char force_update)
{
	unsigned int i;
	for (i = 0; i < count; ++i) {
		switch (table[i].cmd) {
		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			return;
		default:
			dsi_set_cmdq_V2(table[i].cmd, table[i].count,
				(unsigned char *)table[i].para_list, force_update);
			break;
		}
	}
}

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(lcm_util));
}

static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, 1024);
	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	/* Direct assignments observed in the target LCM_PARAMS ABI. */
	params->dsi.mode = BURST_VDO_MODE;       /* raw value: 3 */
	params->dsi.LANE_NUM = LCM_TWO_LANE;
	params->dsi.packet_size = 256;
	params->dsi.vertical_sync_active = 10;
	params->dsi.vertical_backporch = 40;
	params->dsi.vertical_frontporch = 40;
	params->dsi.vertical_active_line = FRAME_HEIGHT;
	params->dsi.horizontal_sync_active = 10;
	params->dsi.horizontal_backporch = 40;
	params->dsi.horizontal_frontporch = 40;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.PLL_CLOCK = 185;

	/* Unnamed target-ABI assignments: +0x1E0=2, +0x1F0=2, +0x1F4=2,
	 * +0x1F8=2, +0x200=4, +0x32C=1, +0x328=1, +0x34C=10, +0x34D=0x9C. */
}

static void lcm_init(void)
{
	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(6);
	SET_RESET_PIN(1);
	MDELAY(50);
	push_table(lcm_initialization_setting,
		ARRAY_SIZE(lcm_initialization_setting), 1);
}

static void lcm_suspend(void)
{
	SET_RESET_PIN(1);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(60);
	push_table(lcm_suspend_setting, ARRAY_SIZE(lcm_suspend_setting), 1);
}

static void lcm_resume(void)
{
	lcm_init();
}

LCM_DRIVER st7701s_fw_dsi_vdo_boe_lcm_drv = {
	.name = "st7701s_fw_dsi_vdo_boe",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
};
