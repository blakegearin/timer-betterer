/*******************************************************************************
 * FILENAME :        menu_window.c
 *
 * DESCRIPTION :
 *      Create, destroy, and manage a MenuWindow to display
 *      a list of CountdownTimers
 *
 * PUBLIC FUNCTIONS :
 *      MenuWindow  *menu_window_create(MenuWindowCallbacks
 *                      menu_window_callbacks, bool animated);
 *      void        menu_window_destroy(MenuWindow *menu_window);
 *      bool        menu_window_get_topmost_window(MenuWindow *menu_window);
 *      void        menu_window_refresh(MenuWindow *menu_window);
 *      void        menu_window_reload_data(MenuWindow *menu_window);
 *      void        menu_window_select_row(MenuWindow *menu_window,
 *                      uint8_t row);
 *      void        menu_window_select_timer_index(MenuWindow *menu_window,
 *                      uint8_t view_index);
 *      MenuRowKind menu_window_row_kind(MenuWindow *menu_window,
 *                      uint8_t row);
 *      int16_t     menu_window_row_to_timer_index(MenuWindow *menu_window,
 *                      uint8_t row);
 *      int16_t     menu_window_row_to_setting_index(MenuWindow *menu_window,
 *                      uint8_t row);
 *      void        menu_window_set_wrap_around(MenuWindow *menu_window,
 *                      bool wrap_around);
 *      void        menu_window_set_highlight_color(MenuWindow *menu_window,
 *                      GColor color);
 *
 * AUTHOR :     Eric Phillips        START DATE :    07/10/15
 *
 */

#include <pebble.h>
#include "menu_window.h"
#include "countdown_timer.h"
#include "menu_list.h"
#include "settings.h"
#include "touch.h"

// Constants
#ifdef PBL_ROUND
#define MENU_CELL_PROG_BORDER 20
#define MENU_CELL_CENTERED true
#define MENU_CELL_PROG_THICK 3
#else
#define MENU_CELL_PROG_BORDER 10
#define MENU_CELL_CENTERED false
#define MENU_CELL_PROG_THICK 2
#endif
#define MENU_CELL_TEXT_Y_BUFF_RATIO 0.2
#define MENU_LAYER_DEFAULT_CELL_HEIGHT 52
#define MENU_LAYER_SELECTED_CELL_HEIGHT 65
// Hold-to-repeat on the two scrolling buttons, at the rate the duration picker's
// selection layer already uses: this list can be fifteen rows long on aplite, so
// holding a button down is the normal way to travel it.
#define MENU_BUTTON_REPEAT_MS 100



/*******************************************************************************
 * STRUCTURE DEFINITION
 */

/*
 * the structure of a MenuWindow
 */

struct MenuWindow {
  Window      *window;    //< main window
  MenuLayer   *menu;      //< menu layer displaying timer list
  GBitmap     *play_icon, *pause_icon;    //< menu layer icons
  GBitmap     *settings_icon;             //< cog row icon, never created on aplite
  GColor      highlight_color;            //< accent of a highlighted row; the icons
                                          //< must match it, and the menu layer keeps
                                          //< no copy a draw callback can reach
  StatusBarLayer      *status;            //< status bar for Basalt
  MenuWindowCallbacks callbacks;          //< menu layer callbacks
  bool wrap_around;                       //< whether the ends of the list wrap
#if TOUCH_INPUT
  DoubleTap double_tap;                   //< the tap-to-open memory
#endif
};



/*******************************************************************************
 * PRIVATE FUNCTIONS
 */

/*
 * get number of sections for menu layer
 * this is always zero for this application
 */

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *context) {
  return 1;
}



/*
 * row layout of the menu list, in one place
 *
 * rows are [0] the "+" add cell, [1..timer_count] the timers, and below them
 * either the permanent cog row or, on aplite, one permanent row per setting.
 * every site that maps between rows and timers classifies through
 * menu_window_row_kind.
 */

static uint8_t menu_get_timer_count(MenuWindow *menu_window) {
  return menu_window->callbacks.get_timer_count(menu_window);
}

static uint16_t menu_get_row_count(MenuWindow *menu_window) {
#ifdef PBL_PLATFORM_APLITE
  return menu_get_timer_count(menu_window) + 1 + SettingCount;
#else
  return menu_get_timer_count(menu_window) + 2;
#endif
}



