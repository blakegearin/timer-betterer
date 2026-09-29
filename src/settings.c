/*******************************************************************************
 * FILENAME :        settings.c
 *
 * DESCRIPTION :
 *      The app's settings: their values, their copy, the group tables the
 *      settings list draws, and their persistence. Everything here is data and
 *      its translation to and from option indices; the reactions a change causes
 *      in the windows live in main.c, which reads the values back through the
 *      behaviour getters below rather than touching these statics.
 *
 * PUBLIC FUNCTIONS :
 *      uint8_t     settings_get(SettingId setting);
 *      void        settings_set(SettingId setting, uint8_t option);
 *      uint8_t     settings_option_count(SettingId setting);
 *      const char *settings_name(SettingId setting);
 *      const char *settings_value(SettingId setting);
 *      const uint8_t *settings_group_rows(SettingsGroup group, uint8_t *count);
 *      const uint8_t *settings_top_rows(uint8_t *count);
 *      const char *settings_group_name(SettingsGroup group);
 *      bool        settings_list_grouping_disabled(void);
 *      bool        settings_list_sort_by_last_used(void);
 *      bool        settings_list_wrap_around(void);
 *      bool        settings_timer_start_automatically(void);
 *      bool        settings_timer_delete_immediately(void);
 *      bool        settings_timer_snooze_enabled(void);
 *      bool        settings_timer_replay_enabled(void);
 *      int64_t     settings_timer_snooze_delay(void);
 *      void        settings_timer_snooze_delay_set(int64_t delay_ms);
 *      GColor      settings_color(void);            // PBL_COLOR only
 *      void        settings_load(void);
 *      void        settings_write(void);
 *
 * AUTHOR :     Blake Gearin        START DATE :    2026-09-24
 *
 */

#include <pebble.h>
#include "settings.h"

// The on-flash keys. COUNTDOWN_TIMER_PERSIST_KEY and its id sibling stay in
// main.c -- those are the timer list, not a setting.
//
// a key's integer is part of the stored contract: renaming the #define is
// fine, changing the number silently drops what users stored under it. Sort
// Order's int keeps meaning "1 = sort by duration" even though the shipped
// default is duration; the inversion lives in settings_load/write
#define TIMER_SORT_BY_DURATION_PERSIST_KEY 9938472
#define TIMER_GROUPING_DISABLED_PERSIST_KEY 73849201
#define TIMER_START_MANUALLY_PERSIST_KEY 51827394
#define TIMER_DELETE_IMMEDIATELY_PERSIST_KEY 68013925
#define TIMER_HIGHLIGHT_COLOR_PERSIST_KEY 19283746
// Snooze is a dialled duration, stored in milliseconds
#define TIMER_SNOOZE_MS_PERSIST_KEY 84720913
#define TIMER_WRAP_AROUND_PERSIST_KEY 64718293
#define TIMER_REPLAY_DISABLED_PERSIST_KEY 30194728

/*******************************************************************************
 * VALUES
 */

// naming rule: every setting's bool is named so that false is the shipped
// default. statics zero-initialise and an absent persist key leaves them
// untouched, so "no key yet" means "the shipped default" with no default table
// to keep in sync -- and a fresh install lands there automatically. The rule
// names the variables, not the on-flash ints: stored values keep their own
// meanings, so two settings are inverted at the load/store boundary.
static bool s_list_sort_by_last_used = false;
static bool s_list_grouping_disabled = false;
// whether a step past either end of the timer list lands on the other end
static bool s_list_wrap_around_enabled = false;
static bool s_timer_start_automatically = false;
static bool s_timer_delete_immediately = false;
// the Replay Button ships On, so per the naming rule the bool names its
// opposite -- disabled -- and the accessors invert, like Confirm Deletion's
static bool s_timer_replay_disabled = false;
// The Snooze Length setting is a dialled duration in milliseconds, not an
// index into a list. Zero is Off. The shipped default -- 2 minutes -- lives in
// this initialiser; an absent persist key leaves it untouched, like the accent
// colour's
#define SNOOZE_DEFAULT_MS 120000
static int64_t s_timer_snooze_delay_ms = SNOOZE_DEFAULT_MS;
#ifdef PBL_COLOR
// the app's accent colour. not a bool, so the false-is-default rule above
// cannot name it; the job is done here instead -- this initialiser is the
// shipped default, and an absent persist key leaves it alone
static GColor s_highlight_color = GColorMalachite;
#endif

/*******************************************************************************
 * COPY
 */

