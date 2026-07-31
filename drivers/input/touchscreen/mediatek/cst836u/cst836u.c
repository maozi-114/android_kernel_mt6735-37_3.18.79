/*
 * Hynitron CST836U driver for the MTK 3.18 TPD framework.
 *
 * Reconstructed from the target kernel's CST836U implementation only:
 *   I2C address: 0x15
 *   report: four contiguous 5-byte reads from offsets 0/5/10/15; two contacts maximum
 *   IRQ: falling edge; EINT name: TOUCH_PANEL-eint
 *   supply: vtouch at 2.8V
 *   reset: state_rst_output0 5 ms, state_rst_output1 200 ms
 *   suspend: write A5 03, then disable vtouch
 *
 * This is deliberately not based on CST3XX/CST9217 command protocols.
 */
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "tpd.h"

#define CST836U_DRIVER_NAME       "hyn_ts"
#define CST836U_I2C_ADDR          0x15
/* I2C1 is verified by merged.dts: i2c@11008000, cell-index = <1>. */
#define CST836U_I2C_BUS           1
#define CST836U_MAX_TOUCHES       2
#define CST836U_REPORT_SIZE       20
#define CST836U_POINT_OFFSET      3
#define CST836U_POINT_SIZE        6
#define CST836U_SLEEP_CMD         0xa5
#define CST836U_SLEEP_VALUE       0x03

struct cst836u_data {
	struct i2c_client *client;
	struct pinctrl *pinctrl;
	struct pinctrl_state *eint_as_int;
	struct pinctrl_state *rst_output0;
	struct pinctrl_state *rst_output1;
	unsigned long active_slots;
	bool button_down;
	bool suspended;
};

static struct cst836u_data *cst836u;
static struct i2c_client *cst836u_client;
static struct task_struct *cst836u_thread;
static DECLARE_WAIT_QUEUE_HEAD(cst836u_waiter);
static int cst836u_irq;
static int cst836u_irq_pending;

static int cst836u_i2c_read(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	int ret;

	ret = i2c_master_send(client, &reg, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;

	ret = i2c_master_recv(client, buf, len);
	return ret == len ? 0 : (ret < 0 ? ret : -EIO);
}

static int cst836u_read_report(struct cst836u_data *ts, u8 *buf)
{
	int offset;
	int ret;

	/* Original worker: 0x00, 0x05, 0x0a, 0x0f; five bytes per transaction. */
	for (offset = 0; offset < CST836U_REPORT_SIZE; offset += 5) {
		ret = cst836u_i2c_read(ts->client, offset, buf + offset, 5);
		if (ret)
			return ret;
	}
	return 0;
}

static void cst836u_release_all(void)
{
	int id;

	if (!tpd || !tpd->dev)
		return;

	for (id = 0; id < CST836U_MAX_TOUCHES; id++) {
		input_mt_slot(tpd->dev, id);
		input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER, false);
	}
	cst836u->active_slots = 0;
	/* Original worker clears both direct-touch key states on release. */
	input_report_key(tpd->dev, BTN_TOUCH, 0);
	input_report_key(tpd->dev, BTN_TOOL_FINGER, 0);
	if (cst836u->button_down) {
		tpd_button(0, 0, 0);
		cst836u->button_down = false;
	}
	input_mt_sync_frame(tpd->dev);
	input_sync(tpd->dev);
}