/*
 * get number of rows per section for menu layer
 * since there is only one section, no need to take sections into account
 */

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index,
                                           void *context) {
  MenuWindow *menu_window = (MenuWindow*)context;
  return menu_get_row_count(menu_window);
}



// Return height of each menu layer cell
// Allows currently selected cell to be larger
static int16_t menu_get_row_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void
*context) {
  if (menu_layer_get_selected_index(menu_layer).row == cell_index->row) {
    return MENU_LAYER_SELECTED_CELL_HEIGHT;
  } else {
    return MENU_LAYER_DEFAULT_CELL_HEIGHT;
  }
}

// Draw a menu layer cell with optional text, image, and progress bar
// All items are centered as best as possible in all directions
static void menu_cell_draw(GContext *ctx, const Layer *layer, char *title, GBitmap *icon,
                           int32_t progress, const GFont font, bool center_text,
                           GColor col_fore, GColor col_back, GColor col_icon) {
  // calculate the relative sizes of the items
  GRect lay_bounds = layer_get_bounds(layer);
  GRect txt_bounds = GRectZero;
  GRect img_bounds = GRectZero;
  GRect prg_bounds = GRectZero;
  if (title) {
    txt_bounds.size = graphics_text_layout_get_content_size(title, font, lay_bounds,
      GTextOverflowModeFill, GTextAlignmentLeft);
  }
  if (icon) {
    img_bounds = gbitmap_get_bounds(icon);
  }
  if (progress) {
    prg_bounds.size = GSize(lay_bounds.size.w - MENU_CELL_PROG_BORDER * 2, MENU_CELL_PROG_THICK);
  }
  // draw the items centered in the layer
  if (title) {
    txt_bounds.origin.x = (lay_bounds.size.w - txt_bounds.size.w - img_bounds.size.w) / 2 *
      center_text + img_bounds.size.w;
    txt_bounds.origin.y = (lay_bounds.size.h - txt_bounds.size.h - prg_bounds.size.h) / 2 -
      txt_bounds.size.h * MENU_CELL_TEXT_Y_BUFF_RATIO;
    graphics_draw_text(ctx, title, font, txt_bounds, GTextOverflowModeFill, GTextAlignmentLeft,
      NULL);
  }
  if (icon) {
    img_bounds.origin.x = (lay_bounds.size.w - txt_bounds.size.w - img_bounds.size.w) / 2 *
      center_text;
    img_bounds.origin.y = (lay_bounds.size.h - img_bounds.size.h - prg_bounds.size.h) / 2;
#ifdef PBL_COLOR
    // the icons are 1-bit png-trans: the shape is black ink on a white field.
    // AND paints that ink black and leaves the field transparent -- right for
    // a white row, and for a highlighted row whose accent is light enough
    // that its legible colour is black. SET inks the same shape white, which
    // is what a dark accent needs -- the icon then matches the text colour
    // menu_layer_set_highlight_colors gave the row, and the two flip over
    // together at the same legibility threshold.
    graphics_context_set_compositing_mode(ctx, gcolor_equal(col_icon, GColorWhite) ?
      GCompOpSet : GCompOpAnd);
#else
    if (menu_cell_layer_is_highlighted(layer)) {
      graphics_context_set_compositing_mode(ctx, GCompOpAssignInverted);
    }
#endif
    graphics_draw_bitmap_in_rect(ctx, icon, img_bounds);
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  }
  if (progress) {
    // draw background
    int16_t h_max = txt_bounds.size.h > img_bounds.size.h ? txt_bounds.size.h : img_bounds.size.h;
    prg_bounds.origin.x = MENU_CELL_PROG_BORDER;
    prg_bounds.origin.y = (lay_bounds.size.h - prg_bounds.size.h - h_max) / 2 + h_max;
    graphics_context_set_fill_color(ctx, col_back);
    graphics_fill_rect(ctx, prg_bounds, 1, GCornersAll);
    // draw fill
    prg_bounds.size.w = prg_bounds.size.w * progress / TRIG_MAX_ANGLE;
    graphics_context_set_fill_color(ctx, col_fore);
    graphics_fill_rect(ctx, prg_bounds, 1, GCornersAll);
  }
}



/*
 * draw each row for menu layer
 */

