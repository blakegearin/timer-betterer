/*******************************************************************************
 * FILENAME :        menu_list.h
 *
 * DESCRIPTION :
 *      Cursor stepping for the app's three MenuLayer lists: the timers, the
 *      settings, and the options.
 *
 * PUBLIC FUNCTIONS :
 *      void            menu_list_step_selection(MenuLayer *menu,
 *                          int16_t offset, uint16_t num_rows,
 *                          bool wrap);
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-27
 *
 */

#pragma once

#include <pebble.h>

// Move the cursor `offset` rows through a list of `num_rows` rows. Wrap
// decides what a step past either end means: the other end, or nothing. The
// MenuLayer's own click config stops at the ends, and that is why these
// windows walk their lists by hand.
void menu_list_step_selection(MenuLayer *menu, int16_t offset, uint16_t num_rows, bool wrap);
