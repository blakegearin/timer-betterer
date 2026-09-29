/*******************************************************************************
 * FILENAME :        main.c
 *
 * DESCRIPTION :
 *      Entry point and main logic for program. Controls everything
 *      of importance that happens.
 *
 * AUTHOR :     Eric Phillips        START DATE :    07/10/15
 *
 */

#include <pebble.h>
#include "countdown_timer.h"
#include "menu_window.h"
#include "detail_window.h"
#include "duration_window.h"
#include "popup_window.h"
#include "phone.h"
#include "settings.h"
#include "settings_window.h"
#include "option_window.h"

// constants
#define COUNTDOWN_TIMER_PERSIST_KEY 72445846
#define COUNTDOWN_TIMER_ID_PERSIST_KEY 3568356
#define PERSIST_VERSION 1
#define PERSIST_VERSION_KEY 46134672
#define COUNTDOWN_TIMERS_MAX 8
#define TIMER_MIN_LENGTH 1000 // milliseconds
#define TIMELINE_MIN_LENGTH 900000 // milliseconds
#define INACTIVITY_THRESHOLD 900000 // length of time before refresh throttling in milliseconds
#define INACTIVE_REFRESH_DELAY 1000 // ms between frames after throttling
#define REFRESH_DELAY 1000 // ms between periodic redraws
#define POPUP_REFRESH_DELAY 35 // ms between popup animation frames
#define REFRESH_ALIGNMENT_DELAY 5 // ms after a second boundary to refresh
#define MIN_REFRESH_DELAY 25 // minimum delay when correcting near a boundary
#define PIN_ACTION_CODE_TRUNCATION_LEVEL 100 // both the pin id and action code have to be stored
                                             // in the pins action code
#define PIN_LAUNCH_ARGS_OPEN 10 // when opened from pin, action code to open timer in detail view


/*******************************************************************************
 * MAIN LOCAL VARIABLES
 */

static MenuWindow *s_menu_window = NULL;
static DetailWindow *s_detail_window = NULL;
static DurationWindow *s_duration_window = NULL;
static PopupWindow *s_popup_window = NULL;
#ifndef PBL_PLATFORM_APLITE
// aplite renders the settings as inline rows in the timer list instead of
// these windows, and cannot afford their .text
static SettingsWindow *s_settings_window = NULL;
// the two groups' own sub-menus. the same window type as the settings list above,
// each pointed at a different set of rows -- which is what makes a sub-menu here
// cost a struct and a Window rather than another screenful of code.
static SettingsWindow *s_list_window = NULL;
static SettingsWindow *s_timer_window = NULL;
static OptionWindow *s_option_window = NULL;
// which setting s_option_window is currently pointed at, so the select
// callback knows what it just changed
static SettingId s_option_window_setting = SettingListSortOrder;
#endif
static uint8_t s_countdown_timers_count = 0;
static CountdownTimer *s_countdown_timers[COUNTDOWN_TIMERS_MAX] = {};
static uint8_t s_timer_view_indices[COUNTDOWN_TIMERS_MAX] = {};
static int32_t s_countdown_timer_id_max = 0;
static AppTimer *s_app_timer = NULL;
static int64_t s_last_activity = 0;

static uint16_t prv_get_next_refresh_delay(void) {
  if (popup_window_get_topmost_window(s_popup_window)) {
    return POPUP_REFRESH_DELAY;
  }

  CountdownTimer *next_timer = countdown_timer_list_get_closest_timer(s_countdown_timers,
    s_countdown_timers_count);
  if (next_timer == NULL) {
    return REFRESH_DELAY;
  }

  int64_t remaining = countdown_timer_get_current_time(next_timer);
  if (remaining <= 0) {
    return MIN_REFRESH_DELAY;
  }

  int64_t delay = remaining % REFRESH_DELAY;
  if (delay == 0) {
    delay = REFRESH_DELAY;
  }
  delay += REFRESH_ALIGNMENT_DELAY;
  if (delay < MIN_REFRESH_DELAY) {
    delay = MIN_REFRESH_DELAY;
  }
  return (uint16_t)delay;
}



/*
 * decides whether timer "a" should be listed above timer "b"
 *
 * running timers come before paused ones -- unless Group is Off -- and within
 * each group the most recently used timer (largest last_update) comes first.
 * "Used" means started, paused, or edited -- anything that touches a timer's
 * last_update.
 */

static bool prv_timer_precedes(CountdownTimer *a, CountdownTimer *b) {
  bool a_running = !countdown_timer_get_paused(a);
  bool b_running = !countdown_timer_get_paused(b);
  if (!settings_list_grouping_disabled() && a_running != b_running) {
    return a_running;
  }
  return countdown_timer_get_last_update(a) > countdown_timer_get_last_update(b);
}



/*
 * decides whether timer "a" should be listed above timer "b" when sorting by
 * length
 *
 * strictly shorter wins, so equal-length timers keep the order they arrived in
 * -- which is recency order, since the storage array is always recency-sorted
 */

static bool prv_timer_is_shorter(CountdownTimer *a, CountdownTimer *b) {
  return countdown_timer_get_duration(a) < countdown_timer_get_duration(b);
}



/*
 * an ordering over two timers, as used by prv_sort_timers
 */

