/*
 * ST7701 BOE 2.8" LCM driver
 * Reconstructed from MediaTek boot kernel:
 *   C:\Users\Administrator\Desktop\maozi\tool\aik\aik-tmp\split_img\_boot.img-kernel.extracted\0
 *
 * Driver name: st7701_st_28
 * Log tag:     hui boe7701_28
 *
 * Key reverse addresses:
 *   set_util_funcs : 0x40413324  (sub_40413324)
 *   get_params     : 0x40413288  (sub_40413288)
 *   init           : 0x404133F4  (sub_404133F4)
 *   suspend        : 0x404133D4  (sub_404133D4)
 *   resume         : 0x40413470  (sub_40413470 -> init)
 *   compare_id     : 0x404131A0  (sub_404131A0)
 *   push_table     : 0x40413348  (sub_40413348)
 *   init table     : 0x40E5EE60  (41 entries, stride 66)
 *   suspend table  : 0x40E5ED10  (5 entries, stride 66)
 *   lcm_driver ops : ~0x40E5F8F8
 *
 * Panel:
 *   IC          : Sitronix ST7701
 *   Resolution  : 480 x 640
 *   Interface   : MIPI-DSI, 2-lane
 *   Mode        : SYNC_PULSE video
 *   Pixel fmt   : packed 24-bit RGB888
 *   Color order : RGB (MADCTL=0x00, BGR bit cleared)
 *   PLL clock   : 150 MHz
 *   ID method   : AUXADC channel 12 voltage average <= 89
 */

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/kernel.h>
#endif

#include "lcm_drv.h"

#ifdef BUILD_LK
#include <platform/mt_gpio.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#include <string.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
#include <mach/mt_gpio.h>
#endif

/* -------------------------------------------------------------------------- */
/*  Local constants / variables                                               */
/* -------------------------------------------------------------------------- */

#define LCM_DSI_CMD_MODE                0

#define FRAME_WIDTH                     (480)
#define FRAME_HEIGHT                    (640)

#define REGFLAG_DELAY                   0xFC
#define REGFLAG_END_OF_TABLE            0xFD

#define LCM_ID_ADC_CHANNEL              12
#define LCM_ID_ADC_MAX                  89
#define LCM_ID_ADC_SAMPLE_COUNT         10

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)                (lcm_util.set_reset_pin((v)))
#define UDELAY(n)                       (lcm_util.udelay(n))
#define MDELAY(n)                       (lcm_util.mdelay(n))

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
        lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
        lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)                  lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
        lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)                   lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
        lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

struct LCM_setting_table {
        unsigned char cmd;
        unsigned char count;
        unsigned char para_list[64];
};

/* -------------------------------------------------------------------------- */
/*  Init command table @ 0x40E5EE60                                           */
/*  Entry size = 66 bytes, count = 0x29 (41)                                  */
/* -------------------------------------------------------------------------- */

static struct LCM_setting_table lcm_initialization_setting[] = {
        /* Command2 BK0 / Page 0x13 */
        {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}},
        {0xEF, 1, {0x08}},

        /* Command2 BK0 / Page 0x10 : display timing + gamma */
        {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}},
        {0xC0, 2, {0x4F, 0x00}},
        {0xC1, 2, {0x0D, 0x12}},
        {0xC2, 2, {0x00, 0x0A}},
        {0xCC, 1, {0x10}},

        /* Positive gamma */
        {0xB0, 16, {0x00, 0x03, 0x0A, 0x0B, 0x0E, 0x02, 0x00, 0x02,
                    0x04, 0x18, 0x04, 0x14, 0x11, 0x24, 0x2A, 0x1F}},
        /* Negative gamma */
        {0xB1, 16, {0x00, 0x12, 0x17, 0x10, 0x14, 0x0B, 0x04, 0x0B,
                    0x09, 0x26, 0x06, 0x15, 0x13, 0x2E, 0x34, 0x1F}},

        /* Command2 BK1 / Page 0x11 : power */
        {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}},
        {0xB0, 1, {0x6D}},          /* Vop */
        {0xB1, 1, {0x4B}},          /* VCOM */
        {0xB2, 1, {0x87}},
        {0xB3, 1, {0x80}},
        {0xB5, 1, {0x49}},
        {0xB7, 1, {0x87}},
        {0xB8, 1, {0x23}},
        {0xC1, 1, {0x78}},
        {0xC2, 1, {0x78}},
        {0xD0, 1, {0x88}},
        {REGFLAG_DELAY, 100, {}},

        /* Power / GIP */
        {0xE0, 4,  {0x00, 0x00, 0x00, 0x00}},
        {0xE1, 11, {0x06, 0xA8, 0x08, 0xA8, 0x05, 0xA8, 0x07, 0xA8,
                    0x00, 0x44, 0x44}},
        {0xE2, 13, {0x22, 0x22, 0x44, 0x44, 0x8B, 0xA8, 0x8D, 0xA8,
                    0x8A, 0xA8, 0x8C, 0xA8, 0x00}},
        {0xE3, 4,  {0x00, 0x00, 0x22, 0x22}},
        {0xE4, 2,  {0x44, 0x44}},
        {0xE5, 16, {0x08, 0x8C, 0xD1, 0xD9, 0x0A, 0x8E, 0xD1, 0xD9,
                    0x0C, 0x90, 0xD1, 0xD9, 0x0E, 0x93, 0xD1, 0xD9}},
        {0xE6, 4,  {0x00, 0x00, 0x22, 0x22}},
        {0xE7, 2,  {0x44, 0x44}},
        {0xE8, 16, {0x07, 0x8B, 0xD1, 0xD9, 0x09, 0x8D, 0xD1, 0xD9,
                    0x0B, 0x8F, 0xD1, 0xD9, 0x0D, 0x91, 0xD1, 0xD9}},
        {0xE9, 2,  {0x36, 0x00}},
        {0xEB, 7,  {0x00, 0x00, 0x4E, 0x4E, 0xEE, 0x44, 0x40}},
        {0xED, 16, {0xFF, 0xFF, 0x32, 0xBF, 0x01, 0xC4, 0x56, 0x7F,
                    0x76, 0x54, 0xC1, 0x0F, 0xB2, 0x3F, 0xFF, 0xFF}},

        /* User Command Set / Page 0 */
        {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}},

        {0x11, 0, {}},              /* Sleep Out */
        {REGFLAG_DELAY, 120, {}},

        {0x35, 1, {0x00}},          /* TE off */
        {0x36, 1, {0x00}},          /* MADCTL: RGB, no MX/MY/MV */

        {0x29, 0, {}},              /* Display ON */
        {REGFLAG_DELAY, 50, {}},

        {REGFLAG_END_OF_TABLE, 0, {}}
};

