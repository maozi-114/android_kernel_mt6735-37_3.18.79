/* Normal-mode type-A event sequence recovered at 0x6cc618..0x6cc844.
 * Factory keys, diagnostics and the X-limit/400 exception are deliberately
 * not reproduced here; caller supplies already accepted frame records.
 */
#include <linux/input.h>
#include "ft6x36_protocol.h"

void ft_rb_report(struct input_dev *input, const struct ft_rb_frame *frame)
{
 unsigned int i;
 if (!input || !frame || frame->count > FT_RB_MAX_RECORDS)
  return;
 if (!frame->count) {
  input_report_key(input, BTN_TOUCH, 0);
  input_mt_sync(input);
 } else {
  for (i = 0; i < frame->count; i++) {
   const struct ft_rb_point *p = &frame->points[i];
   input_report_abs(input, ABS_MT_TRACKING_ID, p->id);
   input_report_key(input, BTN_TOUCH, 1);
   input_report_abs(input, ABS_MT_TOUCH_MAJOR, 20);
   input_report_abs(input, ABS_MT_PRESSURE, 63);
   input_report_abs(input, ABS_MT_POSITION_X, p->x);
   input_report_abs(input, ABS_MT_POSITION_Y, p->y);
   input_mt_sync(input);
  }
 }
 input_sync(input);
}