typedef bool (*TimerPrecedes)(CountdownTimer *a, CountdownTimer *b);



/*
 * order "timers" in place, placing "a" before "b" whenever precedes(a, b)
 *
 * insertion sort is fine here: the list holds at most COUNTDOWN_TIMERS_MAX
 * entries. it is also stable, which both orderings rely on for tiebreaking.
 */

static void prv_sort_timers(CountdownTimer **timers, uint8_t count, TimerPrecedes precedes) {
  for (uint8_t i = 1; i < count; i++) {
    CountdownTimer *key = timers[i];
    int16_t j = (int16_t)i - 1;
    while (j >= 0 && precedes(key, timers[j])) {
      timers[j + 1] = timers[j];
      j--;
    }
    timers[j + 1] = key;
  }
}



/*
 * rebuild the view slot -> storage index mapping the MenuWindow reads through
 *
 * the storage array is always kept in recency order, so the recency view is
 * just the identity mapping and the duration view is a reordering laid over the
 * top. keeping the two separate means toggling the sort never destroys recency.
 */

static void prv_rebuild_timer_view_indices(void) {
  if (settings_list_sort_by_last_used()) {
    for (uint8_t i = 0; i < s_countdown_timers_count; i++) {
      s_timer_view_indices[i] = i;
    }
    return;
  }

  CountdownTimer *by_duration[COUNTDOWN_TIMERS_MAX];
  memcpy(by_duration, s_countdown_timers, sizeof(CountdownTimer*) * s_countdown_timers_count);
  prv_sort_timers(by_duration, s_countdown_timers_count, prv_timer_is_shorter);
  for (uint8_t i = 0; i < s_countdown_timers_count; i++) {
    s_timer_view_indices[i] = (uint8_t)countdown_timer_list_get_timer_index(s_countdown_timers,
      s_countdown_timers_count, by_duration[i]);
  }
}



/*
 * re-establish the list invariants after any change to the timers
 *
 * every mutation of the timer list funnels through here: the storage array goes
 * back into recency order (running timers on top, most recently used first) and
 * the view mapping is rebuilt to match the selected sort.
 */

static void prv_timers_changed(void) {
  prv_sort_timers(s_countdown_timers, s_countdown_timers_count, prv_timer_precedes);
  prv_rebuild_timer_view_indices();
}



/*
 * promote a just-used timer to the top of its group
 *
 * last_update only has one-second resolution, so several timers touched in the
 * same second compare equal. Moving the touched timer to the front of the array
 * first means the stable sort keeps it ahead of those same-second peers, so the
 * timer the user actually just used ends up on top of its running/paused group.
 */

static void prv_promote_timer(CountdownTimer *countdown_timer) {
  int16_t index = countdown_timer_list_get_timer_index(s_countdown_timers,
    s_countdown_timers_count, countdown_timer);
  if (index > 0) {
    memmove(&s_countdown_timers[1], &s_countdown_timers[0],
      sizeof(CountdownTimer*) * index);
    s_countdown_timers[0] = countdown_timer;
  }
  prv_timers_changed();

  /*
   * the list opens with the cursor on the timer you last used
   *
    * the order is untouched -- a timer created paused still lands below every
     * running one when Group is Running First -- but selection follows use. all five acting
     * paths (create, edit, play/pause, snooze, replay) already funnel through here, so
    * this one place covers them all. delete and timer-expiry never promote,
    * and so never steal the cursor: a deleted timer has no row to land on, and
    * an expired one gets the stronger signal of a PopupWindow.
   */
  for (uint8_t view = 0; view < s_countdown_timers_count; view++) {
    if (s_countdown_timers[s_timer_view_indices[view]] == countdown_timer) {
      menu_window_select_timer_index(s_menu_window, view);
      break;
    }
  }
}



/*
 * move a timer between running and paused, and keep its Timeline pin in step
 *
 * the one rule this enforces: a pin exists for a timer if and only if that
 * timer is running and its duration is at least TIMELINE_MIN_LENGTH. Every
 * running/paused transition in the app goes through here.
 *
 * two orderings are load-bearing:
 *   - delete the pin *before* stopping. countdown_timer_stop rolls the timer's
 *     id, and the pin is identified by that id, so deleting afterwards would
 *     delete nothing.
 *   - start *before* sending the pin, so a pin is only ever sent for a timer
 *     that is definitively running.
 *
 * the pause half is guarded against a call that is not a real transition --
 * pausing an already-paused timer, as "Manually" does on create, neither
 * deletes a pin nor stops anything.
 */

static void prv_set_timer_running(CountdownTimer *countdown_timer, bool running) {
  if (running) {
    if (countdown_timer_get_current_time(countdown_timer) <= 0) {
      countdown_timer_update(countdown_timer,
        countdown_timer_get_duration(countdown_timer), false);
    }
    countdown_timer_start(countdown_timer);
    if (countdown_timer_get_duration(countdown_timer) >= TIMELINE_MIN_LENGTH) {
      phone_send_pin(countdown_timer);
    }
  } else {
    // The pin is deleted on the running->paused *transition*, per the rule.
    // When the timer is already paused -- creating under "Manually", or
    // editing a timer that was already paused -- there is no transition and
    // no pin, so delete nothing. countdown_timer_stop carries the same
    // idempotence guard for the state itself.
    if (!countdown_timer_get_paused(countdown_timer)) {
      if (countdown_timer_get_duration(countdown_timer) >= TIMELINE_MIN_LENGTH) {
        phone_delete_pin(countdown_timer);
      }
      countdown_timer_stop(countdown_timer, &s_countdown_timer_id_max);
    }
  }
}



