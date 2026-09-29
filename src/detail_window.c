/*******************************************************************************
 * FILENAME :        detail_window.c
 *
 * DESCRIPTION :
 *      Display a timer with controls to modify it
 *
 * PUBLIC FUNCTIONS :
 *      DetailWindow    *detail_window_create(
 *                          DetailWindowCallbacks detail_window_callbacks);
 *      void            detail_window_destroy(DetailWindow *detail_window);
 *      void            detail_window_push(DetailWindow *detail_window,
 *                          bool animated);
 *      void            detail_window_pop(DetailWindow *detail_window,
 *                          bool animated);
 *      bool            detail_window_get_topmost_window(DetailWindow
 *                          *detail_window);
 *      void            detail_window_set_countdown_timer(DetailWindow
 *                          *detail_window, CountdownTimer *countdown_timer);
 *      void            detail_window_refresh(DetailWindow *detail_window);
 *      void            detail_window_deep_refresh(DetailWindow *detail_window);
 *      void            detail_window_set_highlight_color(DetailWindow
 *                          *detail_window, GColor color);
 *      void            detail_window_set_delete_immediately(DetailWindow
 *                          *detail_window, bool immediately);
 *      bool            detail_window_get_update_needed(DetailWindow
 *                          *detail_window);
 *
 * NOTES :      The actual timer structure definition is not exposed to
 *              prevent direct modification of the structure.
 *
 * AUTHOR :     Eric Phillips        START DATE :    07/11/15
 *
 */

#include <pebble.h>
#include "detail_window.h"
#include "countdown_timer.h"
#include "touch.h"

#define TEXT_LAYER_MAX_LARGE_CHARACTERS 5
#define MSEC_IN_SEC 1000
#define DELETE_ARM_TIMEOUT_MS 2500  //< how long an armed delete stays armed

/*******************************************************************************
 * STRUCTURE DEFINITION
 */

/*
 * the structure of a DetailWindow
 */

struct DetailWindow {
  Window      *window;    //< main window
  Layer       *layer;     //< drawing layer for the accent water block
  /*
   * on the rect colour platforms the two texts are drawn twice, the copies
   * parented to clip containers that meet exactly at the waterline: above it
   * the ink is black on the exposed grey, below it the accent's legible
   * colour on the block. the other platforms keep the single layers and flip
   * their ink instead (round, by where the wedge reaches) or need no flip at
   * all (black ink suits both grounds in black and white).
   */
  Layer       *above, *below;              //< the clip containers
  TextLayer   *main_text, *sub_text;       //< the above-water copies
  TextLayer   *main_below, *sub_below;     //< the below-water copies
  GRect       main_bounds, sub_bounds;     //< where the texts are laid out
  GTextAlignment main_align, sub_align;    //< how they are aligned in them
  ActionBarLayer *action; //< action bar
  GBitmap     *edit_icon, *play_icon, *pause_icon, *delete_icon, *dismiss_icon;  //< icons
  GFont       large_font, medium_font, small_font; //< fonts
  GColor      highlight_color;        //< main color for highlights
  StatusBarLayer *status;             //< status bar for SDK 3
  DetailWindowCallbacks callbacks;    //< callbacks for button presses

  char        main_buff[12];          //< the digits, as drawn by the layer
  char        sub_buff[12];           //< the footer, as drawn by the layer

  bool        animation_update_needed;    //< whether it needs to be refreshed

  CountdownTimer *countdown_timer;        //< the CountdownTimer being shown

  bool        delete_armed;               //< whether delete needs confirmation
  AppTimer   *delete_arm_timer;           //< timer to clear confirmation state
  bool        delete_immediately;         //< Confirm Deletion: Off, so skip arming
};

/*******************************************************************************
 * PRIVATE FUNCTIONS
 */

/*
 * the ink that stays legible over the accent. aplite pops to no accent at
 * all, and black suits its white ground.
 */
static GColor prv_legible_over(GColor accent) {
  return PBL_IF_COLOR_ELSE(gcolor_legible_over(accent), GColorBlack);
}

