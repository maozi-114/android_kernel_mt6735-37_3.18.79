/* Probe reset sequence: original 0x6cd3b0..0x6cd3e4.
 * No numeric GPIO assumptions. Platform owner provides pinctrl handle.
 */
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include "ft6x36_reset.h"
int ft_rb_probe_reset(struct pinctrl *pins)
{
 struct pinctrl_state *low, *high;
 int ret;
 if (IS_ERR_OR_NULL(pins))
  return -EINVAL;
 low = pinctrl_lookup_state(pins, "state_rst_output0");
 if (IS_ERR(low))
  return PTR_ERR(low);
 high = pinctrl_lookup_state(pins, "state_rst_output1");
 if (IS_ERR(high))
  return PTR_ERR(high);
 ret = pinctrl_select_state(pins, low);
 if (ret)
  return ret;
 msleep(100);
 ret = pinctrl_select_state(pins, high);
 if (ret)
  return ret;
 msleep(200);
 return 0;
}