/*
 * the only place the SettingId enum meets the bools behind it
 */

#ifdef PBL_COLOR
/*
 * push the accent colour into every window that owns a highlight. the setters
 * stay per-window -- that is how the windows were built -- and this is the
 * one place that knows all eight. PBL_COLOR platforms are exactly the ones
 * that own the settings and option windows.
 */
static void prv_apply_highlight_color(void) {
  menu_window_set_highlight_color(s_menu_window, settings_color());
  detail_window_set_highlight_color(s_detail_window, settings_color());
  duration_window_set_highlight_color(s_duration_window, settings_color());
  popup_window_set_highlight_color(s_popup_window, settings_color());
  settings_window_set_highlight_color(s_settings_window, settings_color());
  settings_window_set_highlight_color(s_list_window, settings_color());
  settings_window_set_highlight_color(s_timer_window, settings_color());
  option_window_set_highlight_color(s_option_window, settings_color());
}
#endif

/*
 * apply a setting change
 *
 * settings.c has already stored the new value; what is left here is telling the
 * windows that read it. This is the half that cannot live in settings.c: it
 * reaches into the view mapping and the menu, detail and every tinted window.
 */

static void prv_set_setting(SettingId setting, uint8_t option) {
  settings_set(setting, option);
  switch (setting) {
    case SettingListSortOrder:
      prv_rebuild_timer_view_indices();
      break;
    case SettingListGroup:
      // the storage array itself is grouped, so re-establish both invariants
      prv_timers_changed();
      break;
    case SettingListWrapAround:
      // the menu window owns the cursor, so hand it the new value now
      menu_window_set_wrap_around(s_menu_window, settings_list_wrap_around());
      break;
    case SettingTimerStartMode:
      // nothing renders this; the next timer start reads it
      break;
    case SettingTimerDeleteConfirm:
      // the detail window owns the arming, so hand it the new value now
      detail_window_set_delete_immediately(s_detail_window, settings_timer_delete_immediately());
      break;
    case SettingTimerReplay:
      // nothing renders this; the next popup load reads it
      break;
    case SettingTimerSnoozeLength:
      // dialled through the picker, whose own callback carries the reaction;
      // this switch covers the option-list settings
    case SettingCount:
      // the sentinel is never a setting
      break;
#ifdef PBL_COLOR
    case SettingColor:
      prv_apply_highlight_color();
      break;
#endif
  }
}



/*******************************************************************************
 * CALLBACKS
 */

/*
 * AppTimer callback
 *
 * update callback which determines refresh rate
 */

static void app_timer_callback(void *data) {
  s_app_timer = NULL;

  // check for expired timers
  CountdownTimer *countdown_timer = countdown_timer_check_ended(s_countdown_timers,
    s_countdown_timers_count);

  if (countdown_timer != NULL) {
    // a timer just expired and is now paused; re-sort so, when Group is
    // Running First, it drops below any still-running timers
    prv_timers_changed();
    // deep refresh the DetailWindow in case it was that timer
    detail_window_deep_refresh(s_detail_window);
    // show timer confirmation window
    popup_window_set_countdown_timer(s_popup_window, countdown_timer);
    popup_window_set_title(s_popup_window, "Time's Up!");
    popup_window_set_highlight_color(s_popup_window, PBL_IF_COLOR_ELSE(settings_color(), GColorWhite));
#ifdef PBL_PLATFORM_APLITE
    popup_window_set_image(s_popup_window, RESOURCE_ID_IMAGE_ALARM);
#else
    popup_window_set_pdc(s_popup_window, RESOURCE_ID_ICON_ALARM_CLOCK, true);
#endif
    popup_window_set_auto_close_duration(s_popup_window, 15000);
    popup_window_set_snooze_enabled(s_popup_window, settings_timer_snooze_enabled());
    popup_window_set_replay_enabled(s_popup_window, settings_timer_replay_enabled());
    // the alarm clock leaps upward while ringing, so the title stays below it
    popup_window_set_text_above(s_popup_window, false);
    popup_window_add_action_bar(s_popup_window);
    popup_window_push(s_popup_window, true);
    popup_window_set_vibes();

    // we want the alarm going off to count as activity
    s_last_activity = countdown_timer_get_epoch_ms();
  }

  // refresh
  bool menu_top = menu_window_get_topmost_window(s_menu_window);
  bool detail_top = detail_window_get_topmost_window(s_detail_window);
  bool popup_top = popup_window_get_topmost_window(s_popup_window);
  if (menu_top) menu_window_refresh(s_menu_window);
  if (detail_top) detail_window_refresh(s_detail_window);
  if (popup_top) popup_window_refresh(s_popup_window);

  // check activity
  int64_t inactivity_duration = countdown_timer_get_epoch_ms() - s_last_activity;

  // schedule next refresh
  uint16_t refresh_rate = prv_get_next_refresh_delay();
  if (popup_top) {
    inactivity_duration = 0;
  }
  if (refresh_rate == 0) {
    return;
  }
  // cap refresh rate if inactive
  if (inactivity_duration > INACTIVITY_THRESHOLD) {
    refresh_rate = (refresh_rate > INACTIVE_REFRESH_DELAY) ? refresh_rate : INACTIVE_REFRESH_DELAY;
  }
  s_app_timer = app_timer_register(refresh_rate, app_timer_callback, NULL);
}