/*
 * only the rect colour platforms draw the texts twice: they have the ground
 * split (grey above, accent below) and the clips to express it with. aplite's
 * white suits one black ink and round's wedge, a pie, no horizontal clip
 * follows, so round flips its single ink where the wedge passes the texts.
 */
#if defined(PBL_COLOR) && !defined(PBL_ROUND)
#define WATER_SPLIT
#endif

/*
 * the timer's clock and total, and whether there is water to show at all.
 * drained, with no timer or a zero-length one, means the level sits at the
 * bottom: plain grey everywhere.
 */
static bool prv_times(DetailWindow *detail_window, int64_t *current, int64_t *total) {
  if (detail_window->countdown_timer != NULL) {
    *current = countdown_timer_get_display_time(detail_window->countdown_timer);
    *total = countdown_timer_get_duration(detail_window->countdown_timer);
    return *total > 0;
  }
  *current = 0;
  *total = 1;
  return false;
}

#if !defined(PBL_ROUND)
/*
 * the y of the waterline: below it the accent, above it exposed grey
 */
static int16_t prv_water_level(DetailWindow *detail_window, GRect bounds) {
  int64_t current, total;
  if (!prv_times(detail_window, &current, &total)) {
    return bounds.size.h;
  }
  int16_t level = (int16_t)(bounds.size.h - bounds.size.h * current / total);
  return level < 0 ? 0 : (level > bounds.size.h ? bounds.size.h : level);
}
#endif

/*
 * both copies of both texts in their fonts: the digits drop to medium once
 * they outgrow the large face, and the footer borrows a system bold while the
 * delete arms, so the '?' it asks for is always present
 */
static void prv_apply_fonts(DetailWindow *detail_window) {
  GFont main_font = strlen(detail_window->main_buff) > TEXT_LAYER_MAX_LARGE_CHARACTERS
    ? detail_window->medium_font : detail_window->large_font;
  GFont sub_font = detail_window->delete_armed
    ? fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD) : detail_window->small_font;
  text_layer_set_font(detail_window->main_text, main_font);
  text_layer_set_font(detail_window->sub_text, sub_font);
#ifdef WATER_SPLIT
  text_layer_set_font(detail_window->main_below, main_font);
  text_layer_set_font(detail_window->sub_below, sub_font);
#endif
}

/*
 * move the two clip containers so they meet at the waterline, sliding each
 * below-water copy back into pixel-exact register with its above-water twin.
 * the copies are pixel-identical layouts of the same text, so the split line
 * crossing a glyph just changes which half is whose ink.
 */
#ifdef WATER_SPLIT
static void prv_update_split(DetailWindow *detail_window) {
  GRect bounds = layer_get_bounds(detail_window->layer);
  int16_t level = prv_water_level(detail_window, bounds);
  layer_set_frame(detail_window->above, GRect(0, 0, bounds.size.w, level));
  layer_set_frame(detail_window->below,
                  GRect(0, level, bounds.size.w, bounds.size.h - level));
  layer_set_frame(text_layer_get_layer(detail_window->main_below),
    GRect(detail_window->main_bounds.origin.x,
          detail_window->main_bounds.origin.y - level,
          detail_window->main_bounds.size.w, detail_window->main_bounds.size.h));
  layer_set_frame(text_layer_get_layer(detail_window->sub_below),
    GRect(detail_window->sub_bounds.origin.x,
          detail_window->sub_bounds.origin.y - level,
          detail_window->sub_bounds.size.w, detail_window->sub_bounds.size.h));
}
#endif

/*
 * the round platform's single copies flip ink with the wedge. neither string
 * sits on a single bearing -- the digits span across the disc centre -- so
 * each flips where the wedge edge crosses its middle, which for both the
 * centred strings is due south: a text reads in the accent's colour while the
 * wedge still covers more than half the dial it lives on, and in black once
 * the grey is the majority ground under it.
 */
