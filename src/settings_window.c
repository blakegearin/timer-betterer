/*******************************************************************************
 * FILENAME :        settings_window.c
 *
 * DESCRIPTION :
 *      Create, destroy, and manage a SettingsWindow to list settings, each
 *      drawn as a name-over-value cell.
 *
 * PUBLIC FUNCTIONS :
 *      SettingsWindow  *settings_window_create(SettingsWindowCallbacks
 *                          settings_window_callbacks, const char *title,
 *                          const uint8_t *row_ids, uint8_t num_rows);
 *      void            settings_window_destroy(SettingsWindow
 *                          *settings_window);
 *      void            settings_window_push(SettingsWindow
 *                          *settings_window, bool animated);
 *      void            settings_window_refresh(SettingsWindow
 *                          *settings_window);
 *      void            settings_window_set_highlight_color(SettingsWindow
 *                          *settings_window, GColor color);
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-24
 *
 */

#include <pebble.h>
#include "settings_window.h"
#include "touch.h"

/*
 * The whole window is excluded on aplite rather than deleted from the build:
 * wscript globs every C file under src/, so in-file exclusion is the only
 * route. The cost that matters on aplite is compiled code -- this file's .text
 * would eat exactly the heap the detail window needs -- and uncompiled code is
 * free.
 */
#ifndef PBL_PLATFORM_APLITE

// Round menu cell heights are unconditional firmware constants:
// a focused cell holds name over value, an unfocused one only the name.
#ifdef PBL_ROUND
#define SETTINGS_CELL_HEIGHT_FOCUSED 68
#define SETTINGS_CELL_HEIGHT 32
#endif

/*******************************************************************************
 * STRUCTURE DEFINITION
 */

/*
 * the structure of a SettingsWindow
 */

struct SettingsWindow {
  Window      *window;    //< main window
  MenuLayer   *menu;      //< menu layer displaying the settings
  StatusBarLayer *status; //< status bar
  SettingsWindowCallbacks callbacks; //< settings list callbacks
  const char  *title;     //< this window's own name, drawn as a header on rect
  const uint8_t *row_ids; //< which row each list row stands for, owned by the caller
  uint8_t      num_rows;  //< how many row ids there are
  GColor      highlight_color;       //< main color for highlights
#if TOUCH_INPUT
  DoubleTap double_tap;              //< tap-to-open debounce
#endif
};



/*******************************************************************************
 * PRIVATE FUNCTIONS
 */

/*
 * get number of sections for menu layer
 * this is always one for this application
 */

static uint16_t settings_get_num_sections_callback(MenuLayer *menu_layer, void *context) {
  return 1;
}



/*
 * get number of rows for menu layer, one per row id the caller was given
 */

static uint16_t settings_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index,
                                               void *context) {
  SettingsWindow *settings_window = (SettingsWindow*)context;
  return settings_window->num_rows;
}



/*
 * the window title is a section header on rect and nothing on round
 *
 * status_bar_layer_set_title is not in the app SDK, so the firmware's own
 * title mechanism has no app equivalent. On round a header lands flush in the
 * top-left corner where the circular mask has no pixels; and round shows one
 * focused setting at a time anyway, so the title would answer a question
 * nobody is asking.
 */

static int16_t settings_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index,
                                                   void *context) {
  return PBL_IF_RECT_ELSE(MENU_CELL_BASIC_HEADER_HEIGHT, 0);
}

static void settings_draw_header_callback(GContext *ctx, const Layer *cell_layer,
                                          uint16_t section_index, void *context) {
  SettingsWindow *settings_window = (SettingsWindow*)context;
  menu_cell_basic_header_draw(ctx, cell_layer, settings_window->title);
}



/*
 * draw each row: bold setting name over its current value
 *
 * menu_cell_basic_draw resolves the fonts from the theme, which is why there
 * is no font handling here -- hardcoding one would break emery and gabbro.
 * A NULL value leaves the name alone on the cell, which is what a group row
 * wants: it is navigation, and it has nothing to report.
 */