/*
 * one home for all the settings strings. the shipped default is wherever the
 * false static lands, per settings.h, and it is the accessors -- not the table
 * position -- that map a bool to its index. On/Off pairs are listed `Off, On` so
 * they read alike, which is why Confirm Deletion's default (`On`) is the second
 * entry. this is an enum and a string table that generates no UI. it exists
 * because aplite renders the same settings as rows in the timer list while the
 * other platforms render them in the settings window: two renderers, one copy.
 */
static const char *const s_setting_names[SettingCount] = {
  "Sort Order", "Group", "Wrap Around", "Start Mode", "Confirm Deletion",
  "Snooze Length", "Replay Button",
#ifdef PBL_COLOR
  "Accent Color",
#endif
};
static const char *const s_setting_options[SettingCount][2] = {
  { "Duration",      "Recency"       },
  { "Running First", "Off"           },
  { "Off",           "On"            },
  { "Manually",      "Automatically" },
  { "Off",           "On"            },  // On = confirm first, the shipped default
  { NULL, NULL },  // Snooze Length's "option" is a dialled duration, not this table
  { "Off",           "On"            },  // On = the shipped default, like Confirm Deletion
#ifdef PBL_COLOR
  { NULL, NULL },  // Accent Color's options are the palette below, not this table
#endif
};

/*
 * The Snooze Length setting's delay -- how long the alarm waits before going
 * off again when the snooze button is pressed -- is dialled on the duration
 * picker, not chosen from a list. Zero means Off: the popup shows no snooze
 * icon at all.
 */

#ifdef PBL_COLOR
// the Accent Color setting's options: all sixty-four colours a colour platform can
// render, in rainbow order -- red, orange, yellow, green, blue, indigo,
// violet, dark to light within each band -- with the four true greys last.
// The live preview on the picker (option_window.c) is what makes neighbours
// like "Icterine" and "Pastel Yellow" tellable apart; the names are the
// SDK's, spaced for reading.
// index 0 is the shipped default, per the rule in settings.h, and Picton
// Blue follows it: the two colours the app has ever shipped with, kept at
// the top where the cursor lands, before the sweep.
#define COLOR_OPTIONS 64
static const char *const s_color_names[COLOR_OPTIONS] = {
  "Malachite",                 "Picton Blue",               "Bulgarian Rose",            "Dark Candy Apple Red",
  "Jazzberry Jam",             "Red",                       "Folly",                     "Rose Vale",
  "Sunset Orange",             "Brilliant Rose",            "Melon",                     "Windsor Tan",
  "Orange",                    "Chrome Yellow",             "Rajah",                     "Army Green",
  "Limerick",                  "Brass",                     "Yellow",                    "Icterine",
  "Pastel Yellow",             "Kelly Green",               "Spring Bud",                "Inchworm",
  "Dark Green",                "Midnight Green",            "Islamic Green",             "Jaeger Green",
  "Tiffany Blue",              "May Green",                 "Cadet Blue",                "Green",
  "Medium Spring Green",       "Bright Green",              "Cyan",                      "Screamin Green",
  "Medium Aquamarine",         "Electric Blue",             "Mint Green",                "Celeste",
  "Oxford Blue",               "Duke Blue",                 "Blue",                      "Cobalt Blue",
  "Blue Moon",                 "Liberty",                   "Very Light Blue",           "Vivid Cerulean",
  "Baby Blue Eyes",            "Indigo",                    "Electric Ultramarine",      "Vivid Violet",
  "Lavender Indigo",           "Imperial Purple",           "Purple",                    "Fashion Magenta",
  "Magenta",                   "Purpureus",                 "Shocking Pink",             "Rich Brilliant Lavender",
  "Black",                     "Dark Gray",                 "Light Gray",                "White",
};
static const GColor s_color_values[COLOR_OPTIONS] = {
  GColorMalachite,             GColorPictonBlue,            GColorBulgarianRose,         GColorDarkCandyAppleRed,
  GColorJazzberryJam,          GColorRed,                   GColorFolly,                 GColorRoseVale,
  GColorSunsetOrange,          GColorBrilliantRose,         GColorMelon,                 GColorWindsorTan,
  GColorOrange,                GColorChromeYellow,          GColorRajah,                 GColorArmyGreen,
  GColorLimerick,              GColorBrass,                 GColorYellow,                GColorIcterine,
  GColorPastelYellow,          GColorKellyGreen,            GColorSpringBud,             GColorInchworm,
  GColorDarkGreen,             GColorMidnightGreen,         GColorIslamicGreen,          GColorJaegerGreen,
  GColorTiffanyBlue,           GColorMayGreen,              GColorCadetBlue,             GColorGreen,
  GColorMediumSpringGreen,     GColorBrightGreen,           GColorCyan,                  GColorScreaminGreen,
  GColorMediumAquamarine,      GColorElectricBlue,          GColorMintGreen,             GColorCeleste,
  GColorOxfordBlue,            GColorDukeBlue,              GColorBlue,                  GColorCobaltBlue,
  GColorBlueMoon,              GColorLiberty,               GColorVeryLightBlue,         GColorVividCerulean,
  GColorBabyBlueEyes,          GColorIndigo,                GColorElectricUltramarine,   GColorVividViolet,
  GColorLavenderIndigo,        GColorImperialPurple,        GColorPurple,                GColorFashionMagenta,
  GColorMagenta,               GColorPurpureus,             GColorShockingPink,          GColorRichBrilliantLavender,
  GColorBlack,                 GColorDarkGray,              GColorLightGray,             GColorWhite,
};
#endif