#ifdef PBL_ROUND
static void prv_update_inks(DetailWindow *detail_window) {
  int64_t current, total;
  bool water = prv_times(detail_window, &current, &total);
  GColor accent_ink = prv_legible_over(detail_window->highlight_color);
  text_layer_set_text_color(detail_window->main_text,
    water && current * 2 >= total ? accent_ink : GColorBlack);
  text_layer_set_text_color(detail_window->sub_text,
    water && current * 2 >= total ? accent_ink : GColorBlack);
}
#endif

/*
 * the status bar's ink, by the same rule as the texts: whatever ground the
 * waterline has left under its row. the system layer gives no clips to work
 * with, so it flips whole, at its own mid-height -- on round at its twelve
 * o'clock anchor, which the wedge only ever covers at the very full.
 */
static void prv_update_status_color(DetailWindow *detail_window) {
  if (detail_window->status == NULL || detail_window->layer == NULL) {
    return;
  }
  int64_t current, total;
  bool water = prv_times(detail_window, &current, &total);
  bool covered = false;
  if (water) {
#ifdef PBL_ROUND
    covered = current >= total;
#else
    covered = prv_water_level(detail_window,
              layer_get_bounds(detail_window->layer)) <= STATUS_BAR_LAYER_HEIGHT / 2;
#endif
  }
  status_bar_layer_set_colors(detail_window->status, GColorClear,
    covered ? prv_legible_over(detail_window->highlight_color) : GColorBlack);
}

/*
 * layer update proc
 * for animating the water block
 */

static void layer_update_proc(Layer *layer, GContext *ctx) {
  // get DetailWindow pointer from layer data
  DetailWindow *detail_window = (*(DetailWindow**)layer_get_data(layer));
  int64_t current_time, total_time;
  if (!prv_times(detail_window, &current_time, &total_time)) {
    return;
  }

  // draw background
#ifdef PBL_ROUND
  graphics_context_set_fill_color(ctx, detail_window->highlight_color);
  GRect bounds = layer_get_bounds(layer);
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFitCircle, bounds.size.w / 2,
    TRIG_MAX_ANGLE - TRIG_MAX_ANGLE * current_time / total_time, TRIG_MAX_ANGLE);
#else
  int16_t water_level = prv_water_level(detail_window, layer_get_bounds(layer));
  graphics_context_set_fill_color(ctx, detail_window->highlight_color);
  graphics_fill_rect(ctx, GRect(0, water_level, layer_get_bounds(layer).size.w,
    layer_get_bounds(layer).size.h - water_level), 1, GCornerNone);
#endif
}

static int64_t prv_round_up_to_next_second(int64_t value) {
  return value > 0 ? value + MSEC_IN_SEC - 1 : 0;
}



/*******************************************************************************
 * CALLBACKS
 */

static void prv_update_action_icons(DetailWindow *detail_window) {
  if (!detail_window || !detail_window->action) {
    return;
  }

  if (detail_window->delete_armed) {
    // Confirmation mode: confirm on UP, cancel on DOWN, disable SELECT.
    action_bar_layer_set_icon(detail_window->action, BUTTON_ID_UP,
                              detail_window->delete_icon);
    action_bar_layer_set_icon(detail_window->action, BUTTON_ID_SELECT, NULL);
    action_bar_layer_set_icon(detail_window->action, BUTTON_ID_DOWN,
                              detail_window->dismiss_icon);
    return;
  }

  // Normal mode.
  action_bar_layer_set_icon(detail_window->action, BUTTON_ID_UP,
                            detail_window->edit_icon);
  action_bar_layer_set_icon(detail_window->action, BUTTON_ID_SELECT,
                            (detail_window->countdown_timer &&
                             countdown_timer_get_paused(detail_window->countdown_timer))
                                ? detail_window->play_icon
                                : detail_window->pause_icon);
  action_bar_layer_set_icon(detail_window->action, BUTTON_ID_DOWN,
                            detail_window->delete_icon);
}