/*
 * the colour a bitmap icon in a cell must be inked to stay legible: the
 * accent's legible colour on a highlighted row (the same value MenuLayer hands
 * the row's text), plain black on the white of a normal row. only consulted on
 * the colour platforms; aplite inverts the icon through the compositing mode.
 */
static GColor menu_cell_icon_color(MenuWindow *menu_window, const Layer *cell_layer) {
  return menu_cell_layer_is_highlighted(cell_layer) ?
    gcolor_legible_over(menu_window->highlight_color) : GColorBlack;
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index,
                                   void *context) {
  // get properties
  MenuWindow *menu_window = (MenuWindow *) context;
  // draw contents by row kind
  switch (menu_window_row_kind(menu_window, cell_index->row)) {
    case MenuRowAdd: {
      // bitham 42 light puts the "+" ink box near the 25 px cog it sits
      // beside, at a stroke weight that matches it; gothic caps out at 28,
      // whose "+" is only a 12 px ink box.
      menu_cell_draw(ctx, cell_layer, "+", NULL, 0, fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT),
        true, GColorBlack, GColorWhite, GColorBlack);
      break;
    }
    case MenuRowTimer: {
      CountdownTimer *countdown_timer = menu_window->callbacks.get_timer(cell_index->row - 1,
                                                                         context);
      char *buff = countdown_timer_format_own_buff(countdown_timer);
      GBitmap *icon = countdown_timer_get_paused(countdown_timer) ?
                      menu_window->pause_icon : menu_window->play_icon;
      GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      int32_t progress = TRIG_MAX_ANGLE * countdown_timer_get_current_time(countdown_timer) /
        countdown_timer_get_duration(countdown_timer);
      if (progress >= TRIG_MAX_ANGLE) {
        progress = 0;
      }
      GColor progress_bg_color = PBL_IF_COLOR_ELSE(GColorWhite, GColorLightGray);
      GColor progress_fg_color  = PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite);
      if (!menu_cell_layer_is_highlighted(cell_layer)) {
#ifdef PBL_BW
        progress_fg_color  = GColorBlack;
#else
        progress_bg_color = GColorLightGray;
#ifdef PBL_ROUND
        progress = 0;
#endif
#endif
      }
      menu_cell_draw(ctx, cell_layer, buff, icon, progress, font, MENU_CELL_CENTERED, progress_fg_color,
        progress_bg_color, menu_cell_icon_color(menu_window, cell_layer));
      break;
    }
    case MenuRowSettings: {
      // the row carries no label: the cog alone, centred the way the "+" row is.
      // it wants the "+" row's centring everywhere, not MENU_CELL_CENTERED.
      menu_cell_draw(ctx, cell_layer, NULL, menu_window->settings_icon, 0,
        fonts_get_system_font(FONT_KEY_GOTHIC_28), true, GColorBlack, GColorWhite,
        menu_cell_icon_color(menu_window, cell_layer));
      break;
    }
    case MenuRowSetting: {
      // two-line cell, bold name over current value, fonts and layout by the
      // system. no font handling on our side, on purpose.
      const int16_t setting = menu_window_row_to_setting_index(menu_window, cell_index->row);
      menu_cell_basic_draw(ctx, cell_layer,
        menu_window->callbacks.get_setting_name(setting, context),
        menu_window->callbacks.get_setting_value(setting, context), NULL);
      break;
    }
  }
}


/*
 * move the cursor by "offset" rows
 *
 * Wrap Around decides what a step past either end means, and the walk itself
 * is the one every menu list in this app shares (see menu_list.c).
 */

static void menu_move_selection(MenuWindow *menu_window, int16_t offset) {
  menu_list_step_selection(menu_window->menu, offset, menu_get_row_count(menu_window),
                           menu_window->wrap_around);
}



static void menu_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  menu_move_selection((MenuWindow*)context, -1);
}

static void menu_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  menu_move_selection((MenuWindow*)context, 1);
}

/*
 * the row the cursor is on is the row a click acts on. this is the callback the
 * MenuLayer's own config used to make: given the selected index, tell main.c
 * what kind of row it landed on.
 */

static void menu_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  MenuWindow *menu_window = (MenuWindow*)context;
  const uint8_t row = (uint8_t)menu_layer_get_selected_index(menu_window->menu).row;
  menu_window->callbacks.clicked(menu_window_row_kind(menu_window, row), row, menu_window);
}

