/*
 * nv3049f_28.c -- reconstructed only from this Image's nv3049f_28 binary.
 *
 * Evidence: LCM_DRIVER at Image VA 0x40E62A70; get_params 0x4041356C;
 * init table 0x40E5FB40 (183 entries); suspend table 0x40E5F9F0 (5 entries).
 * No values below were imported from another panel driver.
 */
#include <linux/string.h>
#include "lcm_drv.h"

#define FRAME_WIDTH   (480)
#define FRAME_HEIGHT  (640)
#define REGFLAG_DELAY        0xFC
#define REGFLAG_END_OF_TABLE 0xFD

struct LCM_setting_table {
	unsigned char cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static LCM_UTIL_FUNCS lcm_util;
#define SET_RESET_PIN(v)       (lcm_util.set_reset_pin((v)))
#define MDELAY(n)              (lcm_util.mdelay((n)))
#define dsi_set_cmdq_V2(c,n,p,f) lcm_util.dsi_set_cmdq_V2((c),(n),(p),(f))

static const struct LCM_setting_table lcm_initialization_setting[] = {
	{0xFF, 1, {0x30}},
	{0xFF, 1, {0x49}},
	{0xFF, 1, {0x01}},
	{0xE2, 1, {0x00}},
	{0x11, 1, {0x10}},
	{0x14, 1, {0x10}},
	{0x3B, 1, {0x00}},
	{0x41, 1, {0x32}},
	{0x45, 1, {0x02}},
	{0x46, 1, {0x7F}},
	{0x51, 1, {0x3C}},
	{0x52, 1, {0x01}},
	{0x53, 1, {0x22}},
	{0x54, 1, {0x80}},
	{0x55, 1, {0x80}},
	{0x56, 1, {0x10}},
	{0x57, 1, {0x37}},
	{0x59, 1, {0x00}},
	{0x5A, 1, {0x00}},
	{0x5B, 1, {0x82}},
	{0x5C, 1, {0x60}},
	{0x79, 1, {0xFE}},
	{0x7D, 1, {0x08}},
	{0x90, 1, {0x02}},
	{0x91, 1, {0x00}},
	{0x94, 1, {0x0E}},
	{0x96, 1, {0x06}},
	{0xC8, 1, {0x67}},
	{0xCA, 1, {0xB2}},
	{0xCC, 1, {0x5E}},
	{0xCE, 1, {0x03}},
	{0xA1, 1, {0x24}},
	{0xB5, 1, {0x26}},
	{0xA2, 1, {0x20}},
	{0xB6, 1, {0x24}},
	{0xA5, 1, {0x11}},
	{0xB9, 1, {0x15}},
	{0xA6, 1, {0x0F}},
	{0xBA, 1, {0x0B}},
	{0xA3, 1, {0x40}},
	{0xB7, 1, {0x40}},
	{0xA4, 1, {0x44}},
	{0xB8, 1, {0x44}},
	{0xA8, 1, {0x0E}},
	{0xBC, 1, {0x0F}},
	{0xA9, 1, {0x14}},
	{0xBD, 1, {0x10}},
	{0xAA, 1, {0x14}},
	{0xBE, 1, {0x10}},
	{0xAB, 1, {0x12}},
	{0xBF, 1, {0x12}},
	{0xAC, 1, {0x12}},
	{0xC0, 1, {0x10}},
	{0xAD, 1, {0x14}},
	{0xC1, 1, {0x10}},
	{0xAE, 1, {0x12}},
	{0xC2, 1, {0x0E}},
	{0xAF, 1, {0x10}},
	{0xC3, 1, {0x0E}},
	{0xB0, 1, {0x11}},
	{0xC4, 1, {0x0D}},
	{0xB1, 1, {0x0B}},
	{0xC5, 1, {0x05}},
	{0xA0, 1, {0x39}},
	{0xB4, 1, {0x39}},
	{0xA7, 1, {0x0A}},
	{0xBB, 1, {0x0A}},
	{0xFF, 1, {0x30}},
	{0xFF, 1, {0x49}},
	{0xFF, 1, {0x02}},
	{0x50, 1, {0x22}},
	{0x51, 1, {0x7F}},
	{0x52, 1, {0x7F}},
	{0x67, 1, {0x15}},
	{0x68, 1, {0xDF}},
	{0x69, 1, {0x9F}},
	{0xFF, 1, {0x30}},
	{0xFF, 1, {0x49}},
	{0xFF, 1, {0x03}},
	{0x00, 1, {0x22}},
	{0x01, 1, {0x80}},
	{0x02, 1, {0x82}},
	{0x0F, 1, {0x22}},
	{0x10, 1, {0x81}},
	{0x11, 1, {0x83}},
	{0x27, 1, {0x88}},
	{0x28, 1, {0x05}},
	{0x29, 1, {0x03}},
	{0x36, 1, {0x88}},
	{0x37, 1, {0x04}},
	{0x38, 1, {0x02}},
	{0x7B, 1, {0x30}},
	{0x80, 1, {0x83}},
	{0x81, 1, {0x32}},
	{0x82, 1, {0x79}},
	{0x83, 1, {0x05}},
	{0x84, 1, {0x90}},
	{0x85, 1, {0x90}},
	{0x88, 1, {0x82}},
	{0x89, 1, {0x32}},
	{0x8A, 1, {0x7A}},
	{0x8B, 1, {0x11}},
	{0x8C, 1, {0x90}},
	{0x8D, 1, {0x90}},
	{0x8E, 1, {0x81}},
	{0x8F, 1, {0x32}},
	{0x90, 1, {0x7B}},
	{0x91, 1, {0x11}},
	{0x92, 1, {0x90}},
	{0x93, 1, {0x90}},
	{0x94, 1, {0x80}},
	{0x95, 1, {0x32}},
	{0x96, 1, {0x7C}},
	{0x97, 1, {0x11}},
	{0x98, 1, {0x90}},
	{0x99, 1, {0x90}},
	{0x9A, 1, {0x01}},
	{0x9B, 1, {0x32}},
	{0x9C, 1, {0x7D}},
	{0x9D, 1, {0x05}},
	{0x9E, 1, {0x90}},
	{0x9F, 1, {0x90}},
	{0xA2, 1, {0x02}},
	{0xA3, 1, {0x32}},
	{0xA4, 1, {0x7E}},
	{0xA5, 1, {0x11}},
	{0xA6, 1, {0x90}},
	{0xA7, 1, {0x90}},
	{0xA8, 1, {0x03}},
	{0xA9, 1, {0x32}},
	{0xAA, 1, {0x7F}},
	{0xAB, 1, {0x11}},
	{0xAC, 1, {0x90}},
	{0xAD, 1, {0x90}},
	{0xAE, 1, {0x04}},
	{0xAF, 1, {0x32}},
	{0xB0, 1, {0x80}},
	{0xB1, 1, {0x11}},
	{0xB2, 1, {0x90}},
	{0xB3, 1, {0x90}},
	{0xF7, 1, {0x40}},
	{0xF8, 1, {0x40}},
	{0xD0, 1, {0x1F}},
	{0xD1, 1, {0x1F}},
	{0xD2, 1, {0x05}},
	{0xD3, 1, {0x0B}},
	{0xD4, 1, {0x0D}},
	{0xD5, 1, {0x0F}},
	{0xD6, 1, {0x11}},
	{0xD7, 1, {0x1F}},
	{0xD8, 1, {0x1B}},
	{0xD9, 1, {0x1A}},
	{0xDA, 1, {0x01}},
	{0xDB, 1, {0x1F}},
	{0xDC, 1, {0x1F}},
	{0xDD, 1, {0x1F}},
	{0xDE, 1, {0x1F}},
	{0xDF, 1, {0x1F}},
	{0xE0, 1, {0x1F}},
	{0xE1, 1, {0x1F}},
	{0xE2, 1, {0x1F}},
	{0xE3, 1, {0x1F}},
	{0xE4, 1, {0x1F}},
	{0xE5, 1, {0x00}},
	{0xE6, 1, {0x1A}},
	{0xE7, 1, {0x1B}},
	{0xE8, 1, {0x1F}},
	{0xE9, 1, {0x10}},
	{0xEA, 1, {0x0E}},
	{0xEB, 1, {0x0C}},
	{0xEC, 1, {0x0A}},
	{0xED, 1, {0x04}},
	{0xEE, 1, {0x1F}},
	{0xEF, 1, {0x1F}},
	{0xFF, 1, {0x30}},
	{0xFF, 1, {0x49}},
	{0xFF, 1, {0x00}},
	{0x35, 1, {0x00}},
	{0x11, 0, {}},
	{REGFLAG_DELAY, 120, {0}},
	{0x29, 0, {}},
	{REGFLAG_DELAY, 50, {0}},
	{REGFLAG_END_OF_TABLE, 0x00, {0}},
};

static const struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {}},
	{REGFLAG_DELAY, 50, {0}},
	{0x10, 0, {}},
	{REGFLAG_DELAY, 200, {0}},
	{REGFLAG_END_OF_TABLE, 0x00, {0}},
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
	memcpy(&lcm_util, util, sizeof(lcm_util)); /* binary copies exactly 0xD8 */
}