static void prv_disarm_delete(DetailWindow *detail_window, bool refresh) {
  if (!detail_window) {
    return;
  }
  detail_window->delete_armed = false;
  if (detail_window->delete_arm_timer) {
    app_timer_cancel(detail_window->delete_arm_timer);
    detail_window->delete_arm_timer = NULL;
  }
  if (refresh) {
    prv_update_action_icons(detail_window);
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE load:action-bar");
    detail_window_refresh(detail_window);
  }
}

static void prv_delete_arm_timer_callback(void *context) {
  DetailWindow *detail_window = (DetailWindow *)context;
  if (!detail_window) {
    return;
  }
  detail_window->delete_arm_timer = NULL;
  prv_disarm_delete(detail_window, true);
}

/*
 * UP click handler callback
 *
 * edits the timer
 */

static void prv_up_pressed(void *context) {
  DetailWindow *detail_window = (DetailWindow*)context;
  if (detail_window && detail_window->delete_armed) {
    // confirmed: proceed with delete
    prv_disarm_delete(detail_window, false);
    prv_update_action_icons(detail_window);
    return detail_window->callbacks.delete_timer(detail_window->countdown_timer, context);
  }

  prv_disarm_delete(detail_window, false);
  return detail_window->callbacks.edit_timer(detail_window->countdown_timer, context);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_up_pressed(context);
}

/*
 * SELECT click handler callback
 *
 * plays or pauses the timer
 */

static void prv_select_pressed(void *context) {
  DetailWindow *detail_window = (DetailWindow*)context;
  if (detail_window && detail_window->delete_armed) {
    // ignore SELECT during confirmation
    return;
  }

  prv_disarm_delete(detail_window, false);
  return detail_window->callbacks.playpause_timer(detail_window->countdown_timer, context);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_select_pressed(context);
}

/*
 * DOWN click handler callback
 *
 * deletes the timer
 */

static void prv_down_pressed(void *context) {
  DetailWindow *detail_window = (DetailWindow*)context;
  if (!detail_window || !detail_window->countdown_timer) {
    return;
  }

  if (detail_window->delete_armed) {
    // cancel confirmation
    prv_disarm_delete(detail_window, true);
    return;
  }

  if (detail_window->delete_immediately) {
    // Confirm Deletion: Off. DOWN is a straight delete on the first press, and
    // the "Timer Deleted" popup still shows -- it is feedback, not a guard.
    return detail_window->callbacks.delete_timer(detail_window->countdown_timer, context);
  }

  // arm delete confirmation
  detail_window->delete_armed = true;
  if (detail_window->delete_arm_timer) {
    app_timer_cancel(detail_window->delete_arm_timer);
    detail_window->delete_arm_timer = NULL;
  }
  detail_window->delete_arm_timer = app_timer_register(DELETE_ARM_TIMEOUT_MS,
                                                       prv_delete_arm_timer_callback,
                                                       detail_window);
  prv_update_action_icons(detail_window);
  detail_window_refresh(detail_window);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_down_pressed(context);
}

/*
 * click configuration provider
 */

static void click_config_provider(void *context) {
  window_set_click_context(BUTTON_ID_UP, context);
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_set_click_context(BUTTON_ID_DOWN, context);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}



/*******************************************************************************
 * TOUCH
 */

#if TOUCH_INPUT

// the one live DetailWindow, by the file-static route touch.h describes

static DetailWindow *s_touch_detail_window = NULL;

// a horizontal flick backs out of the window, the gesture the system bridge
// would have made for us; this window turns the bridge off, so it stands in.
static void prv_touch_swipe_handler(const Recognizer *recognizer, RecognizerEvent event) {
  if (!s_touch_detail_window || event != RecognizerEvent_Completed) {
    return;
  }
  if (swipe_recognizer_get_direction(recognizer) &
    (SwipeDirection_Left | SwipeDirection_Right)) {
    window_stack_pop(true);
  }
}

