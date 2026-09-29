/*******************************************************************************
 * FILENAME :        settings.h
 *
 * DESCRIPTION :
 *      The app's settings: their values, their copy, and their persistence. The
 *      values and the strings live in settings.c and reach the windows through
 *      callbacks, matching how every other window in this app is fed; the enum
 *      here is what lets a window name a setting in a callback signature without
 *      depending on settings.c.
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-24
 *
 */

#pragma once

#include <pebble.h>

/*
 * Structure:   SettingId
 * ----------------------
 * one of the app's settings. The shipped default is wherever a zero-initialised
 * static lands, and it is the static that carries the default -- not option index
 * 0. For an On/Off setting the options are listed `Off, On` so the pair reads the
 * same everywhere, which puts a default of On at index 1; settings.c's accessors
 * translate the bool to the right index. Every other option list still leads with
 * its default, because there index 0 and the false static agree.
 *
 * SettingColor is a PBL_COLOR member, not a platform one: aplite and diorite
 * are both black and white, so a colour they cannot render must not exist in
 * their copy of this enum. Every renderer sizes itself off SettingCount,
 * which drops the row on both automatically.
 *
 * The rest come in two groups, and the prefix on each name says which: the
 * `SettingList` three change how the timer list behaves, the `SettingTimer` four
 * how a timer itself behaves -- on every platform but aplite each group is a
 * sub-menu of its own. The two groups stay adjacent here because enum order is
 * also the order aplite renders its inline rows in.
 */

typedef enum {
  SettingListSortOrder = 0,
  SettingListGroup,
  SettingListWrapAround,
  SettingTimerStartMode,
  SettingTimerDeleteConfirm,
  SettingTimerSnoozeLength,
  SettingTimerReplay,
#ifdef PBL_COLOR
  SettingColor,
#endif
  SettingCount,
} SettingId;



/*
 * The two groups, as the settings list sees them. Off aplite each is a sub-menu,
 * so a row of the settings list is either a SettingId or one of these -- encoded
 * as the ids just above the enum, where no real setting can collide, so
 * `row_id >= SettingCount` is what tells a group row from a setting row.
 */

typedef enum {
  SettingsGroupList = 0,
  SettingsGroupTimer,
  SettingsGroupCount,
} SettingsGroup;

#define SETTINGS_ROW_GROUP(group) ((uint8_t)(SettingCount + (group)))



/*******************************************************************************
 * VALUES AND COPY
 */

/*
 * Function:    settings_get / settings_set
 * ----------------------------------------
 * read and write one setting as an option index. `settings_set` changes the
 * value and nothing else -- the reaction a change deserves (re-sort the view,
 * tell the detail window, recolour the app) is the caller's, because those
 * reactions belong to the windows.
 */

uint8_t settings_get(SettingId setting);
void    settings_set(SettingId setting, uint8_t option);

/*
 * Function:    settings_option_count
 * ----------------------------------
 * how many options a setting offers: two for the On/Off and paired settings,
 * the whole palette for Accent Color. Snooze Length is dialled on the
 * duration picker rather than chosen from a list, so it never asks this. The
 * aplite row cycle and the option window both walk a setting's options through
 * here rather than assuming two.
 */

uint8_t settings_option_count(SettingId setting);

/*
 * Function:    settings_name / settings_value
 * -------------------------------------------
 * the copy: a setting's display name, and the label of the option it is currently
 * set to. Two renderers read one copy -- the inline rows in the timer list on
 * aplite, and the settings/option windows everywhere else.
 */

const char *settings_name(SettingId setting);
const char *settings_value(SettingId setting);

/*
 * Function:    settings_option_labels / settings_option_swatches
 * -------------------------------------------------------------
 * the option list a setting offers, for the option window to draw: the labels
 * (the two of a paired setting, the whole palette for Accent Color), and the
 * swatches beside them -- NULL for every setting but Accent Color, and NULL
 * for Snooze Length, which is dialled on the picker. `settings_option_count`
 * says how many labels.
 */

const char *const *settings_option_labels(SettingId setting);
const GColor *settings_option_swatches(SettingId setting);



/*******************************************************************************
 * THE SETTINGS LIST'S ROWS
 *
 * Off aplite only: which settings each group's sub-menu shows, and the top-level
 * list that leads with the two group rows. settings.c owns the tables; the
 * windows that draw them live in main.c.
 */

const uint8_t *settings_group_rows(SettingsGroup group, uint8_t *count);
const uint8_t *settings_top_rows(uint8_t *count);
const char *settings_group_name(SettingsGroup group);



/*******************************************************************************
 * BEHAVIOUR READS
 *
 * The settings as the rest of the app acts on them, not as option indices. These
 * are how a callback asks "does the list wrap?" without knowing that Wrap Around
 * stores a bool whose false means Off, or that Sort Order's on-flash int runs
 * opposite to the variable.
 */

bool settings_list_grouping_disabled(void);
bool settings_list_sort_by_last_used(void);
bool settings_list_wrap_around(void);
bool settings_timer_start_automatically(void);
bool settings_timer_delete_immediately(void);
bool settings_timer_snooze_enabled(void);
bool settings_timer_replay_enabled(void);
int64_t settings_timer_snooze_delay(void);
// The picker dials the delay instead of the option list; zero is Off
void settings_timer_snooze_delay_set(int64_t delay_ms);
#ifdef PBL_COLOR
GColor settings_color(void);
#endif



/*******************************************************************************
 * PERSISTENCE
 */

/*
 * settings_load reads every setting from flash; a key that is absent leaves the
 * shipped default (the false static) in place. settings_write stores them all
 * back. Called from main.c's initialize and deinitialize.
 */

void settings_load(void);
void settings_write(void);
