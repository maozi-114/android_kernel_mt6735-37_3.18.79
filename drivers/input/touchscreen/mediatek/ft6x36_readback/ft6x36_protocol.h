/* Recovered from readback kernel; not a claim of physical chip identity. */
#ifndef FT6X36_RB_PROTOCOL_H
#define FT6X36_RB_PROTOCOL_H
#define FT_RB_REPORT_SIZE 64
#define FT_RB_MAX_RECORDS 10
struct ft_rb_point {
 unsigned short x, y;
 unsigned char event, id;
};
struct ft_rb_frame {
 unsigned char count;
 struct ft_rb_point points[FT_RB_MAX_RECORDS];
};
/* Pure decoder, no I2C writes or platform dependencies. */
int ft_rb_decode(const unsigned char *data, unsigned int size,
                 struct ft_rb_frame *frame);
#endif
