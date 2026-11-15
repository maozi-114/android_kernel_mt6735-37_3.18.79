#ifndef FT6X36_RB_TRANSPORT_H
#define FT6X36_RB_TRANSPORT_H
#include <linux/mutex.h>
#include "ft6x36_protocol.h"
struct i2c_client;
/* Owner initializes lock and client before use; lifetime and PM serialized
 * by the future platform layer. No address override or implicit probing. */
struct ft_rb_bus {
 struct i2c_client *client;
 struct mutex lock;
};
int ft_rb_read_frame(struct ft_rb_bus *bus, struct ft_rb_frame *frame);
#endif