/*******************************************************************************
 * THE TWO GROUPS
 *
 * The settings come in two groups, named by their enum prefixes: the
 * `SettingList` three change how the timer list behaves, the `SettingTimer` four
 * how a timer itself behaves. On every platform but aplite each group is a
 * sub-menu of its own, so the settings list shows two rows -- `List` and `Timer`
 * -- with the one setting that belongs to neither, Accent Color, beside them.
 *
 * aplite has none of this: every setting is an inline row in its timer list,
 * grouped or not, and it cannot afford the windows this grouping needs. Because
 * the group tables and their accessors are read only by main.c's sub-menu wiring
 * -- itself compiled out on aplite -- the whole block is compiled out there too,
 * so the bytes do not touch aplite's 24 KB.
 */

#ifndef PBL_PLATFORM_APLITE

static const char *const s_group_names[SettingsGroupCount] = {
  "List", "Timer",
};
static const uint8_t s_list_setting_rows[] = {
  SettingListSortOrder, SettingListGroup, SettingListWrapAround,
};
static const uint8_t s_timer_setting_rows[] = {
  SettingTimerStartMode, SettingTimerDeleteConfirm, SettingTimerSnoozeLength,
  SettingTimerReplay,
};
static const uint8_t s_settings_rows[] = {
  SETTINGS_ROW_GROUP(SettingsGroupList),
  SETTINGS_ROW_GROUP(SettingsGroupTimer),
#ifdef PBL_COLOR
  SettingColor,
#endif
};

#endif  // PBL_PLATFORM_APLITE

/*******************************************************************************
 * API -- VALUES AND COPY
 */

uint8_t settings_get(SettingId setting) {
  switch (setting) {
    case SettingListSortOrder:
      return s_list_sort_by_last_used ? 1 : 0;
    case SettingListGroup:
      return s_list_grouping_disabled ? 1 : 0;
    case SettingListWrapAround:
      return s_list_wrap_around_enabled ? 1 : 0;
    case SettingTimerStartMode:
      return s_timer_start_automatically ? 1 : 0;
    case SettingTimerDeleteConfirm:
      // Off is listed first but On -- confirm first -- is the shipped default, so
      // the index and the bool run opposite ways here. see the note on the table.
      return s_timer_delete_immediately ? 0 : 1;
    case SettingTimerReplay:
      // the same inversion as Confirm Deletion: Off first, On shipped
      return s_timer_replay_disabled ? 0 : 1;
#ifdef PBL_COLOR
    case SettingColor:
      for (uint8_t i = 0; i < COLOR_OPTIONS; i++) {
        if (gcolor_equal(s_color_values[i], s_highlight_color)) {
          return i;
        }
      }
      return 0;  // a colour outside the palette reads as the default
#endif
    case SettingTimerSnoozeLength:
      // no option index to read; its value is the dialled delay, and
      // settings_value formats that directly
    case SettingCount:
      // the sentinel is never a setting; reached only by a caller who
      // cast a row id it should have checked first
      return 0;
  }
  return 0;  // unreachable for in-enum values; keeps the non-void contract
}