static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(*params));
	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	/* Directly verified core timing / transport assignments. */
	params->dsi.mode = SYNC_PULSE_VDO_MODE; /* raw mode value: 1 */
	params->dsi.LANE_NUM = LCM_TWO_LANE;    /* raw value: 2 */
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;
	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;
	params->dsi.packet_size = 256;
	params->dsi.vertical_sync_active = 4;
	params->dsi.vertical_backporch = 14;
	params->dsi.vertical_frontporch = 16;
	params->dsi.vertical_active_line = FRAME_HEIGHT;
	params->dsi.horizontal_sync_active = 4;
	params->dsi.horizontal_backporch = 44;
	params->dsi.horizontal_frontporch = 46;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.PLL_CLOCK = 150;

	/*
	 * Direct raw writes not yet named against the exact target lcm_drv.h ABI:
	 * params+0x2A0 = 1; params+0x348 = 1.
	 * They intentionally remain unset instead of mapping them to possibly
	 * incorrect members of a similar, but non-identical, header.
	 */
}

static void lcm_init(void)
{
	/* Exact reset sequence at binary function 0x404136DC. */
	SET_RESET_PIN(1); MDELAY(20);
	SET_RESET_PIN(0); MDELAY(50);
	SET_RESET_PIN(1); MDELAY(100);
	push_table(lcm_initialization_setting,
		ARRAY_SIZE(lcm_initialization_setting), 1);
}

static void lcm_suspend(void)
{
	push_table(lcm_suspend_setting, ARRAY_SIZE(lcm_suspend_setting), 1);
}

static void lcm_resume(void)
{
	lcm_init();
}

/*
 * Binary compare_id callback at 0x40413484. It selects the panel through
 * AUXADC channel 12, not through a DCS read-ID command. The platform-specific
 * IMM_GetOneChannelValue() declaration/header must be available in the target
 * kernel tree before enabling this callback.
 */
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);

static unsigned int lcm_compare_id(void)
{
	int data[4] = { 0 };
	int rawdata = 0;
	int i;
	int voltage_sum = 0;

	SET_RESET_PIN(1); MDELAY(10);
	SET_RESET_PIN(0); MDELAY(10);
	SET_RESET_PIN(1); MDELAY(120);
	MDELAY(120);

	for (i = 0; i < 10; ++i) {
		IMM_GetOneChannelValue(12, data, &rawdata);
		voltage_sum += data[0] * 100 + data[1];
	}

	return (voltage_sum / 10) <= 89;
}

LCM_DRIVER nv3049f_28_lcm_drv = {
	.name = "nv3049f_28",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.compare_id = lcm_compare_id,
};