static void settings_draw_row_callback(GContext *ctx, const Layer *cell_layer,
                                       MenuIndex *cell_index, void *context) {
  SettingsWindow *settings_window = (SettingsWindow*)context;
  const uint8_t row_id = settings_window->row_ids[cell_index->row];
  menu_cell_basic_draw(ctx, cell_layer,
    settings_window->callbacks.get_name(row_id, context),
    settings_window->callbacks.get_value(row_id, context), NULL);
}



/*
 * TOUCH
 *
 * the system touch navigation drives the MenuLayer itself: a tap moves the
 * cursor onto the tapped row and fires select_click there, whatever moved.
 * that is tap to select, but it is not open, and the firmware offers no
 * double-tap of its own, so opening is the callback's business -- and the
 * button's click config must not be the MenuLayer's, or the physical SELECT
 * would ride the same debounced path. so on touch the window installs the
 * provider below instead: the same walk the main list uses, from
 * menu_list.c, clamped, since Wrap Around is a setting about the timer
 * list and not about these menus.
 */

#if TOUCH_INPUT

#include "menu_list.h"

#define SETTINGS_BUTTON_REPEAT_MS 100

static void settings_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  SettingsWindow *settings_window = (SettingsWindow*)context;
  menu_list_step_selection(settings_window->menu, -1, settings_window->num_rows, false);
}

static void settings_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  SettingsWindow *settings_window = (SettingsWindow*)context;
  menu_list_step_selection(settings_window->menu, 1, settings_window->num_rows, false);
}

static void settings_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  SettingsWindow *settings_window = (SettingsWindow*)context;
  const uint8_t row = (uint8_t)menu_layer_get_selected_index(settings_window->menu).row;
  // a button acts at once; a half-finished tap on this row must not pair
  // with anything that comes after it
  double_tap_reset(&settings_window->double_tap);
  settings_window->callbacks.clicked(settings_window->row_ids[row], settings_window);
}

static void settings_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, SETTINGS_BUTTON_REPEAT_MS,
                                          settings_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, SETTINGS_BUTTON_REPEAT_MS,
                                          settings_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, settings_select_click_handler);
}

#endif  // TOUCH_INPUT



/*
 * menu layer clicked callback
 *
 * off touch this is the physical SELECT, routed by the MenuLayer's own click
 * config, and acts at once. on touch the click config is the window's instead
 * (see the touch block below), so a click reaching this callback is always a
 * finger: the bridge has already moved the cursor onto the tapped row, and
 * the row opens only when a second tap lands on it inside the double tap
 * window.
 */

static void settings_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index,
                                     void *context) {
  SettingsWindow *settings_window = (SettingsWindow*)context;
#if TOUCH_INPUT
  if (!double_tap_tap(&settings_window->double_tap, (uint16_t)cell_index->row)) {
    return;
  }
#endif
  settings_window->callbacks.clicked(settings_window->row_ids[cell_index->row], context);
}



#ifdef PBL_ROUND
// focused cells are taller so they can hold the value line
static int16_t settings_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index,
                                                 void *context) {
  if (menu_layer_get_selected_index(menu_layer).row == cell_index->row) {
    return SETTINGS_CELL_HEIGHT_FOCUSED;
  }
  return SETTINGS_CELL_HEIGHT;
}
#endif



/*
 * window load
 */

static void settings_window_load(Window *window) {
  SettingsWindow *settings_window = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_frame(root);

#ifdef PBL_ROUND
  settings_window->menu = menu_layer_create(bounds);
#else
  settings_window->menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
                                                  bounds.size.h - STATUS_BAR_LAYER_HEIGHT));
