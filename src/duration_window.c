/*******************************************************************************
 * FILENAME :        duration_window.c
 *
 * DESCRIPTION :
 *      Duration picker screen to select a time duration
 *
 * PUBLIC FUNCTIONS :
 *      DurationWindow   *duration_window_create(DurationWindowCallbacks
 *                          duration_window_callbacks);
 *      void            duration_window_destroy(DurationWindow *duration_window);
 *      void            duration_window_push(DurationWindow *duration_window,
 *                          bool animated);
 *      void            duration_window_pop(DurationWindow *duration_window,
 *                          bool animated);
 *      bool            duration_window_get_topmost_window(DurationWindow
 *                          *duration_window);
 *      void            duration_window_set_timer(DurationWindow *duration_window,
 *                          CountdownTimer *countdown_timer);
 *      CountdownTimer  *duration_window_get_timer(DurationWindow
 *                          *duration_window);
 *      void            duration_window_set_highlight_color(DurationWindow
 *                          *duration_window, GColor color);
 *
 * AUTHOR :         Eric Phillips        START DATE :    07/12/15
 *
 */

#include <pebble.h>
#include "duration_window.h"

static void prv_window_load(Window *window);
static void prv_window_unload(Window *window);
#include "countdown_timer.h"
#include "selection_layer.h"
#include "touch.h"

#define MSEC_IN_SEC 1000
#define MSEC_IN_MIN 60000
#define MSEC_IN_HR 3600000
#define MIN_IN_HR 60
#define HR_IN_DAY 24

#define REPEATING_CLICK_THRESHOLD 10

#define TIMER_MINIMUM_DURATION 1000 // milliseconds
#define TIMELINE_MINIMUM_DURATION 900000 // milliseconds



/*******************************************************************************
 * STRUCTURE DEFINITION
 */

/*
 * the structure of a DurationWindow
 */

struct DurationWindow {
  Window          *window;            //< main window
  TextLayer       *main_text;         //< title text at top of screen
  TextLayer       *sub_text;          //< sub text at bottom for messages
  Layer           *selection;         //< SelectionLayer for input
  GColor          highlight_color;    //< color for selection highlights
  StatusBarLayer  *status;            //< status bar for Basalt
  DurationWindowCallbacks callbacks;   //< callbacks

  CountdownTimer  *countdown_timer;   //< timer associated being set
  bool            snooze_mode;        //< dialling a snooze delay, not a timer
  int32_t         field_values[3];    //< values of selection fields
  char            field_buffs[3][3];  //< buffers to draw field contents
  int8_t          field_selection;    //< index of selected field
};



/*******************************************************************************
 * PRIVATE FUNCTIONS
 */

/*
 * update the sub text
 *
 * updates whether the end time is shown in the picker sub text
 */

static void update_sub_text(DurationWindow *duration_window) {
  if (duration_window->sub_text == NULL) {
    return;
  }
  int64_t duration = (int64_t)duration_window->field_values[0] * MSEC_IN_HR +
    (int64_t)duration_window->field_values[1] * MSEC_IN_MIN +
    (int64_t)duration_window->field_values[2] * MSEC_IN_SEC;
  // in snooze mode there is no end time to preview and zero is a
  // legal value -- it means Off -- so the sub text just names it
  if (duration_window->snooze_mode) {
    text_layer_set_text(duration_window->sub_text,
      (duration < MSEC_IN_SEC) ? "Snooze Off" : "");
    layer_set_hidden(text_layer_get_layer(duration_window->sub_text), false);
    return;
  }
  // check duration
  if (duration < TIMER_MINIMUM_DURATION) {
    text_layer_set_text(duration_window->sub_text, "");
    layer_set_hidden(text_layer_get_layer(duration_window->sub_text), false);
    return;
  } else if (duration < TIMELINE_MINIMUM_DURATION) {
    layer_set_hidden(text_layer_get_layer(duration_window->sub_text), true);
    return;
  } else {
    layer_set_hidden(text_layer_get_layer(duration_window->sub_text), false);
  }

  // format into time parts
  time_t end = ((int64_t)time(NULL) * 1000 + (int64_t)time_ms(NULL, NULL) + duration) / 1000;
  static char buff[] = "End: 00:00 AM";
  struct tm *tick_time = localtime(&end);
  if (clock_is_24h_style()) {
    strftime(buff, sizeof(buff), "End: %k:%M", tick_time);
  } else {
    strftime(buff, sizeof(buff), "End: %l:%M %p", tick_time);
  }
  // set text
  text_layer_set_text(duration_window->sub_text, buff);
}


