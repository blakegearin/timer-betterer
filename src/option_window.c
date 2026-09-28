/*******************************************************************************
 * FILENAME :        option_window.c
 *
 * DESCRIPTION :
 *      Create, destroy, and manage a reusable radio option window: on rect,
 *      a left label with a hand-drawn selection circle because the
 *      firmware's own radio resource is not exported to apps; on round, a
 *      centred label with no circle, the committed option marked by weight.
 *
 * PUBLIC FUNCTIONS :
 *      OptionWindow  *option_window_create(OptionWindowSelectCallback
 *                          selected, void *context);
 *      void          option_window_destroy(OptionWindow *option_window);
 *      void          option_window_push(OptionWindow *option_window,
 *                          const char *title, const char *const *labels,
 *                          uint8_t count, uint8_t selected,
 *                          const GColor *swatches, bool animated);
 *      void          option_window_set_highlight_color(OptionWindow
 *                          *option_window, GColor color);
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-24
 *
 */

#include <pebble.h>
#include "option_window.h"
#include "touch.h"

/*
 * Excluded on aplite, which flips its settings inline and cannot afford this
 * file's .text. See settings_window.c for why the file stays in the build.
 */
#ifndef PBL_PLATFORM_APLITE

// Firmware geometry: a 14px outer circle with a 2px ring and a
// 6px filled centre, set in from the right edge by 7px on rect and 10 on
// emery. There is no SDK helper and no resource for any of it. Round draws
// no circle at all -- see option_draw_row_callback -- so this is rect-only.
#define OPTION_RADIO_RADIUS 7
#define OPTION_RADIO_DOT_RADIUS 3
#ifdef PBL_PLATFORM_EMERY
#define OPTION_RADIO_INSET 10
#else
#define OPTION_RADIO_INSET 7
#endif
#define OPTION_ROUND_TEXT_LEFT_INSET 20
#define OPTION_RECT_TEXT_LEFT_INSET 6  //< the inset menu_cell_basic_draw uses

// same round menu-cell constants as the settings window; without a
// get_cell_height the rows fall to MenuLayer's 44 px default, which centres
// neither the label nor the circle on the firmware metric
#ifdef PBL_ROUND
#define OPTION_CELL_HEIGHT_FOCUSED 68
#define OPTION_CELL_HEIGHT 32
#endif

/*******************************************************************************
 * STRUCTURE DEFINITION
 */

/*
 * the structure of an OptionWindow
 *
 * one window re-pointed at one setting per push, not one window per setting;
 * the firmware's own Text Size helper has essentially this shape
 */

struct OptionWindow {
  Window      *window;    //< main window
  MenuLayer   *menu;      //< menu layer displaying the options
  StatusBarLayer *status; //< status bar
  OptionWindowSelectCallback selected; //< selection callback
  void        *context;   //< callback context
  const char  *title;     //< window title, drawn as a section header on rect
  const char *const *labels; //< the option labels, owned by the caller
  const GColor  *swatches;   //< one colour per label, or NULL; owned by the caller
  uint8_t     count;      //< number of labels
  uint8_t     selected_option; //< which option currently carries the filled dot
  GColor      highlight_color; //< main color for highlights
  GColor      row_highlight;   //< live highlight of the focused row: the theme
                               //   colour, or the swatch being previewed on
                               //   the colour screen
#if TOUCH_INPUT
  DoubleTap double_tap;        //< tap-to-open debounce
#endif
};



/*******************************************************************************
 * PRIVATE FUNCTIONS
 */

/*
 * get number of sections for menu layer
 * this is always one for this window
 */

static uint16_t option_get_num_sections_callback(MenuLayer *menu_layer, void *context) {
  return 1;
}



/*
 * get number of rows for menu layer, one per option
 */

static uint16_t option_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index,
                                             void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  return option_window->count;
}



/*
 * the window title is a section header on rect and nothing on round
 * same rule, and same reason, as the settings window
 */

