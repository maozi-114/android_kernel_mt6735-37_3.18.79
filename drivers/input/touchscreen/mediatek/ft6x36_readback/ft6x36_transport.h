#ifndef FT6X36_RB_TRANSPORT_H
#define FT6X36_RB_TRANSPORT_H
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include "ft6x36_protocol.h"
struct i2c_client;
struct ft_rb_bus {
 struct i2c_client *client;
 struct mutex lock;
 unsigned char *rx;
 dma_addr_t rx_dma;
};
int ft_rb_bus_init(struct ft_rb_bus *bus, struct i2c_client *client);
void ft_rb_bus_destroy(struct ft_rb_bus *bus);
int ft_rb_read_frame(struct ft_rb_bus *bus, struct ft_rb_frame *frame);
#endif