// a tap on the action bar column acts as the button whose icon sits in that
// third of the bar.
static void prv_touch_tap_handler(const Recognizer *recognizer, RecognizerEvent event) {
  if (!s_touch_detail_window || event != RecognizerEvent_Completed) {
    return;
  }
  GPoint tap = tap_recognizer_get_tap_point(recognizer);
  GRect bounds = layer_get_bounds(window_get_root_layer(s_touch_detail_window->window));
  if (tap.x < bounds.size.w - ACTION_BAR_WIDTH) {
    return;
  }
  if (tap.y < bounds.size.h / 3) {
    prv_up_pressed(s_touch_detail_window);
  } else if (tap.y < 2 * bounds.size.h / 3) {
    prv_select_pressed(s_touch_detail_window);
  } else {
    prv_down_pressed(s_touch_detail_window);
  }
}

#endif  // TOUCH_INPUT



/*******************************************************************************
 * API FUNCTIONS
 */

static void prv_window_load(Window* window){
  DetailWindow *detail_window = window_get_user_data(window);
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE load:enter w=%p", (void*)window);
  window_set_background_color(detail_window->window, GColorLightGray);

  // load resources
  detail_window->dismiss_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DISMISS);
  detail_window->edit_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_EDIT);
  detail_window->play_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PLAY);
  detail_window->pause_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PAUSE);
  detail_window->delete_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DELETE);
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE load:icons");
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  detail_window->large_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_LECO_REGULAR_SUBSET_48));
  detail_window->medium_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_LECO_REGULAR_SUBSET_36));
  detail_window->small_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_LECO_REGULAR_SUBSET_26));
  uint8_t text_sizes[] = {52, 40, 30};
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE load:fonts");
#else
  detail_window->large_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_LECO_REGULAR_SUBSET_36));
  detail_window->medium_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_LECO_REGULAR_SUBSET_26));
  detail_window->small_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_LECO_REGULAR_SUBSET_20));
  uint8_t text_sizes[] = {40, 30, 24};
#endif
  // get window parameters
  Layer *root = window_get_root_layer(detail_window->window);
  GRect bounds = layer_get_frame(root);
  // create animation layer
  // IMPORTANT: must be created with data for the DetailWindow pointer
  // so that it can be accessed in the layer_update_proc callback
  detail_window->layer = layer_create_with_data(bounds, sizeof(DetailWindow*));
  DetailWindow **layer_data = (DetailWindow**)layer_get_data(detail_window->layer);
  (*layer_data) = detail_window;
  layer_set_update_proc(detail_window->layer, layer_update_proc);
  layer_add_child(root, detail_window->layer);
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE load:content-layer");
  // the texts: laid out as they were as TextLayers, then created in copies
  // for the split or one each for the flips, per platform
#ifdef PBL_ROUND
  detail_window->main_bounds =
    GRect(0, bounds.size.h / 2 - text_sizes[0] / 2, bounds.size.w - ACTION_BAR_WIDTH,
          text_sizes[0]);
#else
  detail_window->main_bounds =
    GRect(0, bounds.size.h * 2 / 17, bounds.size.w - ACTION_BAR_WIDTH, text_sizes[0]);
#endif
  detail_window->main_align = GTextAlignmentCenter;
#ifdef PBL_ROUND
  detail_window->sub_bounds =
    GRect(0, bounds.size.h - text_sizes[2] - 11, bounds.size.w, text_sizes[2]);
  detail_window->sub_align = GTextAlignmentCenter;
#else
  detail_window->sub_bounds =
    GRect(10, bounds.size.h - text_sizes[2] - 6, bounds.size.w - ACTION_BAR_WIDTH,
          text_sizes[2]);
  detail_window->sub_align = GTextAlignmentLeft;
#endif
  strcpy(detail_window->main_buff, "00:00");
  detail_window->sub_buff[0] = '\0';
