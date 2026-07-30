/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 */
/* Restored from vendor binary: st7701_st_28 / hui boe7701_28
 * 480x640, 2-lane DSI, SYNC_PULSE video, RGB888, MADCTL=0x00 (RGB)
 */

#define LOG_TAG "LCM"

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
#endif

#include "lcm_drv.h"

#ifdef BUILD_LK
#include <platform/upmu_common.h>
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
/* kernel: no CONFIG_MTK_LEGACY — avoid mach/mt_*.h */
#endif

#ifdef BUILD_LK
#define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#define LCM_LOGI(fmt, args...)  pr_err("[KERNEL/"LOG_TAG"]"fmt, ##args)
#define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

/* AUXADC LCM-ID (binary compare_id uses IMM_GetOneChannelValue ch12) */
#ifndef BUILD_LK
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);
#endif

static LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
	lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define read_reg_v2(cmd, buffer, buffer_size) \
	lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

/* binary lcm_get_params */
#define FRAME_WIDTH			(480)
#define FRAME_HEIGHT			(640)
#define HSA				(10)
#define HBP				(80)
#define HFP				(80)
#define VSA				(8)
#define VBP				(20)
#define VFP				(20)

/* binary push_table markers: 0xFC=delay, 0xFD=end (same as REGFLAG_*) */
#define REGFLAG_DELAY			0xFFFC
#define REGFLAG_UDELAY			0xFFFB
#define REGFLAG_END_OF_TABLE		0xFFFD

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* LCM ID ADC: binary sub_404131A0 */
#define LCM_ID_ADC_CHANNEL		12
#define LCM_ID_ADC_MAX_MV		89
#define LCM_ID_ADC_SAMPLES		10

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

/* binary suspend table unk_40E5ED10 (5 entries):
 * 0x28 → delay 50 → 0x10 → delay 200 → END
 */
static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 50, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 200, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

/*
 * binary init table unk_40E5EE60, 0x29=41 entries, stride 66
 * ST7701 page13/10/11 + user cmd set
 */
static struct LCM_setting_table init_setting_vdo[] = {
	/* Command2 Page 0x13 */
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13} },
	{0xEF, 1, {0x08} },

	/* Command2 Page 0x10 — display / gamma */
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10} },
	{0xC0, 2, {0x4F, 0x00} },
	{0xC1, 2, {0x0D, 0x12} },
	{0xC2, 2, {0x00, 0x0A} },
	{0xCC, 1, {0x10} },
	{0xB0, 16, {0x00, 0x03, 0x0A, 0x0B, 0x0E, 0x02, 0x00, 0x02,
		    0x04, 0x18, 0x04, 0x14, 0x11, 0x24, 0x2A, 0x1F} },
	{0xB1, 16, {0x00, 0x12, 0x17, 0x10, 0x14, 0x0B, 0x04, 0x0B,
		    0x09, 0x26, 0x06, 0x15, 0x13, 0x2E, 0x34, 0x1F} },

	/* Command2 Page 0x11 — power / GIP */
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11} },
	{0xB0, 1, {0x6D} },
	{0xB1, 1, {0x4B} },
	{0xB2, 1, {0x87} },
	{0xB3, 1, {0x80} },
	{0xB5, 1, {0x49} },
	{0xB7, 1, {0x87} },
	{0xB8, 1, {0x23} },
	{0xC1, 1, {0x78} },
	{0xC2, 1, {0x78} },
	{0xD0, 1, {0x88} },
	{REGFLAG_DELAY, 100, {} },

	{0xE0, 4, {0x00, 0x00, 0x00, 0x00} },
	{0xE1, 11, {0x06, 0xA8, 0x08, 0xA8, 0x05, 0xA8, 0x07, 0xA8,
		    0x00, 0x44, 0x44} },
	{0xE2, 13, {0x22, 0x22, 0x44, 0x44, 0x8B, 0xA8, 0x8D, 0xA8,
		    0x8A, 0xA8, 0x8C, 0xA8, 0x00} },
	{0xE3, 4, {0x00, 0x00, 0x22, 0x22} },
	{0xE4, 2, {0x44, 0x44} },
	{0xE5, 16, {0x08, 0x8C, 0xD1, 0xD9, 0x0A, 0x8E, 0xD1, 0xD9,
		    0x0C, 0x90, 0xD1, 0xD9, 0x0E, 0x93, 0xD1, 0xD9} },
	{0xE6, 4, {0x00, 0x00, 0x22, 0x22} },
	{0xE7, 2, {0x44, 0x44} },
	{0xE8, 16, {0x07, 0x8B, 0xD1, 0xD9, 0x09, 0x8D, 0xD1, 0xD9,
		    0x0B, 0x8F, 0xD1, 0xD9, 0x0D, 0x91, 0xD1, 0xD9} },
	{0xE9, 2, {0x36, 0x00} },
	{0xEB, 7, {0x00, 0x00, 0x4E, 0x4E, 0xEE, 0x44, 0x40} },
	{0xED, 16, {0xFF, 0xFF, 0x32, 0xBF, 0x01, 0xC4, 0x56, 0x7F,
		    0x76, 0x54, 0xC1, 0x0F, 0xB2, 0x3F, 0xFF, 0xFF} },

	/* User Command Set (page 0) */
	{0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00} },
	{0x11, 0, {} },
	{REGFLAG_DELAY, 120, {} },
	/* Explicitly select the DSI host RGB888 pixel stream (COLMOD). */
	{0x3A, 1, {0x77} },
	{0x35, 1, {0x00} },
	{0x36, 1, {0x00} },
	{0x29, 0, {} },
	{REGFLAG_DELAY, 50, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static void push_table(struct LCM_setting_table *table, unsigned int count,
		       unsigned char force_update)
{
	unsigned int i;
	unsigned int cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;
		switch (cmd) {
		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;
		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			dsi_set_cmdq_V2(cmd, table[i].count,
					table[i].para_list, force_update);
			break;
		}
	}
}

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}