/*******************************************************************************
 * CALLBACKS
 */

/*
 * selection layer get text callback
 */

static char* selection_handle_get_text(unsigned index, void *context) {
  DurationWindow *duration_window = (DurationWindow*)context;
  snprintf(duration_window->field_buffs[index], sizeof(duration_window->field_buffs[0]), "%02d",
    (int)duration_window->field_values[index]);
  return duration_window->field_buffs[index];
}



/*
 * selection layer complete callback
 */

static void selection_handle_complete(void *context) {
  DurationWindow *duration_window = (DurationWindow*)context;
  int64_t duration = (int64_t)duration_window->field_values[0] * MSEC_IN_HR +
    (int64_t)duration_window->field_values[1] * MSEC_IN_MIN +
    (int64_t)duration_window->field_values[2] * MSEC_IN_SEC;
  // call complete callback
  duration_window->callbacks.duration_complete(duration, duration_window);
}



/*
 * selection layer increment up callback
 */

static void selection_handle_inc(unsigned index, uint8_t clicks, void *context) {
  DurationWindow *duration_window = (DurationWindow*)context;
  duration_window->field_values[index] += (clicks > REPEATING_CLICK_THRESHOLD) ? 2 : 1;
  int8_t max_value = (index == 0) ? HR_IN_DAY : MIN_IN_HR;
  if (duration_window->field_values[index] >= max_value) {
    duration_window->field_values[index] -= max_value;
  }
  // update text
  update_sub_text(duration_window);
}



/*
 * selection layer decrement callback
 */

static void selection_handle_dec(unsigned index, uint8_t clicks, void *context) {
  DurationWindow *duration_window = (DurationWindow*)context;
  duration_window->field_values[index] -= (clicks > REPEATING_CLICK_THRESHOLD) ? 2 : 1;
  int8_t max_value = (index == 0) ? HR_IN_DAY : MIN_IN_HR;
  if (duration_window->field_values[index] < 0) {
    duration_window->field_values[index] += max_value;
  }
  // update text
  update_sub_text(duration_window);
}



/*******************************************************************************
 * TOUCH
 */

#if TOUCH_INPUT

// the one live DurationWindow, by the file-static route touch.h describes

static DurationWindow *s_touch_duration_window = NULL;

// a horizontal flick steps back through the fields and out of the window,
// exactly what the back button does; the system bridge is off here, so the
// gesture stands in for both it and the button.
static void prv_touch_swipe_handler(const Recognizer *recognizer, RecognizerEvent event) {
  if (!s_touch_duration_window || event != RecognizerEvent_Completed) {
    return;
  }
  if (swipe_recognizer_get_direction(recognizer) &
    (SwipeDirection_Left | SwipeDirection_Right)) {
    selection_layer_touch_back(s_touch_duration_window->selection);
  }
}

static void prv_touch_pan_handler(const Recognizer *recognizer, RecognizerEvent event) {
  if (!s_touch_duration_window) {
    return;
  }
  switch (event) {
    case RecognizerEvent_Updated:
      selection_layer_touch_panned(s_touch_duration_window->selection,
        pan_recognizer_get_delta_since_prev(recognizer).y);
      break;
    case RecognizerEvent_Completed:
    case RecognizerEvent_Cancelled:
      selection_layer_touch_pan_ended(s_touch_duration_window->selection);
      break;
    default:
      break;
  }
}

static void prv_touch_tap_handler(const Recognizer *recognizer, RecognizerEvent event) {
  if (!s_touch_duration_window || event != RecognizerEvent_Completed) {
    return;
  }
  selection_layer_touch_tapped(s_touch_duration_window->selection,
    tap_recognizer_get_tap_point(recognizer));
}

#endif  // TOUCH_INPUT



/*******************************************************************************
 * API FUNCTIONS
 */

/*
 * create a new DurationWindow and return a pointer to it
 * this includes creating all its children layers but
 * does not push it onto the window stack
 */