#ifdef WATER_SPLIT
  // the twin clip containers meet at the waterline; the copies inside them
  // carry the same text, each in its ground's ink
  detail_window->above = layer_create(bounds);
  layer_add_child(root, detail_window->above);
  detail_window->below = layer_create(GRect(0, bounds.size.h, bounds.size.w, 0));
  layer_add_child(root, detail_window->below);
  detail_window->main_below = text_layer_create(detail_window->main_bounds);
  detail_window->sub_below = text_layer_create(detail_window->sub_bounds);
#endif
  detail_window->main_text = text_layer_create(detail_window->main_bounds);
  detail_window->sub_text = text_layer_create(detail_window->sub_bounds);
  text_layer_set_text_alignment(detail_window->main_text, detail_window->main_align);
  text_layer_set_text_alignment(detail_window->sub_text, detail_window->sub_align);
  text_layer_set_background_color(detail_window->main_text, GColorClear);
  text_layer_set_background_color(detail_window->sub_text, GColorClear);
  text_layer_set_text_color(detail_window->main_text, GColorBlack);
  text_layer_set_text_color(detail_window->sub_text, GColorBlack);
  text_layer_set_text(detail_window->main_text, detail_window->main_buff);
  text_layer_set_text(detail_window->sub_text, detail_window->sub_buff);
#ifdef WATER_SPLIT
  text_layer_set_text_alignment(detail_window->main_below, detail_window->main_align);
  text_layer_set_text_alignment(detail_window->sub_below, detail_window->sub_align);
  text_layer_set_background_color(detail_window->main_below, GColorClear);
  text_layer_set_background_color(detail_window->sub_below, GColorClear);
  text_layer_set_text_color(detail_window->main_below,
                            prv_legible_over(detail_window->highlight_color));
  text_layer_set_text_color(detail_window->sub_below,
                            prv_legible_over(detail_window->highlight_color));
  text_layer_set_text(detail_window->main_below, detail_window->main_buff);
  text_layer_set_text(detail_window->sub_below, detail_window->sub_buff);
  layer_add_child(detail_window->above, text_layer_get_layer(detail_window->main_text));
  layer_add_child(detail_window->above, text_layer_get_layer(detail_window->sub_text));
  layer_add_child(detail_window->below, text_layer_get_layer(detail_window->main_below));
  layer_add_child(detail_window->below, text_layer_get_layer(detail_window->sub_below));
  prv_update_split(detail_window);
#else
  layer_add_child(root, text_layer_get_layer(detail_window->main_text));
  layer_add_child(root, text_layer_get_layer(detail_window->sub_text));
#endif
#ifdef PBL_ROUND
  prv_update_inks(detail_window);
#endif
  prv_apply_fonts(detail_window);
  // create action bar
  detail_window->action = action_bar_layer_create();
  action_bar_layer_add_to_window(detail_window->action, detail_window->window);
  action_bar_layer_set_click_config_provider(detail_window->action, click_config_provider);
  action_bar_layer_set_context(detail_window->action, detail_window);
  prv_update_action_icons(detail_window);

#if TOUCH_INPUT
  // touch: tapping an action bar icon acts as its button, a sideways flick
  // backs out. this window has no MenuLayer, so the system touch navigation
  // has nothing to bridge here; the bridge is still turned off so the
  // recognizers above get the touch stream.
  s_touch_detail_window = detail_window;
  window_set_touch_bridge_disabled(detail_window->window, true);
  window_attach_recognizer(detail_window->window,
    tap_recognizer_create(prv_touch_tap_handler, NULL));
  window_attach_recognizer(detail_window->window,
    swipe_recognizer_create(prv_touch_swipe_handler, NULL,
      SwipeDirection_Left | SwipeDirection_Right));
#endif

  // create status bar
#ifdef PBL_ROUND
  int16_t horiz_off = 0;
#else
  int16_t horiz_off = ACTION_BAR_WIDTH;
#endif
  // the status bar row is the first thing the water uncovers, so its ink
  // follows the ground under it like the texts do
  detail_window->status = status_bar_layer_create();
  layer_set_frame(status_bar_layer_get_layer(detail_window->status),
    GRect(0, 0, bounds.size.w - horiz_off, STATUS_BAR_LAYER_HEIGHT));
  status_bar_layer_set_colors(detail_window->status, GColorClear, GColorBlack);
  layer_add_child(root, status_bar_layer_get_layer(detail_window->status));
  prv_update_status_color(detail_window);
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE load:done");
}

