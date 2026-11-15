#include "ft6x36_protocol.h"
/* 0x6cc180: count=data[2]&15; 0x6cc190..0x6cc404: ten unrolled
 * records, stride 6. Coordinates are raw; axis mapping is not inferred.
 * Reject invalid count/short input instead of copying original unsafe behavior.
 */
int ft_rb_decode(const unsigned char *data, unsigned int size,
                 struct ft_rb_frame *frame)
{
 unsigned int i, count;
 struct ft_rb_frame decoded = {0};
 if (!data || !frame || size != FT_RB_REPORT_SIZE)
  return -1;
 count = data[2] & 15;
 if (count > FT_RB_MAX_RECORDS)
  return -1;
 decoded.count = count;
 for (i = 0; i < count; i++) {
  const unsigned char *p = data + 3 + 6 * i;
  decoded.points[i].x = ((p[0] & 15) << 8) | p[1];
  decoded.points[i].y = ((p[2] & 15) << 8) | p[3];
  decoded.points[i].event = p[0] >> 6;
  decoded.points[i].id = p[2] >> 4;
 }
 *frame = decoded;
 return 0;
}
