#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include "ft6x36_transport.h"

int ft_rb_bus_init(struct ft_rb_bus *bus, struct i2c_client *client)
{
 bus->client = client;
 mutex_init(&bus->lock);
#ifdef CONFIG_MTK_I2C_EXTENSION
 if (of_device_is_compatible(client->adapter->dev.of_node, "mediatek,mt6735m-i2c")) {
  struct device *dma_dev = client->adapter->dev.parent;

  bus->rx = dma_alloc_coherent(dma_dev, FT_RB_REPORT_SIZE, &bus->rx_dma,
                              GFP_KERNEL | GFP_DMA32);
  if (!bus->rx)
   return -ENOMEM;
  if (!bus->rx_dma || upper_32_bits(bus->rx_dma)) {
   ft_rb_bus_destroy(bus);
   return -ERANGE;
  }
 }
#endif
 return 0;
}

void ft_rb_bus_destroy(struct ft_rb_bus *bus)
{
 if (bus->rx) {
  dma_free_coherent(bus->client->adapter->dev.parent, FT_RB_REPORT_SIZE,
                    bus->rx, bus->rx_dma);
  bus->rx = NULL;
 }
}

int ft_rb_read_frame(struct ft_rb_bus *bus, struct ft_rb_frame *frame)
{
 unsigned char reg = 0, data[FT_RB_REPORT_SIZE];
 const unsigned char *report = data;
 int ret;
 if (!bus || !bus->client || !frame)
  return -EINVAL;
 mutex_lock(&bus->lock);
 ret = i2c_master_send(bus->client, &reg, 1);
 if (ret != 1) {
  ret = ret < 0 ? ret : -EIO;
  goto out;
 }
#ifdef CONFIG_MTK_I2C_EXTENSION
 if (bus->rx) {
  struct i2c_msg msg = {
   .addr = bus->client->addr,
   .flags = I2C_M_RD,
   .len = FT_RB_REPORT_SIZE,
   .buf = (void *)(unsigned long)bus->rx_dma,
   .ext_flag = I2C_DMA_FLAG,
  };

  ret = i2c_transfer(bus->client->adapter, &msg, 1);
  if (ret != 1) {
   ret = ret < 0 ? ret : -EIO;
   goto out;
  }
  dma_rmb();
  report = bus->rx;
  ret = FT_RB_REPORT_SIZE;
 } else
#endif
  ret = i2c_master_recv(bus->client, data, sizeof(data));
 if (ret != sizeof(data)) {
  ret = ret < 0 ? ret : -EIO;
  goto out;
 }
 ret = ft_rb_decode(report, sizeof(data), frame) ? -EPROTO : 0;
out:
 mutex_unlock(&bus->lock);
 return ret;
}