static void prv_window_unload(Window* window){
  DetailWindow *detail_window = window_get_user_data(window);
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE unload:enter");
#if TOUCH_INPUT
  s_touch_detail_window = NULL;
#endif
  prv_disarm_delete(detail_window, false);
  status_bar_layer_destroy(detail_window->status);
  detail_window->status = NULL;
  action_bar_layer_destroy(detail_window->action);
  detail_window->action = NULL;
  text_layer_destroy(detail_window->sub_text);
  detail_window->sub_text = NULL;
  text_layer_destroy(detail_window->main_text);
  detail_window->main_text = NULL;
#ifdef WATER_SPLIT
  text_layer_destroy(detail_window->sub_below);
  detail_window->sub_below = NULL;
  text_layer_destroy(detail_window->main_below);
  detail_window->main_below = NULL;
  layer_destroy(detail_window->below);
  detail_window->below = NULL;
  layer_destroy(detail_window->above);
  detail_window->above = NULL;
#endif
  layer_destroy(detail_window->layer);
  detail_window->layer = NULL;
  gbitmap_destroy(detail_window->dismiss_icon);
  detail_window->dismiss_icon = NULL;
  gbitmap_destroy(detail_window->edit_icon);
  detail_window->edit_icon = NULL;
  gbitmap_destroy(detail_window->play_icon);
  detail_window->play_icon = NULL;
  gbitmap_destroy(detail_window->pause_icon);
  detail_window->pause_icon = NULL;
  gbitmap_destroy(detail_window->delete_icon);
  detail_window->delete_icon = NULL;
  fonts_unload_custom_font(detail_window->large_font);
  detail_window->large_font = NULL;
  fonts_unload_custom_font(detail_window->medium_font);
  detail_window->medium_font = NULL;
  fonts_unload_custom_font(detail_window->small_font);
  detail_window->small_font = NULL;
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE unload:done");
}

/*
 * create a new DetailWindow and return a pointer to it
 * this includes creating all its children layers but
 * does not push it onto the window stack
 */

DetailWindow *detail_window_create(DetailWindowCallbacks detail_window_callbacks) {
  DetailWindow *detail_window = (DetailWindow*)malloc(sizeof(DetailWindow));
  // error handling
  if (detail_window == NULL) {
    return NULL;
  }

  *detail_window = (DetailWindow) {
    .callbacks = detail_window_callbacks,
    .delete_armed = false,
    .delete_arm_timer = NULL,
    .delete_immediately = false,
  };

  detail_window->window = window_create();
  window_set_user_data(detail_window->window, detail_window);
  window_set_window_handlers(detail_window->window,
    (WindowHandlers){
      .load = prv_window_load,
      .unload = prv_window_unload
    });
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE detail create w=%p", (void*)detail_window->window);

  return detail_window;
}

/*
 * destroy a previously created DetailWindow
 */

void detail_window_destroy(DetailWindow *detail_window) {
  if (detail_window != NULL) {
    if (detail_window->window != NULL) {
      window_destroy(detail_window->window);
      detail_window->window = NULL;
    }
    free(detail_window);
    detail_window = NULL;
    return;
  }
}

/*
 * push the window onto the stack
 */

void detail_window_push(DetailWindow *detail_window, bool animated) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE detail push w=%p", (void*)detail_window->window);
  window_stack_push(detail_window->window, animated);
}

/*
 * pop the window off the stack
 */

void detail_window_pop(DetailWindow *detail_window, bool animated) {
  if (detail_window->window) {
    window_stack_remove(detail_window->window, animated);
  }
}

/*
 * gets whether it is the topmost window on the stack
 */

bool detail_window_get_topmost_window(DetailWindow *detail_window) {
  return window_stack_get_top_window() == detail_window->window;
}