static int16_t option_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index,
                                                 void *context) {
  return PBL_IF_RECT_ELSE(MENU_CELL_BASIC_HEADER_HEIGHT, 0);
}

static void option_draw_header_callback(GContext *ctx, const Layer *cell_layer,
                                        uint16_t section_index, void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  menu_cell_basic_header_draw(ctx, cell_layer, option_window->title);
}



/*
 * the menu cell title font the theme would have picked
 *
 * hand-drawing the label means losing the font menu_cell_basic_draw would
 * have chosen for us, and hardcoding one breaks the Large platforms --
 * emery on rect, gabbro on round. resolve it from preferred_content_size()
 * instead, the way system_theme.c does. The bold flag picks between a size
 * and its bold twin; wherever a row has no radio -- every round row and the
 * rect colour screen -- that twin is the only thing marking which option is
 * committed, so every size must honour it -- including 28.
 */

static GFont option_title_font(bool bold) {
  switch (preferred_content_size()) {
    case PreferredContentSizeSmall:
      return fonts_get_system_font(bold ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_18);
    case PreferredContentSizeLarge:
    case PreferredContentSizeExtraLarge:
      return fonts_get_system_font(bold ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_28);
    default:
      return fonts_get_system_font(bold ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_24);
  }
}


#ifdef PBL_ROUND
/*
 * the vertical room one line of the title font actually takes
 *
 * graphics_text_layout_get_content_size answers with the line ADVANCE, and
 * for the system Gothic faces the renderer puts the baseline right at the
 * bottom of that advance -- capitals and descenders both overflow the
 * reported box (the g in "Bulgarian Rose" reaches ~6 px past it). So the
 * advance alone is neither a centring basis nor a cell height: measure it
 * as the height a second line adds, and give rows advance + 12 px, which is
 * what it takes for the full ink of 18/24/28 pt Gothic to sit inside a cell
 * without the row boundary cutting tails off.
 */

#define OPTION_LINE_SLACK 12  //< px around the line advance for capital + descender ink

static int16_t option_line_pitch(GFont font) {
  const GRect probe = GRect(0, 0, 64, 200);
  const int16_t one = graphics_text_layout_get_content_size(
    "Ag", font, probe, GTextOverflowModeFill, GTextAlignmentLeft).h;
  const int16_t two = graphics_text_layout_get_content_size(
    "Ag\nAg", font, probe, GTextOverflowModeFill, GTextAlignmentLeft).h;
  const int16_t pitch = two - one;
  return (pitch > one) ? pitch : one;
}
#endif



/*
 * draw the radio selection circle
 *
 * two concentric strokes make the firmware's 2px ring; the filled dot marks
 * the selected option. colours follow the highlight, never a literal
 * GColorBlack, which would disappear on a highlighted row.
 *
 * only the plain settings on rect call this -- round has no circles at all
 * (see option_draw_row_callback), and the colour screen never had one: its
 * focused row is painted in the colour it names and the committed value is
 * the bold label. See option_draw_label.
 */

#ifndef PBL_ROUND
static void option_draw_radio(GContext *ctx, const Layer *cell_layer,
                              const OptionWindow *option_window, uint8_t row) {
  const GRect bounds = layer_get_bounds(cell_layer);
  // Gothic capital ink sits a few px below the middle of the line box it was
  // measured in, so a ring centred on the cell geometry reads as riding above
  // the label. Rect uses menu_cell_basic_draw, whose own centring already
  // agrees with ours.
  const int16_t center_y = bounds.size.h / 2;
  const GPoint center = GPoint(bounds.size.w - OPTION_RADIO_INSET - OPTION_RADIO_RADIUS,
                               center_y);
  const GColor color = menu_cell_layer_is_highlighted(cell_layer) ?
                       gcolor_legible_over(option_window->row_highlight) : GColorBlack;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_draw_circle(ctx, center, OPTION_RADIO_RADIUS);
  graphics_draw_circle(ctx, center, OPTION_RADIO_RADIUS - 1);
  if (row == option_window->selected_option) {
    graphics_fill_circle(ctx, center, OPTION_RADIO_DOT_RADIUS);
  }
}
#endif  // PBL_ROUND