DurationWindow *duration_window_create(DurationWindowCallbacks duration_window_callbacks) {
  DurationWindow *duration_window = (DurationWindow*)malloc(sizeof(DurationWindow));
  if (duration_window != NULL) {

    *duration_window = (DurationWindow) { .callbacks = duration_window_callbacks };

    duration_window->window = window_create();
    window_set_user_data(duration_window->window, duration_window);
    window_set_window_handlers(duration_window->window,
      (WindowHandlers){
        .load = prv_window_load,
        .unload = prv_window_unload
      });
    APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE duration create w=%p", (void*)duration_window->window);

    return duration_window;
  }
  return NULL;
}



/*
 * destroy a previously created DurationWindow
 */

void duration_window_destroy(DurationWindow *duration_window) {
  if (duration_window != NULL) {
    if (duration_window->window != NULL) {
      window_destroy(duration_window->window);
      duration_window->window = NULL;
    }
    free(duration_window);
    duration_window = NULL;
    return;
  }
}


static void prv_window_load(Window* window){
  DurationWindow *duration_window = window_get_user_data(window);
  // get window parameters
  Layer *root = window_get_root_layer(duration_window->window);
  GRect bounds = layer_get_frame(root);
  // main text
  duration_window->main_text = text_layer_create(GRect(0, bounds.size.h/7, bounds.size.w, 40));
  text_layer_set_text(duration_window->main_text,
    duration_window->snooze_mode ? "Snooze" : "Set Timer");
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  text_layer_set_font(duration_window->main_text,
    fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
#else
  text_layer_set_font(duration_window->main_text,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
#endif
  text_layer_set_text_alignment(duration_window->main_text, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(duration_window->main_text));
  // sub text
  duration_window->sub_text = text_layer_create(GRect(1, bounds.size.h-43*bounds.size.h/168, bounds.size.w, 40));
  text_layer_set_text_alignment(duration_window->sub_text, GTextAlignmentCenter);
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  text_layer_set_font(duration_window->sub_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
#else
  text_layer_set_font(duration_window->sub_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
#endif
  layer_add_child(root, text_layer_get_layer(duration_window->sub_text));
  // create selection layer
  uint8_t num_cells = 3; // hours, minutes, seconds
  uint16_t screen_width = bounds.size.w;
  uint16_t cell_height = (bounds.size.h + 4) / 5;
  uint16_t cell_y_start = bounds.size.h/2 - cell_height/2;
#ifdef PBL_ROUND
  duration_window->selection = selection_layer_create(GRect(26, cell_y_start, bounds.size.w-52, cell_height), num_cells);
  uint8_t cell_width = (screen_width - 52 - (num_cells - 1) * 4) / num_cells;
#else
  duration_window->selection = selection_layer_create(GRect(8, cell_y_start, bounds.size.w-16, cell_height), num_cells);
  uint8_t cell_width = (screen_width - 16 - (num_cells - 1) * 4) / num_cells;
#endif
  for (int i = 0; i < num_cells; i++) {
    selection_layer_set_cell_width(duration_window->selection, i, cell_width);
  }
  selection_layer_set_cell_padding(duration_window->selection, 4);
  selection_layer_set_active_bg_color(duration_window->selection, duration_window->highlight_color);
  selection_layer_set_inactive_bg_color(duration_window->selection, GColorDarkGray);
  selection_layer_set_click_config_onto_window(
    duration_window->selection, duration_window->window);
  selection_layer_set_callbacks(duration_window->selection, duration_window,
    (SelectionLayerCallbacks) {
      .get_cell_text = selection_handle_get_text,
      .complete = selection_handle_complete,
      .increment = selection_handle_inc,
      .decrement = selection_handle_dec,
    });
  layer_add_child(window_get_root_layer(duration_window->window), duration_window->selection);

  // create status bar
  duration_window->status = status_bar_layer_create();
  status_bar_layer_set_colors(duration_window->status, GColorClear, GColorBlack);
  layer_add_child(root, status_bar_layer_get_layer(duration_window->status));

#if TOUCH_INPUT
  // touch: swipe vertically to spin the active field, tap a field to select
  // it, tap the active field to commit, flick sideways to step back. the
  // bridge is turned off here so these recognizers get the touch stream
  // instead of synthesised button presses.
  s_touch_duration_window = duration_window;
  window_set_touch_bridge_disabled(duration_window->window, true);
  window_attach_recognizer(duration_window->window,
    pan_recognizer_create(prv_touch_pan_handler, NULL, PanAxis_Vertical));
  window_attach_recognizer(duration_window->window,
    tap_recognizer_create(prv_touch_tap_handler, NULL));
  window_attach_recognizer(duration_window->window,
    swipe_recognizer_create(prv_touch_swipe_handler, NULL,
      SwipeDirection_Left | SwipeDirection_Right));
#endif

  update_sub_text(duration_window);
}

static void prv_window_unload(Window* window){
  DurationWindow *duration_window = window_get_user_data(window);
#if TOUCH_INPUT
  s_touch_duration_window = NULL;
#endif
  status_bar_layer_destroy(duration_window->status);
  duration_window->status = NULL;
  selection_layer_destroy(duration_window->selection);
  duration_window->selection = NULL;
  text_layer_destroy(duration_window->sub_text);
  duration_window->sub_text = NULL;
  text_layer_destroy(duration_window->main_text);
  duration_window->main_text = NULL;
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE duration unload:done");
}



/*
 * push the window onto the stack
 */
void duration_window_push(DurationWindow *duration_window, bool animated) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "TRACE duration push w=%p", (void*)duration_window->window);
  window_stack_push(duration_window->window, animated);
}



/*
 * pop the window off the stack
 */

void duration_window_pop(DurationWindow *duration_window, bool animated) {
  if (duration_window->window) {
    window_stack_remove(duration_window->window, animated);
  }
}



/*
 * gets whether it is the topmost window on the stack
 */

bool duration_window_get_topmost_window(DurationWindow *duration_window) {
  return window_stack_get_top_window() == duration_window->window;
}



/*
 * sets the CountdownTimer associated with the DurationWindow
 *
 * used to identify if this was an update to an existing timer or a new one
 */

void duration_window_set_timer(DurationWindow *duration_window, CountdownTimer *countdown_timer) {
  duration_window->snooze_mode = false;  // every timer entry resets the mode
  duration_window->countdown_timer = countdown_timer;
  // set selection values if a timer was passed in
  int64_t duration = 0;
  if (duration_window->countdown_timer) {
    duration = countdown_timer_get_duration(duration_window->countdown_timer);
  }
  duration_window->field_values[0] = duration / MSEC_IN_HR;
  duration_window->field_values[1] = duration % MSEC_IN_HR / MSEC_IN_MIN;
  duration_window->field_values[2] = duration % MSEC_IN_MIN / MSEC_IN_SEC;
  // change text
  if (window_is_loaded(duration_window->window)) {
    update_sub_text(duration_window);
  }
}



/*
 * gets the CountdownTimer associated with this DurationWindow
 *
 * used to identify if this was an update to an existing timer or a new one
 */

CountdownTimer *duration_window_get_timer(DurationWindow *duration_window) {
  return duration_window->countdown_timer;
}



/*
 * Switch the picker to Snooze mode and seed the fields with the current
 * delay. The complete callback fires as before; main.c asks
 * duration_window_get_snooze_mode to know which setting the value is for.
 */

void duration_window_set_snooze_mode(DurationWindow *duration_window, int64_t current) {
  duration_window->snooze_mode = true;
  duration_window->countdown_timer = NULL;
  duration_window->field_values[0] = current / MSEC_IN_HR;
  duration_window->field_values[1] = current % MSEC_IN_HR / MSEC_IN_MIN;
  duration_window->field_values[2] = current % MSEC_IN_MIN / MSEC_IN_SEC;
  // the title is set at window load; the sub text can live-update
  if (window_is_loaded(duration_window->window)) {
    text_layer_set_text(duration_window->main_text, "Snooze");
    update_sub_text(duration_window);
  }
}



/*
 * Gets whether the picker is dialling a snooze delay
 */

bool duration_window_get_snooze_mode(DurationWindow *duration_window) {
  return duration_window->snooze_mode;
}



/*
 * set highlight color of this window
 * this is the overall color scheme used
 */

void duration_window_set_highlight_color(DurationWindow *duration_window, GColor color) {
  duration_window->highlight_color = color;
}
