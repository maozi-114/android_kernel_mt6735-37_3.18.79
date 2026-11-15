/* Readback reconstruction: transport only, no automatic device binding.
 * Original 0x6cbd7c / 0x6cc940 use MTK DMA send then receive (separate
 * transactions). Here use bounded PIO with the same STOP boundary, not a
 * combined repeated-start transfer. Address comes from the supplied client.
 */
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include "ft6x36_transport.h"

int ft_rb_read_frame(struct ft_rb_bus *bus, struct ft_rb_frame *frame)
{
 unsigned char reg = 0, data[FT_RB_REPORT_SIZE];
 int ret;
 if (!bus || !bus->client || !frame)
  return -EINVAL;
 mutex_lock(&bus->lock);
 ret = i2c_master_send(bus->client, &reg, 1);
 if (ret != 1) {
  ret = ret < 0 ? ret : -EIO;
  goto out;
 }
 ret = i2c_master_recv(bus->client, data, sizeof(data));
 if (ret != sizeof(data)) {
  ret = ret < 0 ? ret : -EIO;
  goto out;
 }
 ret = ft_rb_decode(data, sizeof(data), frame) ? -EPROTO : 0;
out:
 mutex_unlock(&bus->lock);
 return ret;
}