/* binary sub_40413288 */
static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	LCM_LOGI("st7701_st_28/boe7701_28: %ux%u RGB888 2-lane SYNC_PULSE PLL=150\n",
		 FRAME_WIDTH, FRAME_HEIGHT);

	/* a1 mode=1 → SYNC_PULSE_VDO_MODE */
	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	params->dsi.switch_mode = CMD_MODE;
	params->dsi.switch_mode_enable = 0;

	/* a1 LANE_NUM=2 */
	params->dsi.LANE_NUM = LCM_TWO_LANE;
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	params->dsi.packet_size = 256;
	/* PS = 0x100 → LCM_PACKED_PS_24BIT_RGB888 */
	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = VSA;
	params->dsi.vertical_backporch = VBP;
	params->dsi.vertical_frontporch = VFP;
	params->dsi.vertical_frontporch_for_low_power = VFP;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = HSA;
	params->dsi.horizontal_backporch = HBP;
	params->dsi.horizontal_frontporch = HFP;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;

	/* continuous clock; SSC related bit set in binary → disable SSC */
	params->dsi.cont_clock = 1;
	params->dsi.ssc_disable = 1;

#ifndef MACH_FPGA
	params->dsi.PLL_CLOCK = 150;
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif
	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 0;
	params->dsi.customization_esd_check_enable = 0;
}

static void lcm_init_power(void)
{
}

static void lcm_suspend_power(void)
{
}

static void lcm_resume_power(void)
{
}

/* binary sub_404133F4: RST 1/20 → 0/50 → 1/100, then 41-entry table */
static void lcm_init(void)
{
	LCM_LOGI("st7701_st_28 lcm_init: RST 20/50/100 then DCS table\n");

	SET_RESET_PIN(1);
	MDELAY(20);
	SET_RESET_PIN(0);
	MDELAY(50);
	SET_RESET_PIN(1);
	MDELAY(100);

	push_table(init_setting_vdo,
		   sizeof(init_setting_vdo) / sizeof(struct LCM_setting_table),
		   1);
}

/* binary sub_404133D4: short suspend table only */
static void lcm_suspend(void)
{
	push_table(lcm_suspend_setting,
		   sizeof(lcm_suspend_setting) /
		   sizeof(struct LCM_setting_table), 1);
}

/* binary sub_40413470 → lcm_init */
static void lcm_resume(void)
{
	lcm_init();
}

/*
 * binary sub_404131A0 (lcm_compare_id):
 * RST 1/10 → 0/10 → 1, delay 120+120,
 * AUXADC ch12 x10, adc_vol = avg(100*data[0] + data[1]),
 * match if adc_vol <= 89
 */
static unsigned int lcm_compare_id(void)
{
	int data[4] = { 0, 0, 0, 0 };
	int rawdata = 0;
	int i;
	int adc_vol = 0;
	int ret;

	SET_RESET_PIN(1);
	MDELAY(10);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(120);
	MDELAY(120);

#ifndef BUILD_LK
	for (i = 0; i < LCM_ID_ADC_SAMPLES; i++) {
		ret = IMM_GetOneChannelValue(LCM_ID_ADC_CHANNEL, data, &rawdata);
		if (ret == 0)
			adc_vol += data[0] * 100 + data[1];
	}
	adc_vol /= LCM_ID_ADC_SAMPLES;
#else
	/* LK path: keep structure; force match if no AUXADC helper wired */
	adc_vol = 0;
#endif

	LCM_LOGI("%s:hui boe7701_28 lcm adc_vol is %d\n",
		 "lcm_compare_id", adc_vol);

	return (adc_vol <= LCM_ID_ADC_MAX_MV) ? 1 : 0;
}

/*
 * Must match parent list (mt65xx_lcm_list.c / .h):
 *   extern LCM_DRIVER st7701_lcm_drv;
 *   lcm_driver_list[] → &st7701_lcm_drv  (#if defined(ST7701))
 *   lcm_name_list[]   → "st7701" //st7701_st_28
 * Also matches CONFIG_CUSTOM_KERNEL_LCM / mtkfb force name: "st7701"
 * Binary original string was "st7701_st_28" (behavior still boe7701_28).
 */
LCM_DRIVER st7701_lcm_drv = {
	.name = "st7701_st_28",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.init_power = lcm_init_power,
	.compare_id = lcm_compare_id,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
};
