/*******************************************************************************
 * FILENAME :        menu_list.c
 *
 * DESCRIPTION :
 *      Implements menu_list_step_selection, the cursor stepping used by the
 *      app's three MenuLayer lists.
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-27
 *
 */

#include "menu_list.h"

void menu_list_step_selection(MenuLayer *menu, int16_t offset, uint16_t num_rows, bool wrap) {
  if (num_rows == 0) {
    return;
  }
  MenuIndex selected = menu_layer_get_selected_index(menu);
  int16_t target = (int16_t)selected.row + offset;
  if (wrap) {
    // a held button can overshoot by more than the list is long, so fold it
    // rather than assuming one step. the ends meet: a full loop lands back
    // where it started.
    target %= (int16_t)num_rows;
    if (target < 0) {
      target += (int16_t)num_rows;
    }
  } else if (target < 0) {
    target = 0;
  } else if (target >= (int16_t)num_rows) {
    target = (int16_t)num_rows - 1;
  }
  if (target == (int16_t)selected.row) {
    return;
  }
  menu_layer_set_selected_index(menu,
                                (MenuIndex) { .section = 0, .row = (uint16_t)target },
                                MenuRowAlignCenter, true);
}