/* -------------------------------------------------------------------------- */
/*  Suspend table @ 0x40E5ED10, count = 5                                     */
/* -------------------------------------------------------------------------- */

/* binary unk_40E5ED10: 28 / delay50 / 10 / delay200 / END */
static struct LCM_setting_table lcm_suspend_setting[] = {
        {0x28, 0, {}},              /* Display OFF */
        {REGFLAG_DELAY, 50, {}},
        {0x10, 0, {}},              /* Sleep In */
        {REGFLAG_DELAY, 200, {}},
        {REGFLAG_END_OF_TABLE, 0, {}}
};

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* -------------------------------------------------------------------------- */

static void push_table(struct LCM_setting_table *table,
                       unsigned int count,
                       unsigned char force_update)
{
        unsigned int i;
        unsigned cmd;

        for (i = 0; i < count; i++) {
                cmd = table[i].cmd;

                switch (cmd) {
                case REGFLAG_DELAY:
                        MDELAY(table[i].count);
                        break;

                case REGFLAG_END_OF_TABLE:
                        break;

                default:
                        dsi_set_cmdq_V2(cmd,
                                        table[i].count,
                                        table[i].para_list,
                                        force_update);
                        break;
                }
        }
}

/* -------------------------------------------------------------------------- */
/*  LCM driver callbacks                                                      */
/* -------------------------------------------------------------------------- */

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
        /* binary: memcpy(0x410188E0, util, 216) */
        memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}

static void lcm_get_params(LCM_PARAMS *params)
{
        /* binary: sub_40413288
         * memset(params, 0, 1024)
         * then fill the fields below
         */
        memset(params, 0, sizeof(LCM_PARAMS));

        params->type   = LCM_TYPE_DSI;
        params->width  = FRAME_WIDTH;
        params->height = FRAME_HEIGHT;

        /* DSI */
        params->dsi.mode = SYNC_PULSE_VDO_MODE;   /* = 1 */

        /* Command mode cannot travel over older DSI host implementations
         * without packet-size care; this panel is pure video mode.
         */
        params->dsi.LANE_NUM                = LCM_TWO_LANE;     /* = 2 */
        params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;
        params->dsi.PS                      = LCM_PACKED_PS_24BIT_RGB888; /* 0x100 */

        /* vertical timing */
        params->dsi.vertical_sync_active    = 8;
        params->dsi.vertical_backporch      = 20;
        params->dsi.vertical_frontporch     = 20;
        params->dsi.vertical_active_line    = FRAME_HEIGHT;

        /* horizontal timing */
        params->dsi.horizontal_sync_active  = 10;
        params->dsi.horizontal_backporch    = 80;
        params->dsi.horizontal_frontporch   = 80;
        params->dsi.horizontal_active_pixel = FRAME_WIDTH;

        /* bit clock */
        params->dsi.PLL_CLOCK               = 150;

        /* binary also set:
         *   a1[164] = 150
         *   a1[168] = 1
         *   a1[210] = 1
         * which map to cont_clock / SSC related flags on this LK/kernel tree.
         */
        params->dsi.cont_clock              = 1;
        params->dsi.ssc_disable             = 1;
}