/*
 * draw one hand-drawn label
 *
 * this is every row on round and the colour screen on rect. With no radio
 * (round dropped them; the colour screen never had one), weight is the only
 * thing marking which option is committed: bold for it, regular for the
 * rest, while the highlighted band says where the cursor is.
 * menu_cell_basic_draw is always-bold -- it drew these titles bold on every
 * row -- so the label is hand-drawn instead. On round there is no circle,
 * so no gutter to reserve either; the three-word colour names live at the
 * end of the rainbow where they still mostly fit, and ellipsize rather than
 * clip if they do not.
 */

static void option_draw_label(GContext *ctx, const Layer *cell_layer,
                              const OptionWindow *option_window, uint8_t row) {
  const GRect cell = layer_get_bounds(cell_layer);
  const char *label = option_window->labels[row];
  const GFont font = option_title_font(row == option_window->selected_option);
#ifdef PBL_ROUND
  const GRect text_box = GRect(OPTION_ROUND_TEXT_LEFT_INSET, 0,
                               cell.size.w - 2 * OPTION_ROUND_TEXT_LEFT_INSET, cell.size.h);
  const GTextAlignment align = GTextAlignmentCenter;
#else
  const GRect text_box = GRect(OPTION_RECT_TEXT_LEFT_INSET, 0,
    cell.size.w - OPTION_RECT_TEXT_LEFT_INSET - OPTION_RADIO_INSET, cell.size.h);
  const GTextAlignment align = GTextAlignmentLeft;
#endif
#ifdef PBL_ROUND
  const int16_t line_h = option_line_pitch(font);
#else
  const GSize used = graphics_text_layout_get_content_size(label, font, text_box,
                                                           GTextOverflowModeTrailingEllipsis,
                                                           align);
  const int16_t line_h = used.h;
#endif
  const GRect text = GRect(text_box.origin.x, (cell.size.h - line_h) / 2,
                           text_box.size.w, line_h);
  graphics_context_set_text_color(ctx, menu_cell_layer_is_highlighted(cell_layer) ?
                                  gcolor_legible_over(option_window->row_highlight) :
                                  GColorBlack);
  graphics_draw_text(ctx, label, font, text, GTextOverflowModeTrailingEllipsis, align, NULL);
}



/*
 * draw each row
 *
 * on rect: the plain settings get menu_cell_basic_draw's left label plus
 * the selection circle, the colour screen just a hand-drawn label. On round
 * every row is the hand-drawn centred label -- the right-aligned text with
 * the circle parked at its end read as jarring beside the curved edge, so
 * the colour screen's circle-less style is the window's one style there.
 */

static void option_draw_row_callback(GContext *ctx, const Layer *cell_layer,
                                     MenuIndex *cell_index, void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  const uint8_t row = (uint8_t)cell_index->row;

#ifdef PBL_ROUND
  option_draw_label(ctx, cell_layer, option_window, row);
  return;
#else
  if (option_window->swatches != NULL) {
    option_draw_label(ctx, cell_layer, option_window, row);
    return;
  }
  menu_cell_basic_draw(ctx, cell_layer, option_window->labels[row], NULL, NULL);
  option_draw_radio(ctx, cell_layer, option_window, row);
#endif
}



/*
 * TOUCH
 *
 * same arrangement as the settings window (see settings_window.c): on touch
 * the window installs its own click config, because the MenuLayer's would
 * route the physical SELECT through the debounced select_click as well. the
 * provider's select acts at once; the debounced callback below only ever sees
 * fingers. The walk is the shared one from menu_list.c, clamped like the
 * settings menu's.
 */

#if TOUCH_INPUT

#include "menu_list.h"