void settings_set(SettingId setting, uint8_t option) {
  switch (setting) {
    case SettingListSortOrder:
      s_list_sort_by_last_used = (option != 0);
      break;
    case SettingListGroup:
      s_list_grouping_disabled = (option != 0);
      break;
    case SettingListWrapAround:
      s_list_wrap_around_enabled = (option != 0);
      break;
    case SettingTimerStartMode:
      s_timer_start_automatically = (option != 0);
      break;
    case SettingTimerDeleteConfirm:
      // option 0 is "Off" -- no confirmation -- which is the same thing as
      // deleting immediately. inverse of the index, per the note in settings_get
      s_timer_delete_immediately = (option == 0);
      break;
    case SettingTimerReplay:
      // option 0 is "Off", which is the disabled state the bool names
      s_timer_replay_disabled = (option == 0);
      break;
#ifdef PBL_COLOR
    case SettingColor:
      s_highlight_color = s_color_values[option];
      break;
#endif
    case SettingTimerSnoozeLength:
      // dialled, not chosen; see settings_timer_snooze_delay_set
    case SettingCount:
      // the sentinel is never a setting
      break;
  }
}

uint8_t settings_option_count(SettingId setting) {
  switch (setting) {
    // Snooze Length is dialled on the picker, not cycled through options here
#ifdef PBL_COLOR
    case SettingColor:
      return COLOR_OPTIONS;
#endif
    default:
      return 2;
  }
}

const char *const *settings_option_labels(SettingId setting) {
#ifdef PBL_COLOR
  if (setting == SettingColor) {
    return s_color_names;
  }
#endif
  if (setting == SettingTimerSnoozeLength) {
    // no labels to offer -- the setting dials on the picker
    return NULL;
  }
  if (setting < SettingCount) {
    return s_setting_options[setting];
  }
  // error handling
  APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to access setting outside the enum");
  return s_setting_options[0];
}

const GColor *settings_option_swatches(SettingId setting) {
#ifdef PBL_COLOR
  if (setting == SettingColor) {
    return s_color_values;
  }
#else
  (void)setting;
#endif
  return NULL;
}

const char *settings_name(SettingId setting) {
  if (setting < SettingCount) {
    return s_setting_names[setting];
  }
  // error handling
  APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to access setting outside the enum");
  return "";
}

/*
 * The Snooze Length row's value is a dialled duration, not a list label, so it
 * gets its own formatter. Compact clock style: "2 Min" survives as "2:00", an
 * hour-plus as "1:05:30", seconds-only as "45 Sec".
 */
static const char *prv_snooze_label(void) {
  static char buff[24];
  if (s_timer_snooze_delay_ms <= 0) {
    return "Off";
  }
  int h = (int)(s_timer_snooze_delay_ms / 3600000);
  int m = (int)(s_timer_snooze_delay_ms % 3600000 / 60000);
  int s = (int)(s_timer_snooze_delay_ms % 60000 / 1000);
  if (h > 0) {
    snprintf(buff, sizeof(buff), "%d:%02d:%02d", h, m, s);
  } else if (m > 0) {
    snprintf(buff, sizeof(buff), "%d:%02d", m, s);
  } else {
    snprintf(buff, sizeof(buff), "%d Sec", s);
  }
  return buff;
}

const char *settings_value(SettingId setting) {
  if (setting >= SettingCount) {
    // error handling
    APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to access setting outside the enum");
    return "";
  }
#ifdef PBL_COLOR
  if (setting == SettingColor) {
    return s_color_names[settings_get(SettingColor)];
  }
#endif
  if (setting == SettingTimerSnoozeLength) {
    return prv_snooze_label();
  }
  return s_setting_options[setting][settings_get(setting)];
}

#ifndef PBL_PLATFORM_APLITE
const uint8_t *settings_group_rows(SettingsGroup group, uint8_t *count) {
  if (group == SettingsGroupList) {
    *count = sizeof(s_list_setting_rows) / sizeof(s_list_setting_rows[0]);
    return s_list_setting_rows;
  }
  *count = sizeof(s_timer_setting_rows) / sizeof(s_timer_setting_rows[0]);
  return s_timer_setting_rows;
}

const uint8_t *settings_top_rows(uint8_t *count) {
  *count = sizeof(s_settings_rows) / sizeof(s_settings_rows[0]);
  return s_settings_rows;
}

const char *settings_group_name(SettingsGroup group) {
  if (group < SettingsGroupCount) {
    return s_group_names[group];
  }
  // error handling
  APP_LOG(APP_LOG_LEVEL_ERROR, "Attempted to access settings group outside the enum");
  return "";
}
#endif  // PBL_PLATFORM_APLITE

/*******************************************************************************
 * API -- BEHAVIOUR READS
 */

bool settings_list_grouping_disabled(void) {
  return s_list_grouping_disabled;
}

bool settings_list_sort_by_last_used(void) {
  return s_list_sort_by_last_used;
}

bool settings_list_wrap_around(void) {
  return s_list_wrap_around_enabled;
}

bool settings_timer_start_automatically(void) {
  return s_timer_start_automatically;
}