static void lcm_init(void)
{
        /* binary: sub_404133F4
         * RESET 1 -> 20ms
         * RESET 0 -> 50ms
         * RESET 1 -> 100ms
         * push_table(init, 0x29)
         */
        SET_RESET_PIN(1);
        MDELAY(20);
        SET_RESET_PIN(0);
        MDELAY(50);
        SET_RESET_PIN(1);
        MDELAY(100);

        push_table(lcm_initialization_setting,
                   sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table),
                   1);
}

static void lcm_suspend(void)
{
        /* binary: sub_404133D4
         * push_table(suspend, 5)
         * sequence: 28 / delay50 / 10 / delay200 / END
         */
        push_table(lcm_suspend_setting,
                   sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table),
                   1);
}

static void lcm_resume(void)
{
        /* binary: sub_40413470 simply re-calls lcm_init() */
        lcm_init();
}

static unsigned int lcm_compare_id(void)
{
        /* binary: sub_404131A0
         *
         * RESET pulse for ADC path:
         *   high 10ms -> low 10ms -> high
         * then delay 120 + 120 ms
         * sample AUXADC ch12 ten times:
         *   vol_i = 100 * raw_hi + raw_lo   (platform-specific packing)
         *   avg   = sum(vol_i) / 10
         * match if avg <= 89
         *
         * log:
         *   "%s:hui boe7701_28 lcm adc_vol is %d\n", "lcm_compare_id", avg
         */
        int i;
        int adc_vol = 0;
        int data[4] = {0, 0, 0, 0};
        int rawdata = 0;
        int ret = 0;

        SET_RESET_PIN(1);
        MDELAY(10);
        SET_RESET_PIN(0);
        MDELAY(10);
        SET_RESET_PIN(1);
        MDELAY(120);
        MDELAY(120);

        for (i = 0; i < LCM_ID_ADC_SAMPLE_COUNT; i++) {
                /* IMM_GetOneChannelValue(channel, data, &rawdata)
                 * binary called sub_4036CF20(12, &data[0], &raw)
                 * and used: vol += data[1] + 100 * data[0]
                 */
#ifdef BUILD_LK
                ret = IMM_GetOneChannelValue(LCM_ID_ADC_CHANNEL, data, &rawdata);
#else
                /* Kernel path may use different helper; keep same formula. */
                ret = IMM_GetOneChannelValue(LCM_ID_ADC_CHANNEL, data, &rawdata);
#endif
                if (ret == 0)
                        adc_vol += data[1] + 100 * data[0];
        }

        adc_vol /= LCM_ID_ADC_SAMPLE_COUNT;

#ifdef BUILD_LK
        printf("%s:hui boe7701_28 lcm adc_vol is %d\n", "lcm_compare_id", adc_vol);
#else
        printk("%s:hui boe7701_28 lcm adc_vol is %d\n", "lcm_compare_id", adc_vol);
#endif

        return (adc_vol <= LCM_ID_ADC_MAX) ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/*  Driver export — aligned to parent mt65xx_lcm_list                         */
/*    extern LCM_DRIVER st7701_lcm_drv;                                       */
/*    lcm_driver_list[] → &st7701_lcm_drv  (#if defined(ST7701))              */
/*    lcm_name_list[]   → "st7701"                                            */
/*  Binary original name string was "st7701_st_28"; keep behavior, match list */
/*  ops near 0x40E5F8F8: name, set_util, get_params, init, sus, res, cmp_id   */
/*  NOTE: only st7701.c is built (Makefile obj-y += st7701.o). This file is   */
/*  the IDA skeleton reference; do not compile both or symbols will clash.    */
/* -------------------------------------------------------------------------- */

LCM_DRIVER st7701_lcm_drv = {
        .name           = "st7701",
        .set_util_funcs = lcm_set_util_funcs,
        .get_params     = lcm_get_params,
        .init           = lcm_init,
        .suspend        = lcm_suspend,
        .resume         = lcm_resume,
        .compare_id     = lcm_compare_id,
};

/*
 * Timing summary
 * --------------
 * HTotal = HSA + HBP + HACT + HFP = 10 + 80 + 480 + 80 = 650
 * VTotal = VSA + VBP + VACT + VFP =  8 + 20 + 640 + 20 = 688
 * Mode   = SYNC_PULSE video
 * Lane   = 2
 * Format = RGB888 / RGB order
 * PLL    = 150 MHz
 *
 * Rough fps estimate (2-lane, 24bpp):
 *   bitclk ~= 150 MHz
 *   throughput ~= 150e6 * 2 / 24 pixels/s
 *   fps ~= throughput / (HTotal * VTotal)
 *        ~= (12.5e6) / (650 * 688) ≈ 28 fps class
 *   (exact fps still depends on DSI PHY effective rate / blanking)
 */
