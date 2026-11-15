#ifndef FT6X36_RB_INPUT_H
#define FT6X36_RB_INPUT_H
#include "ft6x36_protocol.h"
struct input_dev;
void ft_rb_report(struct input_dev *input, const struct ft_rb_frame *frame);
#endif