static void cst836u_report(void)
{
	u8 buf[CST836U_REPORT_SIZE];
	unsigned long reported = 0;
	unsigned int count;
	bool report_button = false;
	int i;
	int ret;

	if (cst836u->suspended)
		return;

	ret = cst836u_read_report(cst836u, buf);
	if (ret) {
		pr_err_ratelimited("[CST836U] touch report read failed: %d\n", ret);
		return;
	}

	count = buf[2] & 0x0f;
	if (count > CST836U_MAX_TOUCHES)
		count = CST836U_MAX_TOUCHES;

	for (i = 0; i < count; i++) {
		const u8 *p = &buf[CST836U_POINT_OFFSET + i * CST836U_POINT_SIZE];
		/*
		 * Exact original 0x406F2BE4 mapping:
		 * p[0] top two bits are event; p[2] top nibble is ID.
		 * The original accepts event 0 (down) and 2 (contact) as active;
		 * event 1/3 releases the slot.
		 */
		unsigned int event = p[0] >> 6;
		unsigned int x = ((p[0] & 0x0f) << 8) | p[1];
		unsigned int id = p[2] >> 4;
		unsigned int y = ((p[2] & 0x0f) << 8) | p[3];
		bool down;

		/* Binary stops parsing when the two-contact ID nibble exceeds 1. */
		if (id >= CST836U_MAX_TOUCHES)
			break;

		down = (event == 0 || event == 2);

		/*
		 * The original driver sends an unambiguous single touch below its
		 * display area to tpd_button(). The supplied DTB defines all three
		 * keys at y=883; its normal display is 480x640.
		 */
		if (tpd_dts_data.use_tpd_button && count == 1 && y > 800) {
			tpd_button(x, y, down);
			cst836u->button_down = down;
			report_button = true;
			continue;
		}

		input_mt_slot(tpd->dev, id);
		input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER, down);
		if (!down)
			continue;

		input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
		input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
		/* Original 0x406F2BE4 reports logical pressure 1, not p[4]. */
		input_report_abs(tpd->dev, ABS_MT_PRESSURE, 1);
		input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, p[5] >> 4);
		reported |= BIT(id);
	}

	if (cst836u->button_down && !report_button) {
		tpd_button(0, 0, 0);
		cst836u->button_down = false;
	}

	for (i = 0; i < CST836U_MAX_TOUCHES; i++) {
		if ((cst836u->active_slots & BIT(i)) && !(reported & BIT(i))) {
			input_mt_slot(tpd->dev, i);
			input_mt_report_slot_state(tpd->dev, MT_TOOL_FINGER, false);
		}
	}
	cst836u->active_slots = reported;

	/*
	 * Required by the original worker: without BTN_TOOL_FINGER Android
	 * InputReader classifies the coordinate stream as hover, not touch.
	 */
	input_report_key(tpd->dev, BTN_TOUCH, !!reported);
	input_report_key(tpd->dev, BTN_TOOL_FINGER, !!reported);
	input_mt_sync_frame(tpd->dev);
	input_sync(tpd->dev);
}

static int cst836u_event_thread(void *unused)
{
	struct sched_param param = { .sched_priority = 4 };

	sched_setscheduler(current, SCHED_RR, &param);
	while (!kthread_should_stop()) {
		wait_event_interruptible(cst836u_waiter,
			cst836u_irq_pending || kthread_should_stop());
		if (kthread_should_stop())
			break;
		cst836u_irq_pending = 0;
		cst836u_report();
	}
	return 0;
}

static irqreturn_t cst836u_irq_handler(int irq, void *dev_id)
{
	cst836u_irq_pending = 1;
	wake_up_interruptible(&cst836u_waiter);
	return IRQ_HANDLED;
}

static int cst836u_select_state(struct pinctrl_state *state)
{
	if (!cst836u->pinctrl || IS_ERR_OR_NULL(state))
		return -EINVAL;
	return pinctrl_select_state(cst836u->pinctrl, state);
}

static int cst836u_reset(unsigned int high_delay_ms)
{
	int ret;

	ret = cst836u_select_state(cst836u->rst_output0);
	if (ret)
		return ret;
	mdelay(5);
	ret = cst836u_select_state(cst836u->rst_output1);
	if (ret)
		return ret;
	mdelay(high_delay_ms);
	return 0;
}

static int cst836u_pinctrl_init(void)
{
	/* Pinctrl states live on the shared MTK touch@ platform node. */
	cst836u->pinctrl = devm_pinctrl_get(tpd->tpd_dev);
	if (IS_ERR(cst836u->pinctrl))
		return PTR_ERR(cst836u->pinctrl);

	cst836u->eint_as_int = pinctrl_lookup_state(cst836u->pinctrl,
						     "state_eint_as_int");
	cst836u->rst_output0 = pinctrl_lookup_state(cst836u->pinctrl,
						    "state_rst_output0");
	cst836u->rst_output1 = pinctrl_lookup_state(cst836u->pinctrl,
						    "state_rst_output1");
	if (IS_ERR(cst836u->eint_as_int) || IS_ERR(cst836u->rst_output0) ||
	    IS_ERR(cst836u->rst_output1))
		return -EINVAL;

	return cst836u_select_state(cst836u->eint_as_int);
}

