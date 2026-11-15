/* Readback-derived normal-touch driver. No firmware programming.
 * Binding is explicit: fts6x36 I2C name or reconstructed,ft6x36-readback.
 * The latter is a NEW opt-in binding, not a recovered original compatible.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "tpd.h"
#include "ft6x36_transport.h"
#include "ft6x36_input.h"

struct ft_rb_device {
 struct ft_rb_bus bus;
 int irq;
 bool suspended;
};
static struct ft_rb_device *active;
static struct regulator *supply;

static irqreturn_t ft_irq(int irq, void *arg)
{
 struct ft_rb_device *d = arg;
 struct ft_rb_frame f;
 int ret;
 if (d->suspended)
  return IRQ_HANDLED;
 ret = ft_rb_read_frame(&d->bus, &f);
 if (!ret)
  ft_rb_report(tpd->dev, &f);
 else
  dev_err_ratelimited(&d->bus.client->dev, "report read failed: %d\n", ret);
 return IRQ_HANDLED;
}

static void ft_reset(unsigned int low_ms, unsigned int high_ms)
{
 /* Destination MTK API maps selector 0 to reset pinctrl, as original wrapper. */
 tpd_gpio_output(GTP_RST_PORT, 0);
 msleep(low_ms);
 tpd_gpio_output(GTP_RST_PORT, 1);
 msleep(high_ms);
}

static int ft_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
 struct ft_rb_device *d;
 struct device_node *node;
 unsigned char reg = 0xa3, chip;
 int ret;
 if (active)
  return -EBUSY;
 if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
  return -EOPNOTSUPP;
 d = kzalloc(sizeof(*d), GFP_KERNEL);
 if (!d)
  return -ENOMEM;
 d->bus.client = client;
 mutex_init(&d->bus.lock);
 ret = regulator_enable(supply);
 if (ret)
  goto free_dev;
 ft_reset(100, 200);
 /* Original probe reads A3. Require a complete transfer, but do not invent
  * a chip-ID allowlist or override the board-provided slave address. */
 ret = i2c_master_send(client, &reg, 1);
 if (ret != 1) {
  ret = ret < 0 ? ret : -EIO;
  goto power_off;
 }
 ret = i2c_master_recv(client, &chip, 1);
 if (ret != 1) {
  ret = ret < 0 ? ret : -EIO;
  goto power_off;
 }
 d->irq = client->irq;
 if (d->irq <= 0) {
  node = of_find_matching_node(NULL, touch_of_match);
  if (!node) {
   ret = -ENODEV;
   goto power_off;
  }
  d->irq = irq_of_parse_and_map(node, 0);
  of_node_put(node);
 }
 if (d->irq <= 0) {
  ret = -EINVAL;
  goto power_off;
 }
 tpd_gpio_as_int(GTP_INT_PORT);
 input_set_abs_params(tpd->dev, ABS_MT_TRACKING_ID, 0, 15, 0, 0);
 input_set_abs_params(tpd->dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
 input_set_abs_params(tpd->dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
 /* Threaded IRQ replaces original hard-IRQ + dedicated waitqueue thread. */
 ret = request_threaded_irq(d->irq, NULL, ft_irq,
                           IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                           "ft6x36-readback", d);
 if (ret)
  goto power_off;
 i2c_set_clientdata(client, d);
 active = d;
 tpd_load_status = 1;
 dev_info(&client->dev, "readback touch: addr=0x%02x A3=0x%02x irq=%d\n",
          client->addr, chip, d->irq);
 return 0;
power_off:
 regulator_disable(supply);
free_dev:
 kfree(d);
 return ret;
}
static int ft_remove(struct i2c_client *client)
{
 struct ft_rb_device *d = i2c_get_clientdata(client);
 free_irq(d->irq, d);
 if (active == d)
  active = NULL;
 regulator_disable(supply);
 tpd_load_status = 0;
 kfree(d);
 return 0;
}
static void ft_suspend(struct device *dev)
{
 struct ft_rb_device *d = active;
 struct ft_rb_frame released = {0};
 if (!d || d->suspended)
  return;
 disable_irq(d->irq);
 d->suspended = true;
 ft_rb_report(tpd->dev, &released);
 /* Conservative adaptation: hold reset, not partially recovered gesture/PM
  * register writes. Supply remains enabled across suspend. */
 tpd_gpio_output(GTP_RST_PORT, 0);
}
static void ft_resume(struct device *dev)
{
 struct ft_rb_device *d = active;
 if (!d || !d->suspended)
  return;
 ft_reset(100, 200);
 d->suspended = false;
 enable_irq(d->irq);
}
static const struct i2c_device_id ft_ids[] = {
 { "fts6x36", 0 }, { }
};
MODULE_DEVICE_TABLE(i2c, ft_ids);
static const struct of_device_id ft_match[] = {
 { .compatible = "reconstructed,ft6x36-readback" }, { }
};
MODULE_DEVICE_TABLE(of, ft_match);
static struct i2c_driver ft_driver = {
 .driver = { .name = "ft6x36_readback", .of_match_table = ft_match },
 .probe = ft_probe, .remove = ft_remove, .id_table = ft_ids,
};
static int ft_local_init(void)
{
 int ret;
 supply = regulator_get(tpd->tpd_dev, "vtouch");
 if (IS_ERR(supply))
  return PTR_ERR(supply);
 ret = regulator_set_voltage(supply, 2800000, 2800000);
 if (ret)
  goto put;
 tpd_load_status = 0;
 ret = i2c_add_driver(&ft_driver);
 if (ret)
  goto put;
 if (!tpd_load_status) {
  i2c_del_driver(&ft_driver);
  ret = -ENODEV;
  goto put;
 }
 return 0;
put:
 regulator_put(supply);
 supply = NULL;
 return ret;
}
static struct tpd_driver_t ft_tpd = {
 .tpd_device_name = "fts6x36",
 .tpd_local_init = ft_local_init,
 .suspend = ft_suspend, .resume = ft_resume,
};
static int __init ft_init(void)
{
 tpd_get_dts_info();
 return tpd_driver_add(&ft_tpd);
}
static void __exit ft_exit(void)
{
 if (supply) {
  i2c_del_driver(&ft_driver);
  regulator_put(supply);
 }
 tpd_driver_remove(&ft_tpd);
}
module_init(ft_init);
module_exit(ft_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Readback-derived FT6x36 normal touch driver");
