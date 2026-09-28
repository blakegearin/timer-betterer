/*******************************************************************************
 * FILENAME :        touch.h
 *
 * DESCRIPTION :
 *      Definitions used by every touch feature in the app: the TOUCH_INPUT
 *      platform test, and DoubleTap, the record of a menu's last tap that
 *      makes a row open on a second tap of it and not on the first.
 *
 * NOTES :      Recognizer callbacks (RecognizerEventCb) receive no context
 *              argument -- the user_data handed to a recognizer constructor
 *              is not reachable from them -- so a window that drives its own
 *              recognizers passes NULL there and reaches its state through a
 *              file static to the one live instance, seeded on load and
 *              cleared on unload.
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-27
 *
 */

#pragma once

#include <pebble.h>

// The platforms this app gives touch input on, tested in one place: every
// touch feature in the app is #if TOUCH_INPUT, so widening the set is a
// single edit here.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define TOUCH_INPUT 1
#else
#define TOUCH_INPUT 0
#endif

#if TOUCH_INPUT

// Two taps on the same row inside this window count as one double tap.
#define TOUCH_DOUBLE_TAP_MS 500

// The row value that means no tap is awaiting its twin.
#define DOUBLE_TAP_NONE UINT16_MAX

/*
 * Structure:   DoubleTap
 *
 * One menu's memory of its last tap. The system touch bridge moves the
 * cursor onto a tapped row and fires select_click for every tap, whatever
 * moved; a lone tap must therefore be swallowed where opening belongs. A tap
 * that matches this record completes the double tap, any other becomes the
 * new record.
 */
typedef struct DoubleTap {
  uint16_t row;   //< row of the tap awaiting its twin; DOUBLE_TAP_NONE if none
  uint32_t ms;    //< when that tap landed
} DoubleTap;

void double_tap_reset(DoubleTap *double_tap);

// Report a tap on `row`, timed now: true when it completes a double tap
// (which clears the record), false when it is only the first tap on the row
// (which the record now holds).
bool double_tap_tap(DoubleTap *double_tap, uint16_t row);

#endif  // TOUCH_INPUT