static int cst836u_irq_init(void)
{
	struct device_node *node;
	u32 debounce[2] = { 0, 0 };
	int ret;

	node = of_find_matching_node(NULL, touch_of_match);
	if (!node)
		return -ENODEV;

	of_property_read_u32_array(node, "debounce", debounce, ARRAY_SIZE(debounce));
	gpio_set_debounce(debounce[0], debounce[1]);
	cst836u_irq = irq_of_parse_and_map(node, 0);
	of_node_put(node);
	if (cst836u_irq <= 0)
		return -EINVAL;

	ret = request_irq(cst836u_irq, cst836u_irq_handler,
			  IRQF_TRIGGER_FALLING, "TOUCH_PANEL-eint", cst836u);
	return ret;
}

static int cst836u_tpd_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int ret;

	pr_err("[CST836U] I2C probe: adapter=%s addr=0x%02x\n",
	       dev_name(&client->adapter->dev), client->addr);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;
	if (client->addr != CST836U_I2C_ADDR)
		client->addr = CST836U_I2C_ADDR;

	cst836u = kzalloc(sizeof(*cst836u), GFP_KERNEL);
	if (!cst836u)
		return -ENOMEM;
	cst836u->client = client;
	i2c_set_clientdata(client, cst836u);

	tpd->reg = regulator_get(tpd->tpd_dev, "vtouch");
	if (IS_ERR(tpd->reg)) {
		ret = PTR_ERR(tpd->reg);
		goto err_free;
	}
	ret = regulator_set_voltage(tpd->reg, 2800000, 2800000);
	if (ret)
		goto err_reg;
	ret = regulator_enable(tpd->reg);
	if (ret)
		goto err_reg;

	ret = cst836u_pinctrl_init();
	if (ret)
		goto err_power;
	ret = cst836u_reset(200);
	if (ret)
		goto err_power;

	ret = cst836u_irq_init();
	if (ret)
		goto err_power;

	cst836u_thread = kthread_run(cst836u_event_thread, NULL, "mtk-tpd");
	if (IS_ERR(cst836u_thread)) {
		ret = PTR_ERR(cst836u_thread);
		cst836u_thread = NULL;
		goto err_irq;
	}

	tpd_load_status = 1;
	return 0;

err_irq:
	free_irq(cst836u_irq, cst836u);
err_power:
	regulator_disable(tpd->reg);
err_reg:
	regulator_put(tpd->reg);
err_free:
	kfree(cst836u);
	cst836u = NULL;
	return ret;
}

static int cst836u_tpd_remove(struct i2c_client *client)
{
	if (cst836u_thread)
		kthread_stop(cst836u_thread);
	if (cst836u_irq > 0)
		free_irq(cst836u_irq, cst836u);
	if (tpd && tpd->reg && !IS_ERR(tpd->reg)) {
		regulator_disable(tpd->reg);
		regulator_put(tpd->reg);
	}
	kfree(cst836u);
	cst836u = NULL;
	return 0;
}

static const struct i2c_device_id cst836u_tpd_id[] = {
	{ CST836U_DRIVER_NAME, 0 },
	{ }
};

/* Exact original i2c_driver OF compatible from the target kernel. */
static const struct of_device_id cst836u_of_match[] = {
	{ .compatible = "mediatek,cap_touch_cst" },
	{ }
};
MODULE_DEVICE_TABLE(of, cst836u_of_match);

static struct i2c_driver cst836u_i2c_driver = {
	.driver = {
		.name = CST836U_DRIVER_NAME,
		.of_match_table = of_match_ptr(cst836u_of_match),
	},
	.probe = cst836u_tpd_probe,
	.remove = cst836u_tpd_remove,
	.id_table = cst836u_tpd_id,
};