/*
 * set the timer associated with the window
 */

void detail_window_set_countdown_timer(DetailWindow *detail_window,
                                       CountdownTimer *countdown_timer) {
  detail_window->countdown_timer = countdown_timer;
  prv_disarm_delete(detail_window, true);
  prv_update_action_icons(detail_window);
}

/*
 * refresh the provided DetailWindow
 */

void detail_window_refresh(DetailWindow *detail_window) {
  if (!window_is_loaded(detail_window->window)) {
    return;
  }

  if (detail_window->countdown_timer == NULL) {
    strcpy(detail_window->main_buff, "00:00");
    detail_window->sub_buff[0] = '\0';
  } else {
    // main text
    countdown_timer_format_text(
      prv_round_up_to_next_second(countdown_timer_get_display_time(detail_window->countdown_timer)),
      detail_window->main_buff, sizeof(detail_window->main_buff));
    // sub text
    if (detail_window->delete_armed) {
      // Use a built-in system font so glyphs (e.g. '?') are always present.
      strcpy(detail_window->sub_buff, "Delete?");
    } else {
      countdown_timer_format_text(countdown_timer_get_duration(detail_window->countdown_timer),
        detail_window->sub_buff, sizeof(detail_window->sub_buff));
    }
  }
  // hand the text to every copy and re-ink, re-clip, re-font them with it
  text_layer_set_text(detail_window->main_text, detail_window->main_buff);
  text_layer_set_text(detail_window->sub_text, detail_window->sub_buff);
#ifdef WATER_SPLIT
  text_layer_set_text(detail_window->main_below, detail_window->main_buff);
  text_layer_set_text(detail_window->sub_below, detail_window->sub_buff);
#endif
  prv_apply_fonts(detail_window);
#ifdef PBL_ROUND
  prv_update_inks(detail_window);
#endif
#ifdef WATER_SPLIT
  prv_update_split(detail_window);
#endif
  prv_update_status_color(detail_window);
  layer_mark_dirty(detail_window->layer);
}

/*
 * deep refresh the window, updating icons etc.
 */

void detail_window_deep_refresh(DetailWindow *detail_window) {
  if (window_is_loaded(detail_window->window) && detail_window->countdown_timer != NULL) {
    prv_update_action_icons(detail_window);
    detail_window_refresh(detail_window);
    return;
  }
}

/*
 * set highlight color of this window
 * this is the overall color scheme used
 */

void detail_window_set_highlight_color(DetailWindow *detail_window,
                                       GColor color) {
  detail_window->highlight_color = color;
  if (!window_is_loaded(detail_window->window)) {
    return;
  }
  // the accent is both the water and the ground the legible colour is judged
  // against: re-ink everything that reads off it and repaint the water
#ifdef WATER_SPLIT
  text_layer_set_text_color(detail_window->main_below, prv_legible_over(color));
  text_layer_set_text_color(detail_window->sub_below, prv_legible_over(color));
#endif
#ifdef PBL_ROUND
  prv_update_inks(detail_window);
#endif
  prv_update_status_color(detail_window);
  layer_mark_dirty(detail_window->layer);
}

/*
 * set whether DOWN deletes on the first press
 *
 * this is the Confirm Deletion setting's arming behaviour, owned here and stored
 * there: main.c calls this wherever the setting changes and wherever the window
 * is
 * pushed. when immediately is false, behaviour is exactly 3d4774f's armed
 * action bar.
 */

void detail_window_set_delete_immediately(DetailWindow *detail_window, bool immediately) {
  if (detail_window == NULL) {
    return;
  }
  detail_window->delete_immediately = immediately;
}

/*
 * gets whether it needs to be updated for the animations
 */

bool detail_window_get_update_needed(DetailWindow *detail_window) {
  if (detail_window->countdown_timer == NULL) return false;
  return detail_window->animation_update_needed ||
         !countdown_timer_get_paused(detail_window->countdown_timer);
}