#define OPTION_BUTTON_REPEAT_MS 100

static void option_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  menu_list_step_selection(option_window->menu, -1, option_window->count, false);
}

static void option_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  menu_list_step_selection(option_window->menu, 1, option_window->count, false);
}

static void option_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  const uint8_t option = (uint8_t)menu_layer_get_selected_index(option_window->menu).row;
  // a button acts at once; a half-finished tap on this row must not pair
  // with anything that comes after it
  double_tap_reset(&option_window->double_tap);
  window_stack_remove(option_window->window, true);
  option_window->selected(option, option_window->context);
}

static void option_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, OPTION_BUTTON_REPEAT_MS,
                                          option_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, OPTION_BUTTON_REPEAT_MS,
                                          option_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, option_select_click_handler);
}

#endif  // TOUCH_INPUT



/*
 * menu layer clicked callback
 *
 * selecting pops immediately, no BACK and no Submit row -- follow the
 * firmware, not the ui-patterns example. The pop happens before the callback
 * so the callback can refresh the now-topmost settings window and the user
 * sees the new value already drawn under the setting's name.
 *
 * on touch this callback is reached only by a finger (the buttons ride the
 * provider above), and only the second tap on the same row commits.
 */

static void option_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  const uint8_t option = (uint8_t)cell_index->row;
#if TOUCH_INPUT
  if (!double_tap_tap(&option_window->double_tap, (uint16_t)option)) {
    return;
  }
#endif
  window_stack_remove(option_window->window, true);
  option_window->selected(option, option_window->context);
}



/*
 * live colour preview
 *
 * on the colour screen the focused row's background is the colour it names,
 * so scrolling through the rainbow shows the choice at row size -- the only
 * way to tell Icterine from Pastel Yellow before committing. This only
 * repaints the picker: the app's theme colour changes on selection, nowhere
 * earlier, so a BACK leaves nothing applied. The committed value stays findable
 * while the cursor wanders: its label is the bold one.
 */

static void option_selection_changed_callback(MenuLayer *menu_layer, MenuIndex new_index,
                                              MenuIndex old_index, void *context) {
  OptionWindow *option_window = (OptionWindow*)context;
  if (option_window->swatches == NULL) {
    return;
  }
  option_window->row_highlight = option_window->swatches[new_index.row];
  menu_layer_set_highlight_colors(menu_layer, option_window->row_highlight,
                                  gcolor_legible_over(option_window->row_highlight));
}

#ifdef PBL_ROUND
static int16_t option_get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index,
                                               void *context) {
  if (menu_layer_get_selected_index(menu_layer).row == cell_index->row) {
    return OPTION_CELL_HEIGHT_FOCUSED;
  }
  // a one-line cell has to hold capital + descender ink, not just the line
  // advance -- see option_line_pitch (issue: the cut-off g in "Bulgarian
  // Rose" on the 28 px platforms; chalk's 32 px firmware metric was already
  // a pixel or two short at 24 px)
  const int16_t pitch = option_line_pitch(option_title_font(false));
  const int16_t needed = pitch + OPTION_LINE_SLACK;
  return (needed > OPTION_CELL_HEIGHT) ? needed : OPTION_CELL_HEIGHT;
}
#endif



/*
 * window load
 */

static void option_window_load(Window *window) {
  OptionWindow *option_window = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_frame(root);

#ifdef PBL_ROUND
  option_window->menu = menu_layer_create(bounds);
#else
  option_window->menu = menu_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
                                                bounds.size.h - STATUS_BAR_LAYER_HEIGHT));
#endif
  MenuLayerCallbacks callbacks = {
    .get_num_sections = option_get_num_sections_callback,
    .get_num_rows = option_get_num_rows_callback,
    .get_header_height = option_get_header_height_callback,
    .draw_header = option_draw_header_callback,
    .draw_row = option_draw_row_callback,
    .select_click = option_select_callback,
    .selection_changed = option_selection_changed_callback,
#ifdef PBL_ROUND
    .get_cell_height = option_get_cell_height_callback,
#endif
  };
  menu_layer_set_callbacks(option_window->menu, option_window, callbacks);