static int cst836u_local_init(void)
{
	struct i2c_adapter *adapter;
	struct i2c_board_info board_info = {
		I2C_BOARD_INFO(CST836U_DRIVER_NAME, CST836U_I2C_ADDR),
	};
	int ret;

	pr_err("[CST836U] TPD local init\n");
	tpd_load_status = 0;

	/* The adapter already exists when TPD invokes local_init(). */
	ret = i2c_add_driver(&cst836u_i2c_driver);
	if (ret)
		return ret;

	adapter = i2c_get_adapter(CST836U_I2C_BUS);
	if (!adapter) {
		pr_err("[CST836U] I2C%d adapter not registered\n", CST836U_I2C_BUS);
		ret = -ENODEV;
		goto err_del_driver;
	}

	/*
	 * i2c_register_board_info() is too late here: I2C1 was registered
	 * before TPD local_init(). i2c_new_device() creates the client now,
	 * and i2c_add_driver() above binds this driver immediately.
	 */
	cst836u_client = i2c_new_device(adapter, &board_info);
	i2c_put_adapter(adapter);
	if (!cst836u_client) {
		pr_err("[CST836U] cannot create I2C%d client at 0x%02x\n",
		       CST836U_I2C_BUS, CST836U_I2C_ADDR);
		ret = -ENODEV;
		goto err_del_driver;
	}

	if (!tpd_load_status) {
		pr_err("[CST836U] client created but probe failed\n");
		ret = -ENODEV;
		goto err_unregister_client;
	}

	/* Both key bits must be present before the TPD input device registers. */
	__set_bit(BTN_TOUCH, tpd->dev->keybit);
	__set_bit(BTN_TOOL_FINGER, tpd->dev->keybit);
	input_set_abs_params(tpd->dev, ABS_MT_POSITION_X, 0,
			     tpd_dts_data.tpd_resolution[0], 0, 0);
	input_set_abs_params(tpd->dev, ABS_MT_POSITION_Y, 0,
			     tpd_dts_data.tpd_resolution[1], 0, 0);
	input_set_abs_params(tpd->dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(tpd->dev, ABS_MT_TOUCH_MAJOR, 0, 15, 0, 0);
	input_mt_init_slots(tpd->dev, CST836U_MAX_TOUCHES, INPUT_MT_DIRECT);

	if (tpd_dts_data.use_tpd_button)
		tpd_button_setting(tpd_dts_data.tpd_key_num,
				   tpd_dts_data.tpd_key_local,
				   tpd_dts_data.tpd_key_dim_local);
	tpd_type_cap = 1;
	return 0;

err_unregister_client:
	i2c_unregister_device(cst836u_client);
	cst836u_client = NULL;
err_del_driver:
	i2c_del_driver(&cst836u_i2c_driver);
	return ret;
}

static void cst836u_suspend(struct device *h)
{
	u8 cmd[] = { CST836U_SLEEP_CMD, CST836U_SLEEP_VALUE };

	if (!cst836u || cst836u->suspended)
		return;
	disable_irq(cst836u_irq);
	cst836u_release_all();
	/* Exact original command before regulator_disable(): A5 03. */
	i2c_master_send(cst836u->client, cmd, sizeof(cmd));
	regulator_disable(tpd->reg);
	cst836u->suspended = true;
}

static void cst836u_resume(struct device *h)
{
	if (!cst836u || !cst836u->suspended)
		return;
	if (regulator_enable(tpd->reg))
		return;
	if (cst836u_reset(200)) {
		regulator_disable(tpd->reg);
		return;
	}
	cst836u->suspended = false;
	enable_irq(cst836u_irq);
}

static struct tpd_driver_t cst836u_tpd_driver = {
	.tpd_device_name = CST836U_DRIVER_NAME,
	.tpd_local_init = cst836u_local_init,
	.suspend = cst836u_suspend,
	.resume = cst836u_resume,
	.tpd_have_button = 1,
};

static int __init cst836u_tpd_init(void)
{
	pr_err("[CST836U] init: waiting for OF node mediatek,cap_touch_cst\n");
	tpd_get_dts_info();
	return tpd_driver_add(&cst836u_tpd_driver);
}

static void __exit cst836u_tpd_exit(void)
{
	tpd_driver_remove(&cst836u_tpd_driver);
	if (cst836u_client) {
		i2c_unregister_device(cst836u_client);
		cst836u_client = NULL;
	}
	i2c_del_driver(&cst836u_i2c_driver);
}

module_init(cst836u_tpd_init);
module_exit(cst836u_tpd_exit);
MODULE_DESCRIPTION("Hynitron CST836U MTK TPD driver");
MODULE_LICENSE("GPL v2");