/*******************************************************************************
 * TOUCH
 */

#if TOUCH_INPUT

/*
 * the system touch navigation moves the menu cursor to a tapped row and then
 * fires this callback for every tap, whatever moved; the physical SELECT
 * button never comes through here because menu_click_config_provider owns it,
 * so a click reaching this callback is always a finger. the first tap on a
 * row has already done its work -- the cursor is on it -- and only opens when
 * a second tap lands on the same row inside the double tap window, which is
 * what the shared DoubleTap memory (see touch.h) decides.
 */

static void menu_touch_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index,
                                       void *context) {
  MenuWindow *menu_window = (MenuWindow*)context;
  const uint16_t row = (uint16_t)cell_index->row;
  if (!double_tap_tap(&menu_window->double_tap, row)) {
    return;
  }
  menu_window->callbacks.clicked(menu_window_row_kind(menu_window, (uint8_t)row),
                                 (uint8_t)row, menu_window);
}

#endif  // TOUCH_INPUT



/*
 * UP and DOWN walk the list, repeating while held so the fifteen rows an aplite
 * list can hold are one press-and-hold away from either end; SELECT acts on the
 * row the cursor is on; BACK is left unclaimed, which is how a window keeps the
 * firmware's own pop-or-quit handling of it.
 */

static void menu_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, MENU_BUTTON_REPEAT_MS,
                                          menu_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, MENU_BUTTON_REPEAT_MS,
                                          menu_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, menu_select_click_handler);
}



/*
 * menu window initialize
 */

static MenuWindow *menu_window_init(MenuWindow *menu_window,
                                    MenuWindowCallbacks menu_window_callbacks, bool animated) {
  // load resources
  menu_window->highlight_color = GColorBlack;
  menu_window->wrap_around = false;
  menu_window->settings_icon = NULL;
  menu_window->play_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PLAY_TRANS_WHITE);
  menu_window->pause_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PAUSE_TRANS_WHITE);
#ifndef PBL_PLATFORM_APLITE
  // aplite has no cog row, so it must not pay the bitmap's heap cost
  menu_window->settings_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SETTINGS_WHITE);
#endif
  // create window
  menu_window->window = window_create();
  menu_window->callbacks = menu_window_callbacks;
#if TOUCH_INPUT
  double_tap_reset(&menu_window->double_tap);
#endif
#ifdef PBL_PLATFORM_APLITE
  bool icons_ok = (menu_window->play_icon && menu_window->pause_icon);
#else
  bool icons_ok = (menu_window->play_icon && menu_window->pause_icon && menu_window->settings_icon);
#endif
  if (icons_ok && menu_window->window) {
    // get window parameters
    Layer *root = window_get_root_layer(menu_window->window);
    GRect bounds = layer_get_frame(root);
    // create menu layer
#ifdef PBL_ROUND
    menu_window->menu = menu_layer_create(bounds);
#else
    menu_window->menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
                                          bounds.size.h - STATUS_BAR_LAYER_HEIGHT));
#endif
#ifdef PBL_ROUND
    menu_layer_set_center_focused(menu_window->menu, true);
#endif
    menu_layer_set_callbacks(menu_window->menu, menu_window, (MenuLayerCallbacks) {
      .get_num_sections = menu_get_num_sections_callback,
      .get_num_rows = menu_get_num_rows_callback,
      .draw_row = menu_draw_row_callback,
#ifdef PBL_ROUND
      .get_cell_height = menu_get_row_height_callback,
#endif
#if TOUCH_INPUT
      .select_click = menu_touch_select_callback,
#endif
    });
    // not menu_layer_set_click_config_onto_window: its cursor stops at the ends
    // of the list, and Wrap Around is about those ends. see the provider above.
    window_set_click_config_provider_with_context(menu_window->window,
                                                 menu_click_config_provider, menu_window);
    layer_add_child(root, menu_layer_get_layer(menu_window->menu));
    // create status bar
    menu_window->status = status_bar_layer_create();
    status_bar_layer_set_colors(menu_window->status, GColorClear, GColorBlack);
    layer_add_child(root, status_bar_layer_get_layer(menu_window->status));
    // push window
    window_stack_push(menu_window->window, animated);
    return menu_window;
  }

  // free menu window
  free(menu_window);
  return NULL;
}