#if TOUCH_INPUT
  window_set_click_config_provider_with_context(window, option_click_config_provider,
                                                option_window);
#else
  menu_layer_set_click_config_onto_window(option_window->menu, window);
#endif
  menu_layer_set_highlight_colors(option_window->menu, option_window->row_highlight,
                                  gcolor_legible_over(option_window->row_highlight));
  layer_add_child(root, menu_layer_get_layer(option_window->menu));
  // open on the option that is currently selected, like the system does
  menu_layer_set_selected_index(option_window->menu,
    (MenuIndex) { .section = 0, .row = option_window->selected_option },
    MenuRowAlignCenter, false);

  option_window->status = status_bar_layer_create();
  status_bar_layer_set_colors(option_window->status, GColorClear, GColorBlack);
  layer_add_child(root, status_bar_layer_get_layer(option_window->status));
}

/*
 * window unload
 */

static void option_window_unload(Window *window) {
  OptionWindow *option_window = window_get_user_data(window);
  status_bar_layer_destroy(option_window->status);
  option_window->status = NULL;
  menu_layer_destroy(option_window->menu);
  option_window->menu = NULL;
}



/*******************************************************************************
 * API FUNCTIONS
 */

/*
 * create a new OptionWindow and return a pointer to it
 * the window itself is created now, its layers only while it is on screen
 */

OptionWindow *option_window_create(OptionWindowSelectCallback selected, void *context) {
  OptionWindow *option_window = (OptionWindow*)malloc(sizeof(OptionWindow));
  if (option_window == NULL) {
    // error handling
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to create OptionWindow");
    return NULL;
  }
  option_window->selected = selected;
  option_window->context = context;
  option_window->menu = NULL;
  option_window->status = NULL;
  option_window->title = NULL;
  option_window->labels = NULL;
  option_window->swatches = NULL;
  option_window->count = 0;
  option_window->selected_option = 0;
  option_window->highlight_color = GColorBlack;
  option_window->row_highlight = GColorBlack;
#if TOUCH_INPUT
  double_tap_reset(&option_window->double_tap);
#endif
  option_window->window = window_create();
  window_set_user_data(option_window->window, option_window);
  window_set_window_handlers(option_window->window, (WindowHandlers) {
    .load = option_window_load,
    .unload = option_window_unload,
  });
  return option_window;
}



/*
 * destroy a previously created OptionWindow
 */

void option_window_destroy(OptionWindow *option_window) {
  if (option_window != NULL) {
    window_destroy(option_window->window);
    free(option_window);
    return;
  }
  // error handling
  APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to free NULL OptionWindow");
}



/*
 * re-point the window at one setting and push it onto the stack
 */

void option_window_push(OptionWindow *option_window, const char *title,
                        const char *const *labels, uint8_t count, uint8_t selected,
                        const GColor *swatches, bool animated) {
  option_window->title = title;
  option_window->labels = labels;
  option_window->swatches = swatches;
  option_window->count = count;
  option_window->selected_option = selected;
  // open already previewing the current colour, so the picker agrees with
  // the settings row behind it before the user moves the cursor at all
  option_window->row_highlight = (swatches != NULL) ? swatches[selected]
                                                    : option_window->highlight_color;
  window_stack_push(option_window->window, animated);
}



/*
 * set highlight color of this window
 * this is the overall color scheme used
 */

void option_window_set_highlight_color(OptionWindow *option_window, GColor color) {
  option_window->highlight_color = color;
  // a live swatch preview owns the focused row until the next push
  if (option_window->menu && option_window->swatches == NULL) {
    option_window->row_highlight = color;
    menu_layer_set_highlight_colors(option_window->menu, color, gcolor_legible_over(color));
  }
}

#endif  // PBL_PLATFORM_APLITE