/*
 * PopupWindow snooze timer callback
 * snoozes the vibrating timer for the duration the Snooze Length setting picks.
 * the popup hides the snooze icon when the setting is Off, so the delay
 * check is just a guard against a click config that predates the change
 */

static void popup_window_snooze_timer_callback(CountdownTimer *countdown_timer, void *context) {
  int64_t snooze_delay = settings_timer_snooze_delay();
  if (snooze_delay <= 0) {
    return;
  }
  countdown_timer_update(countdown_timer, snooze_delay, false);
  countdown_timer_start(countdown_timer);
  prv_promote_timer(countdown_timer);
  popup_window_pop(s_popup_window, true);
  // show detail if not on top
  if (!detail_window_get_topmost_window(s_detail_window)) {
    detail_window_set_countdown_timer(s_detail_window, countdown_timer);
    detail_window_set_delete_immediately(s_detail_window, settings_timer_delete_immediately());
    detail_window_push(s_detail_window, false);
  }
  detail_window_deep_refresh(s_detail_window);
  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*
 * PopupWindow replay timer callback
 * restarts the expired timer from its original duration, the one-press
 * version of dismiss, select, play that repetitive timers want (stretches,
 * power hours, intervals). the expiry has already paused the timer and
 * zeroed its remaining time, so prv_set_timer_running refills it from the
 * duration and re-sends the Timeline pin, like any other play
 */

static void popup_window_replay_timer_callback(CountdownTimer *countdown_timer, void *context) {
  // the Replay Button setting hides the icon on the next popup load, so this
  // guard is the same protection the snooze callback carries: a click config
  // that predates a just-changed setting
  if (!settings_timer_replay_enabled()) {
    return;
  }
  prv_set_timer_running(countdown_timer, true);
  prv_promote_timer(countdown_timer);
  popup_window_pop(s_popup_window, true);
  // show detail if not on top
  if (!detail_window_get_topmost_window(s_detail_window)) {
    detail_window_set_countdown_timer(s_detail_window, countdown_timer);
    detail_window_set_delete_immediately(s_detail_window, settings_timer_delete_immediately());
    detail_window_push(s_detail_window, false);
  }
  detail_window_deep_refresh(s_detail_window);
  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*
 * PopupWindow stop timer callback
 * cancels the current timer vibration sequence
 */

static void popup_window_stop_timer_callback(void *context) {
  // pop the window
  popup_window_pop(s_popup_window, true);

  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*
 * DurationWindow complete callback
 * simple window to create or edit timer durations
 */

static void duration_window_complete_callback(int64_t duration, void *context) {
  DurationWindow *duration_window = (DurationWindow*)context;

  // The picker can also dial the Snooze Length delay. Zero is Off, so no
  // minimum-duration check -- and nothing below this branch may assume
  // a timer, because this path never touches the list.
  if (duration_window_get_snooze_mode(duration_window)) {
    settings_timer_snooze_delay_set(duration);
    duration_window_pop(duration_window, false);
#ifdef PBL_PLATFORM_APLITE
    // aplite shows the settings as rows in the timer list
    menu_window_refresh(s_menu_window);
#else
    // which settings window is underneath depends on where the setting was
    // reached from, so mark all three dirty, like the option window's callback
    settings_window_refresh(s_settings_window);
    settings_window_refresh(s_list_window);
    settings_window_refresh(s_timer_window);
#endif
    s_last_activity = countdown_timer_get_epoch_ms();
    return;
  }

  CountdownTimer *countdown_timer = duration_window_get_timer(duration_window);
  // check if long enough
  if (duration < TIMER_MIN_LENGTH) {
    duration_window_pop(duration_window, true);
    if (s_app_timer != NULL) {
      app_timer_reschedule(s_app_timer, MIN_REFRESH_DELAY);
    }
    return;
  }

  // check if new timer or editing
  if (countdown_timer == NULL) {
    countdown_timer = countdown_timer_create(duration, &s_countdown_timer_id_max);
    // list_add destroys the tail timer when full, and it knows nothing about
    // Timeline pins -- per the pin rule, that is this layer's job. The victim
    // is the storage tail, and only a running one of at least TIMELINE_MIN_LENGTH
    // can own a pin.
    if (s_countdown_timers_count == COUNTDOWN_TIMERS_MAX) {
      CountdownTimer *evicted = s_countdown_timers[COUNTDOWN_TIMERS_MAX - 1];
      if (!countdown_timer_get_paused(evicted) &&
          countdown_timer_get_duration(evicted) >= TIMELINE_MIN_LENGTH) {
        phone_delete_pin(evicted);
      }
    }
    countdown_timer_list_add(s_countdown_timers, COUNTDOWN_TIMERS_MAX,
      &s_countdown_timers_count, countdown_timer);
    // Start Mode is absolute: under Manually the create path lands the
    // timer paused. The chokepoint skips a stop that is not a real transition,
    // so this is a true no-op -- a fresh timer is already paused and owns no
    // pin.
    prv_set_timer_running(countdown_timer, settings_timer_start_automatically());
    // update visuals
    menu_window_reload_data(s_menu_window);
    menu_window_refresh(s_menu_window);
    detail_window_set_countdown_timer(s_detail_window, countdown_timer);
    detail_window_set_delete_immediately(s_detail_window, settings_timer_delete_immediately());
    duration_window_pop(duration_window, false);
    detail_window_push(s_detail_window, true);
    detail_window_deep_refresh(s_detail_window);
  } else {
    // stop first, while the *old* duration is still in place: a timer edited
    // from above TIMELINE_MIN_LENGTH down to below it still has a stale pin,
    // and only the old duration passes the guard that deletes it
    prv_set_timer_running(countdown_timer, false);
    countdown_timer_update(countdown_timer, duration, true);
    // the same setting governs edit as create: the timer lands in the state
    // the setting names either way
    prv_set_timer_running(countdown_timer, settings_timer_start_automatically());
    detail_window_deep_refresh(s_detail_window);
    duration_window_pop(duration_window, true);
  }

  // keep the list in order and put the cursor on the timer just used
  prv_promote_timer(countdown_timer);

  // refresh now
  if (s_app_timer != NULL) {
    app_timer_reschedule(s_app_timer, MIN_REFRESH_DELAY);
  }

  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*
 * DetailWindow edit timer callback
 * edit the timer currently in the detail view
 */

static void detail_window_edit_timer_callback(CountdownTimer *countdown_timer, void *context) {
  duration_window_set_timer(s_duration_window, countdown_timer);
  duration_window_push(s_duration_window, true);

  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*
 * DetailWindow play pause timer callback
 * plays or pauses the timer currently in the detail view
 */

static void detail_window_playpause_timer_callback(CountdownTimer *countdown_timer, void *context) {
  prv_set_timer_running(countdown_timer, countdown_timer_get_paused(countdown_timer));
  // keep the list in order and put the cursor on the timer just used
  prv_promote_timer(countdown_timer);
  // refresh DetailWindow
  detail_window_deep_refresh(s_detail_window);

  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*
 * DetailWindow delete timer callback
 * delete the timer currently in the detail view
 */

static void detail_window_delete_timer_callback(CountdownTimer *countdown_timer, void *context) {
  // delete the Timeline pin
  if (countdown_timer_get_duration(countdown_timer) >= TIMELINE_MIN_LENGTH) {
    phone_delete_pin(countdown_timer);
  }

  // delete the timer
  int16_t timer_index = countdown_timer_list_get_timer_index(s_countdown_timers,
    s_countdown_timers_count, countdown_timer);
  countdown_timer_destroy(countdown_timer);
  countdown_timer_list_remove(s_countdown_timers, &s_countdown_timers_count, timer_index);
  prv_timers_changed();
  // reload MenuWindow data (no idea why, but this must be called twice or when the last timer
  // is deleted, the "+" cell is stuck at the short cell height)
  menu_window_reload_data(s_menu_window);
  menu_window_reload_data(s_menu_window);
  // pop detail off stack
  detail_window_pop(s_detail_window, true);

  // show timer confirmation window
  popup_window_set_title(s_popup_window, "Timer Deleted");
  popup_window_set_highlight_color(s_popup_window, PBL_IF_COLOR_ELSE(settings_color(), GColorWhite));
  // the shredder drops confetti past the bottom of its bounds, so the
  // title goes above the graphic here
  popup_window_set_text_above(s_popup_window, true);
#ifdef PBL_PLATFORM_APLITE
  popup_window_set_image(s_popup_window, RESOURCE_ID_IMAGE_SHREADER);
  popup_window_set_auto_close_duration(s_popup_window, 1000);
#else
  popup_window_set_pdc(s_popup_window, RESOURCE_ID_ICON_DELETED, false);
  int64_t pdc_duration = popup_window_get_pdc_duration(s_popup_window);
  popup_window_set_auto_close_duration(s_popup_window, pdc_duration);
#endif
  popup_window_remove_action_bar(s_popup_window);
  popup_window_push(s_popup_window, true);
  popup_window_refresh(s_popup_window);

  // refresh immediately
  if (s_app_timer) {
    app_timer_reschedule(s_app_timer, POPUP_REFRESH_DELAY);
  } else {
    s_app_timer = app_timer_register(POPUP_REFRESH_DELAY, app_timer_callback, NULL);
  }

  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*
 * MenuWindow get timer callback
 * gets a pointer to a timer at a specific index
 */

static CountdownTimer *menu_window_get_timer_callback(uint8_t index, void *context) {
  if (index < s_countdown_timers_count) {
    return s_countdown_timers[s_timer_view_indices[index]];
  }
  // error handling
  APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to access timer outside array bounds");
  return NULL;
}



/*
 * MenuWindow get timer count callback
 * get the total number of timers
 */

static uint8_t menu_window_get_timer_count_callback(void *context) {
  return s_countdown_timers_count;
}



/*
 * Settings copy callbacks
 *
 * thin adapters over settings.c, kept because the window callbacks are declared
 * `(uint8_t, void *)` and settings.c speaks in SettingIds. The same pair feeds both
 * renderers: the inline rows in menu_window (only ever drawn on aplite) and, on
 * every other platform, the settings windows.
 */

static const char *settings_name_callback(uint8_t setting, void *context) {
  return settings_name((SettingId)setting);
}

static const char *settings_value_callback(uint8_t setting, void *context) {
  return settings_value((SettingId)setting);
}



#ifndef PBL_PLATFORM_APLITE

/*
 * SettingsWindow clicked callback
 *
 * re-point the one option window at the clicked setting and open it. The labels,
 * their count and the colour swatches all come from settings.c, so the one
 * unusual setting -- Accent Color's palette -- pushes exactly like a plain
 * On/Off pair. Snooze Length is the exception: it dials its delay on the
 * duration picker instead of opening an option list.
 */

static void settings_window_clicked_callback(uint8_t setting, void *context) {
  if ((SettingId)setting == SettingTimerSnoozeLength) {
    // snooze dials on the duration picker, not the option list
    duration_window_set_snooze_mode(s_duration_window, settings_timer_snooze_delay());
    duration_window_push(s_duration_window, true);
    return;
  }
  s_option_window_setting = (SettingId)setting;
  option_window_push(s_option_window, settings_name((SettingId)setting),
    settings_option_labels((SettingId)setting), settings_option_count((SettingId)setting),
    settings_get((SettingId)setting), settings_option_swatches((SettingId)setting), true);
}



/*
 * The top settings list
 *
 * Its two rows above the settings are the group sub-menus; settings.c owns which
 * rows each group holds and what the groups are called, and encodes a group row as
 * an id at or above SettingCount (see SETTINGS_ROW_GROUP in settings.h). So these
 * three callbacks only have to tell a group row from a setting row -- a group has
 * a name and opens another SettingsWindow instead of an option list -- and lean on
 * the plain callbacks for everything else.
 */

static const char *settings_top_name_callback(uint8_t row_id, void *context) {
  if (row_id >= SettingCount) {
    return settings_group_name((SettingsGroup)(row_id - SettingCount));
  }
  return settings_name_callback(row_id, context);
}

static const char *settings_top_value_callback(uint8_t row_id, void *context) {
  if (row_id >= SettingCount) {
    // no value: the row says "List" or "Timer" and nothing else, because what the
    // group is set to is a question the next screen answers
    return NULL;
  }
  return settings_value_callback(row_id, context);
}

static void settings_top_clicked_callback(uint8_t row_id, void *context) {
  if (row_id >= SettingCount) {
    settings_window_push((SettingsGroup)(row_id - SettingCount) == SettingsGroupList ?
      s_list_window : s_timer_window, true);
    return;
  }
  settings_window_clicked_callback(row_id, context);
}



/*
 * OptionWindow selected callback
 *
 * the option window has already popped itself by the time this runs, so a
 * settings window is the topmost again and shows the new value immediately --
 * which one of the two it is depends on where the setting was reached from, so
 * both are marked dirty. the timer list underneath needs no touch: the MenuLayer
 * redraws the rows through the view mapping as soon as it is topmost again.
 */

static void option_window_selected_callback(uint8_t option, void *context) {
  prv_set_setting(s_option_window_setting, option);
  settings_window_refresh(s_settings_window);
  settings_window_refresh(s_list_window);
  settings_window_refresh(s_timer_window);

  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}

#endif  // PBL_PLATFORM_APLITE



/*
 * MenuWindow click callback
 */

static void menu_window_click_callback(MenuRowKind kind, uint8_t row, void *context) {
  switch (kind) {
    case MenuRowAdd: {
      // add a timer: open the duration picker with no timer to edit
      duration_window_set_timer(s_duration_window, NULL);
      duration_window_push(s_duration_window, true);
      break;
    }
    case MenuRowTimer: {
      // show timer in detail window
      const int16_t view_index = menu_window_row_to_timer_index(s_menu_window, row);
      if (view_index < 0) {
        break;
      }
      CountdownTimer *countdown_timer = menu_window_get_timer_callback((uint8_t)view_index,
        context);
      if (countdown_timer == NULL) {
        break;
      }
      detail_window_set_countdown_timer(s_detail_window, countdown_timer);
      detail_window_set_delete_immediately(s_detail_window, settings_timer_delete_immediately());
      detail_window_push(s_detail_window, true);
      detail_window_deep_refresh(s_detail_window);
      if (s_app_timer != NULL) {
        app_timer_reschedule(s_app_timer, MIN_REFRESH_DELAY);
      }
      break;
    }
    case MenuRowSettings: {
      // the cog row only exists off aplite, but the switch arms are compiled
      // everywhere so the row model stays out of #ifdefs
#ifndef PBL_PLATFORM_APLITE
      settings_window_push(s_settings_window, true);
#endif
      break;
    }
    case MenuRowSetting: {
      // on aplite a settings row cycles its options in place, in one press
      const int16_t setting = menu_window_row_to_setting_index(s_menu_window, row);
      if (setting < 0) {
        break;
      }
      if ((SettingId)setting == SettingTimerSnoozeLength) {
        // snooze is dialled, not cycled -- the picker already exists on
        // aplite, so this adds no window to the 24 KB budget
        duration_window_set_snooze_mode(s_duration_window, settings_timer_snooze_delay());
        duration_window_push(s_duration_window, true);
        break;
      }
      const uint8_t next = (settings_get((SettingId)setting) + 1) %
        settings_option_count((SettingId)setting);
      prv_set_setting((SettingId)setting, next);
      if ((SettingId)setting == SettingListSortOrder || (SettingId)setting == SettingListGroup) {
        // the timer order just changed, so reload -- which drops the
        // selection back to the "+" row. put the user back on the row they
        // just pressed.
        menu_window_reload_data(s_menu_window);
        menu_window_select_row(s_menu_window, row);
      }
      // the remaining settings changed only row content, not row count
      menu_window_refresh(s_menu_window);
      break;
    }
  }

  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}



/*******************************************************************************
 * INITIALIZE AND DEINITIALIZE
 */

/*
 * initialize the program
 */

static void initialize(void) {
  // connect to phone
  phone_connect();
  // opt into touch navigation: on touch hardware the system maps taps and
  // swipes on the app's MenuLayers (timers, settings, options) onto button
  // presses. a no-op where the platform has no touch. the duration, detail,
  // and popup windows drive their own recognizers instead, and turn the
  // bridge off for themselves.
  (void)app_touch_navigation_enable(true);
  // load the CountdownTimer data
  if (persist_exists(COUNTDOWN_TIMER_PERSIST_KEY)) {
    countdown_timer_list_load(s_countdown_timers, COUNTDOWN_TIMERS_MAX,
      &s_countdown_timers_count, COUNTDOWN_TIMER_PERSIST_KEY);
  }
  if (persist_exists(COUNTDOWN_TIMER_ID_PERSIST_KEY)) {
    s_countdown_timer_id_max = persist_read_int(COUNTDOWN_TIMER_ID_PERSIST_KEY);
  }
  // the settings. an absent key leaves each at its shipped default, so an
  // upgrading user keeps their timers and inherits the defaults they never set
  settings_load();
  // cancel wakeup
  wakeup_cancel_all();

  // open the restored list with the most recently used timer on top
  prv_timers_changed();

  // create menu window
  MenuWindowCallbacks menu_callbacks = {
    .get_timer = menu_window_get_timer_callback,
    .get_timer_count = menu_window_get_timer_count_callback,
    .get_setting_name = settings_name_callback,
    .get_setting_value = settings_value_callback,
    .clicked = menu_window_click_callback,
  };
  s_menu_window = menu_window_create(menu_callbacks, true);
  menu_window_set_highlight_color(s_menu_window, PBL_IF_COLOR_ELSE(settings_color(), GColorBlack));
  // the cursor needs the setting it was loaded with before the list is walked
  menu_window_set_wrap_around(s_menu_window, settings_list_wrap_around());
  menu_window_refresh(s_menu_window);

  // create detail window
  DetailWindowCallbacks detail_callbacks = {
    .edit_timer = detail_window_edit_timer_callback,
    .playpause_timer = detail_window_playpause_timer_callback,
    .delete_timer = detail_window_delete_timer_callback,
  };
  s_detail_window = detail_window_create(detail_callbacks);
  detail_window_set_highlight_color(s_detail_window,PBL_IF_COLOR_ELSE(settings_color(), GColorWhite));

  // create duration window
  DurationWindowCallbacks duration_callbacks = {
    .duration_complete = duration_window_complete_callback,
  };
  s_duration_window = duration_window_create(duration_callbacks);
  duration_window_set_highlight_color(s_duration_window, PBL_IF_COLOR_ELSE(settings_color(), GColorBlack));

  // create pop-up window
  PopupWindowCallbacks popup_callbacks = {
    .up_click = popup_window_snooze_timer_callback,
    .select_click = popup_window_replay_timer_callback,
    .down_click = popup_window_stop_timer_callback,
  };
  s_popup_window = popup_window_create();
  popup_window_set_action_bar_callbacks(s_popup_window, popup_callbacks);

  // create settings and option windows
  // aplite has neither: its settings are inline rows in the timer list, and
  // the two windows' .text does not fit in its 24 KB alongside the data
#ifndef PBL_PLATFORM_APLITE
  uint8_t top_count, group_count;
  const uint8_t *top_rows = settings_top_rows(&top_count);
  const uint8_t *list_rows = settings_group_rows(SettingsGroupList, &group_count);
  SettingsWindowCallbacks settings_callbacks = {
    .get_name = settings_top_name_callback,
    .get_value = settings_top_value_callback,
    .clicked = settings_top_clicked_callback,
  };
  s_settings_window = settings_window_create(settings_callbacks, "Settings", top_rows, top_count);
  settings_window_set_highlight_color(s_settings_window,
    PBL_IF_COLOR_ELSE(settings_color(), GColorBlack));
  // the two groups' own sub-menus, fed by the plain callbacks: every row in each
  // is a setting, so there is nothing extra to say about them
  SettingsWindowCallbacks group_callbacks = {
    .get_name = settings_name_callback,
    .get_value = settings_value_callback,
    .clicked = settings_window_clicked_callback,
  };
  s_list_window = settings_window_create(group_callbacks, settings_group_name(SettingsGroupList),
    list_rows, group_count);
  const uint8_t *timer_rows = settings_group_rows(SettingsGroupTimer, &group_count);
  s_timer_window = settings_window_create(group_callbacks, settings_group_name(SettingsGroupTimer),
    timer_rows, group_count);
  settings_window_set_highlight_color(s_list_window,
    PBL_IF_COLOR_ELSE(settings_color(), GColorBlack));
  settings_window_set_highlight_color(s_timer_window,
    PBL_IF_COLOR_ELSE(settings_color(), GColorBlack));
  s_option_window = option_window_create(option_window_selected_callback, NULL);
  option_window_set_highlight_color(s_option_window,
    PBL_IF_COLOR_ELSE(settings_color(), GColorBlack));
#endif

  // check wakeup in case launched by pin
  if (launch_reason() == APP_LAUNCH_TIMELINE_ACTION) {
    uint32_t args = launch_get_args() % PIN_ACTION_CODE_TRUNCATION_LEVEL;
    if (args == PIN_LAUNCH_ARGS_OPEN) {
      CountdownTimer *countdown_timer = countdown_timer_list_get_timer_by_id(s_countdown_timers,
        s_countdown_timers_count, launch_get_args() / PIN_ACTION_CODE_TRUNCATION_LEVEL);
      if (countdown_timer != NULL) {
        // show timer in detail window
        detail_window_set_countdown_timer(s_detail_window, countdown_timer);
        detail_window_set_delete_immediately(s_detail_window, settings_timer_delete_immediately());
        detail_window_push(s_detail_window, true);
        detail_window_deep_refresh(s_detail_window);
      }
    }
  }

  // open the duration picker if no timers
  if (s_countdown_timers_count == 0) {
    duration_window_set_timer(s_duration_window, NULL);
    duration_window_push(s_duration_window, true);
  }

  // start the main update timer
  s_app_timer = app_timer_register(prv_get_next_refresh_delay(), app_timer_callback, NULL);


  // log activity
  s_last_activity = countdown_timer_get_epoch_ms();
}

static void prv_add_slice(AppGlanceReloadSession *session, const char *str, time_t expiration_time) {
  const AppGlanceSlice slice = {
    .layout.subtitle_template_string = str,
    .expiration_time = expiration_time
  };
  AppGlanceResult result = app_glance_add_slice(session, slice);
  if (result != APP_GLANCE_RESULT_SUCCESS) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Error : %d", result);
  }
}

// update app glance
static void prv_update_app_glance(AppGlanceReloadSession *session, size_t limit, void *context) {
  // Ensure we have sufficient slices
  if (limit < 1) {
    return;
  }

  time_t expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION;

  CountdownTimer *countdown_timer = countdown_timer_list_get_closest_timer(s_countdown_timers,
    s_countdown_timers_count);

  if (countdown_timer != NULL) {
    expiration_time = time(NULL) + countdown_timer_get_current_time(countdown_timer) / 1000; // expires when the timer is ended
    char buff_glance[40] = {0}; // {time_until(4294967295)|format('%fT')}
    snprintf(buff_glance, sizeof(buff_glance), "{time_until(%ld)|format('%%fT')}", expiration_time);
    prv_add_slice(session, buff_glance, expiration_time);
  } else {
    countdown_timer = countdown_timer_list_get_last_updated_timer(s_countdown_timers, s_countdown_timers_count);
    if (countdown_timer != NULL) {
      expiration_time = countdown_timer_get_last_update(countdown_timer) + SECONDS_PER_HOUR;
      if (expiration_time > time(NULL)) { // don't display the appglance if the timer is paused for > 1 hour
        prv_add_slice(session, countdown_timer_format_own_buff(countdown_timer), expiration_time);
      }
    }
  }
}


/*
 * deinitialize the program
 */

static void deinitialize(void) {
  // cancel the timer if it is still registered
  if (s_app_timer != NULL) {
    app_timer_cancel(s_app_timer);
  }
  // disconnect from phone
  phone_disconnect();

  // persist state
  persist_write_int(PERSIST_VERSION_KEY, PERSIST_VERSION);
  persist_write_int(COUNTDOWN_TIMER_ID_PERSIST_KEY, s_countdown_timer_id_max);
  settings_write();
  countdown_timer_list_save(s_countdown_timers, s_countdown_timers_count,
    COUNTDOWN_TIMER_PERSIST_KEY);
  // schedule the wakeup
  CountdownTimer *countdown_timer = countdown_timer_list_get_closest_timer(s_countdown_timers,
    s_countdown_timers_count);
  if (countdown_timer != NULL) {
    time_t timestamp = time(NULL) + countdown_timer_get_current_time(countdown_timer) / 1000;
    // add one second to ensure it opens straight to the PopupWindow
    wakeup_schedule(timestamp + 1, 0, true);
  }

  // update appglance
  app_glance_reload(prv_update_app_glance, NULL);

  // destroy classes
  popup_window_destroy(s_popup_window);
#ifndef PBL_PLATFORM_APLITE
  option_window_destroy(s_option_window);
  settings_window_destroy(s_timer_window);
  settings_window_destroy(s_list_window);
  settings_window_destroy(s_settings_window);
#endif
  duration_window_destroy(s_duration_window);
  detail_window_destroy(s_detail_window);
  menu_window_destroy(s_menu_window);
  countdown_timer_list_destroy_all(s_countdown_timers, &s_countdown_timers_count);
}



/*
 * main entry point
 */

int main(void) {
  initialize();
  app_event_loop();
  deinitialize();
}