bool settings_timer_delete_immediately(void) {
  return s_timer_delete_immediately;
}

bool settings_timer_snooze_enabled(void) {
  return s_timer_snooze_delay_ms > 0;
}

bool settings_timer_replay_enabled(void) {
  return !s_timer_replay_disabled;
}

int64_t settings_timer_snooze_delay(void) {
  return s_timer_snooze_delay_ms;
}

// The duration picker submits a dialled snooze delay; zero means Off
void settings_timer_snooze_delay_set(int64_t delay_ms) {
  // sanity bound is the largest dial the picker can produce, 23:59:59;
  // anything past it reads as Off rather than a nonsense delay
  s_timer_snooze_delay_ms = (delay_ms > 0 && delay_ms < 86400000) ? delay_ms : 0;
}

#ifdef PBL_COLOR
GColor settings_color(void) {
  return s_highlight_color;
}
#endif

/*******************************************************************************
 * API -- PERSISTENCE
 */

void settings_load(void) {
  if (persist_exists(TIMER_SORT_BY_DURATION_PERSIST_KEY)) {
    // the stored int means 1 = sort by duration; the default is duration, so
    // the variable is its inverse
    s_list_sort_by_last_used = (persist_read_int(TIMER_SORT_BY_DURATION_PERSIST_KEY) == 0);
  }
  if (persist_exists(TIMER_GROUPING_DISABLED_PERSIST_KEY)) {
    // stores the bool's own meaning: no inversion at the load boundary
    s_list_grouping_disabled = (persist_read_int(TIMER_GROUPING_DISABLED_PERSIST_KEY) != 0);
  }
  if (persist_exists(TIMER_WRAP_AROUND_PERSIST_KEY)) {
    // the same: stores the bool's own meaning
    s_list_wrap_around_enabled = (persist_read_int(TIMER_WRAP_AROUND_PERSIST_KEY) != 0);
  }
  if (persist_exists(TIMER_START_MANUALLY_PERSIST_KEY)) {
    // 1 = start manually; the default is automatic, so the variable is its inverse
    s_timer_start_automatically = (persist_read_int(TIMER_START_MANUALLY_PERSIST_KEY) == 0);
  }
  if (persist_exists(TIMER_DELETE_IMMEDIATELY_PERSIST_KEY)) {
    s_timer_delete_immediately = (persist_read_int(TIMER_DELETE_IMMEDIATELY_PERSIST_KEY) != 0);
  }
  if (persist_exists(TIMER_REPLAY_DISABLED_PERSIST_KEY)) {
    // stores the bool's own meaning: 1 = replay off
    s_timer_replay_disabled = (persist_read_int(TIMER_REPLAY_DISABLED_PERSIST_KEY) != 0);
  }
  if (persist_exists(TIMER_SNOOZE_MS_PERSIST_KEY)) {
    int32_t saved = persist_read_int(TIMER_SNOOZE_MS_PERSIST_KEY);
    s_timer_snooze_delay_ms = (saved >= 0 && saved < 86400000) ? saved : SNOOZE_DEFAULT_MS;
  }
#ifdef PBL_COLOR
  if (persist_exists(TIMER_HIGHLIGHT_COLOR_PERSIST_KEY)) {
    s_highlight_color = (GColor) {
      .argb = (uint8_t)persist_read_int(TIMER_HIGHLIGHT_COLOR_PERSIST_KEY)
    };
  }
#endif
}

void settings_write(void) {
  persist_write_int(TIMER_SORT_BY_DURATION_PERSIST_KEY, s_list_sort_by_last_used ? 0 : 1);
  persist_write_int(TIMER_GROUPING_DISABLED_PERSIST_KEY, s_list_grouping_disabled ? 1 : 0);
  persist_write_int(TIMER_WRAP_AROUND_PERSIST_KEY, s_list_wrap_around_enabled ? 1 : 0);
  persist_write_int(TIMER_START_MANUALLY_PERSIST_KEY, s_timer_start_automatically ? 0 : 1);
  persist_write_int(TIMER_DELETE_IMMEDIATELY_PERSIST_KEY, s_timer_delete_immediately ? 1 : 0);
  persist_write_int(TIMER_REPLAY_DISABLED_PERSIST_KEY, s_timer_replay_disabled ? 1 : 0);
  persist_write_int(TIMER_SNOOZE_MS_PERSIST_KEY, (int32_t)s_timer_snooze_delay_ms);
#ifdef PBL_COLOR
  persist_write_int(TIMER_HIGHLIGHT_COLOR_PERSIST_KEY, s_highlight_color.argb);
#endif
}