/*******************************************************************************
 * API FUNCTIONS
 */

/*
 * create a new MenuWindow and return a pointer to it
 * this includes creating all its children layers
 */

MenuWindow *menu_window_create(MenuWindowCallbacks menu_window_callbacks, bool animated) {
  MenuWindow *menu_window = (MenuWindow*)malloc(sizeof(MenuWindow));
  if (menu_window) {
    menu_window_init(menu_window, menu_window_callbacks, animated);
  } else {
    // error handling
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create MenuWindow");
    return NULL;
  }
  return menu_window;
}



/*
 * destroy a previously created MenuWindow
 */

void menu_window_destroy(MenuWindow *menu_window) {
  if (menu_window != NULL) {
    status_bar_layer_destroy(menu_window->status);
    menu_layer_destroy(menu_window->menu);
    window_destroy(menu_window->window);
    gbitmap_destroy(menu_window->play_icon);
    gbitmap_destroy(menu_window->pause_icon);
#ifndef PBL_PLATFORM_APLITE
    gbitmap_destroy(menu_window->settings_icon);
#endif
    free(menu_window);
    return;
  }
  // error handling
  APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to free NULL MenuWindow");
}



/*
 * gets whether it is the topmost window on the stack
 */

bool menu_window_get_topmost_window(MenuWindow *menu_window) {
  return window_stack_get_top_window() == menu_window->window;
}



/*
 * refresh the provided MenuWindow
 */

void menu_window_refresh(MenuWindow *menu_window) {
  layer_mark_dirty(menu_layer_get_layer(menu_window->menu));
}



/*
 * reload the data for the MenuWindow's MenuLayer
 */

void menu_window_reload_data(MenuWindow *menu_window) {
  // this makes the selected index go back to the top. that is desired after a
  // delete, where the list really changed and "+" is the only survivor; the
  // promote paths in main.c now override it, putting the cursor on the
  // last-used timer afterwards via menu_window_select_timer_index.
  menu_layer_reload_data(menu_window->menu);
}



/*
 * select a row in the menu list
 */

void menu_window_select_row(MenuWindow *menu_window, uint8_t row) {
  if (row >= menu_get_row_count(menu_window)) {
    return;
  }
  menu_layer_set_selected_index(menu_window->menu, (MenuIndex) { .section = 0, .row = row },
    MenuRowAlignCenter, false);
}



/*
 * select the row displaying a given timer
 */

void menu_window_select_timer_index(MenuWindow *menu_window, uint8_t view_index) {
  menu_window_select_row(menu_window, view_index + 1);
}



/*
 * get what a menu row holds
 */

MenuRowKind menu_window_row_kind(MenuWindow *menu_window, uint8_t row) {
  if (row == 0) {
    return MenuRowAdd;
  }
  if (row <= menu_get_timer_count(menu_window)) {
    return MenuRowTimer;
  }
#ifdef PBL_PLATFORM_APLITE
  return MenuRowSetting;
#else
  return MenuRowSettings;
#endif
}



/*
 * map a menu row onto its timer, or -1 if the row does not hold one
 */

int16_t menu_window_row_to_timer_index(MenuWindow *menu_window, uint8_t row) {
  if (menu_window_row_kind(menu_window, row) != MenuRowTimer) {
    return -1;
  }
  return (int16_t)row - 1;
}



/*
 * map an aplite settings row onto its setting, or -1 if the row does not hold
 * one. off aplite the classifier never returns MenuRowSetting, so this always
 * answers -1 there.
 */

int16_t menu_window_row_to_setting_index(MenuWindow *menu_window, uint8_t row) {
  if (menu_window_row_kind(menu_window, row) != MenuRowSetting) {
    return -1;
  }
  return (int16_t)(row - menu_get_timer_count(menu_window) - 1);
}



/*
 * set highlight color of this window
 * this is the overall color scheme used
 */

void menu_window_set_highlight_color(MenuWindow *menu_window, GColor color) {
  menu_window->highlight_color = color;
  menu_layer_set_highlight_colors(menu_window->menu, color, gcolor_legible_over(color));
}



/*
 * turn Wrap Around on or off for this window's cursor
 */

void menu_window_set_wrap_around(MenuWindow *menu_window, bool wrap_around) {
  menu_window->wrap_around = wrap_around;
}