#endif
  MenuLayerCallbacks callbacks = {
    .get_num_sections = settings_get_num_sections_callback,
    .get_num_rows = settings_get_num_rows_callback,
    .get_header_height = settings_get_header_height_callback,
    .draw_header = settings_draw_header_callback,
    .draw_row = settings_draw_row_callback,
    .select_click = settings_select_callback,
#ifdef PBL_ROUND
    .get_cell_height = settings_get_cell_height_callback,
#endif
  };
  // no get_cell_height on rect: the MenuLayer default is the system metric on
  // every platform, and MENU_CELL_BASIC_HEIGHT does not exist to name it
  menu_layer_set_callbacks(settings_window->menu, settings_window, callbacks);
#if TOUCH_INPUT
  window_set_click_config_provider_with_context(window, settings_click_config_provider,
                                                settings_window);
#else
  menu_layer_set_click_config_onto_window(settings_window->menu, window);
#endif
  menu_layer_set_highlight_colors(settings_window->menu, settings_window->highlight_color,
                                  gcolor_legible_over(settings_window->highlight_color));
  layer_add_child(root, menu_layer_get_layer(settings_window->menu));

  settings_window->status = status_bar_layer_create();
  status_bar_layer_set_colors(settings_window->status, GColorClear, GColorBlack);
  layer_add_child(root, status_bar_layer_get_layer(settings_window->status));
}

/*
 * window unload
 */

static void settings_window_unload(Window *window) {
  SettingsWindow *settings_window = window_get_user_data(window);
  status_bar_layer_destroy(settings_window->status);
  settings_window->status = NULL;
  menu_layer_destroy(settings_window->menu);
  settings_window->menu = NULL;
}



/*******************************************************************************
 * API FUNCTIONS
 */

/*
 * create a new SettingsWindow and return a pointer to it
 * the window itself is created now, its layers only while it is on screen
 */

SettingsWindow *settings_window_create(SettingsWindowCallbacks settings_window_callbacks,
                                       const char *title, const uint8_t *row_ids,
                                       uint8_t num_rows) {
  SettingsWindow *settings_window = (SettingsWindow*)malloc(sizeof(SettingsWindow));
  if (settings_window == NULL) {
    // error handling
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create SettingsWindow");
    return NULL;
  }
  settings_window->callbacks = settings_window_callbacks;
  settings_window->title = title;
  settings_window->row_ids = row_ids;
  settings_window->num_rows = num_rows;
  settings_window->menu = NULL;
  settings_window->status = NULL;
  settings_window->highlight_color = GColorBlack;
#if TOUCH_INPUT
  double_tap_reset(&settings_window->double_tap);
#endif
  settings_window->window = window_create();
  window_set_user_data(settings_window->window, settings_window);
  window_set_window_handlers(settings_window->window, (WindowHandlers) {
    .load = settings_window_load,
    .unload = settings_window_unload,
  });
  return settings_window;
}



/*
 * destroy a previously created SettingsWindow
 */

void settings_window_destroy(SettingsWindow *settings_window) {
  if (settings_window != NULL) {
    window_destroy(settings_window->window);
    free(settings_window);
    return;
  }
  // error handling
  APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to free NULL SettingsWindow");
}



/*
 * push the window onto the stack
 */

void settings_window_push(SettingsWindow *settings_window, bool animated) {
  window_stack_push(settings_window->window, animated);
}



/*
 * refresh the provided SettingsWindow
 */

void settings_window_refresh(SettingsWindow *settings_window) {
  if (settings_window->menu) {
    layer_mark_dirty(menu_layer_get_layer(settings_window->menu));
  }
}



/*
 * set highlight color of this window
 * this is the overall color scheme used
 */

void settings_window_set_highlight_color(SettingsWindow *settings_window, GColor color) {
  settings_window->highlight_color = color;
  if (settings_window->menu) {
    menu_layer_set_highlight_colors(settings_window->menu, color, gcolor_legible_over(color));
  }
}

#endif  // PBL_PLATFORM_APLITE
