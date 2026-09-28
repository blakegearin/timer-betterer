/*******************************************************************************
 * FILENAME :        touch.c
 *
 * DESCRIPTION :
 *      Implements DoubleTap, the record of a menu's last tap used by the
 *      touch menus. The whole file compiles away when TOUCH_INPUT is off, so
 *      platforms without touch carry no touch code.
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-27
 *
 */

#include "touch.h"

#if TOUCH_INPUT

static uint32_t prv_touch_now_ms(void) {
  time_t sec;
  uint16_t ms;
  time_ms(&sec, &ms);
  return (uint32_t)sec * 1000u + ms;
}

void double_tap_reset(DoubleTap *double_tap) {
  double_tap->row = DOUBLE_TAP_NONE;
  double_tap->ms = 0;
}

bool double_tap_tap(DoubleTap *double_tap, uint16_t row) {
  const uint32_t now = prv_touch_now_ms();
  if (row == double_tap->row &&
      now - double_tap->ms <= TOUCH_DOUBLE_TAP_MS) {
    double_tap_reset(double_tap);
    return true;
  }
  double_tap->row = row;
  double_tap->ms = now;
  return false;
}

#endif  // TOUCH_INPUT
