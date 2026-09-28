# CONTEXT

A glossary for this repo. Not a manual -- just the words whose meaning is not
obvious from the code, and the one platform fact that constrains everything.

## Window naming

A window is named for **what it shows**, not for what the user does on it:

| file | shows |
| --- | --- |
| `menu_window` | the list of timers |
| `detail_window` | one timer |
| `duration_window` | a duration being dialled in |
| `settings_window` | the list of app settings |
| `option_window` | the options for one setting |
| `popup_window` | a popup |

## Terms

- **Duration** -- the length dialled in on the duration picker.
- **Setting** -- one of the app's preferences: `Sort Order`, `Group`, `Wrap
  Around`, `Start Mode`, `Confirm Deletion`, `Snooze Length`, and -- on the
  colour platforms only -- `Accent Color`. Named by `SettingId` in `settings.h`,
  which prefixes the names with the group each belongs to.
- **Group** (of settings) -- `List` (how the timer list behaves: `Sort Order`,
  `Group`, `Wrap Around`) and `Timer` (how a timer behaves: `Start Mode`,
  `Confirm Deletion`, `Snooze Length`). On aplite the groups have no UI, so the
  six are flat inline rows; everywhere else each is a sub-menu off the settings
  list. Beware: `Group` is also the *name of a setting* (the running/paused
  grouping), and `SettingListGroup` is the setting while the group rows are
  `SettingsGroup` ids wrapped by the `SETTINGS_ROW_GROUP()` macro.
- **Option** -- one choice within one setting (`Recency` / `Duration`).
  Every setting has two options except `Snooze Length` -- a duration dialled
  on the picker, where zero means `Off` -- and `Accent Color`, whose options
  are the sixty-four palette swatches in `s_color_values` in `settings.c`. An
  On/Off pair is listed `Off, On`; the shipped default is wherever the false
  static lands, which for `Confirm Deletion` is index 1.
- **Armed delete** -- the in-place confirm state on the detail window's action
  bar, not a separate confirmation screen. It is what `Confirm Deletion: On`
  gives you, and `Off` takes away.

## Storage index vs. view index

`s_countdown_timers[]` is **always** in recency order: most recently used
first, with running timers above paused -- that grouping is the `Group` setting
(`SettingListGroup`), `Running First` by default, and `Off` makes the order purely
by last use.
`s_timer_view_indices[]` lays the selected sort over the top without
disturbing it.

A "timer index" in a `menu_window` signature is a **view** index.

## Platforms

Six targets: `aplite` (Pebble/Pebble Steel), `basalt` (Time/Time Steel),
`chalk` (Time Round), `diorite` (Pebble 2), `emery` (Time 2),
`gabbro` (Round 2 -- colour and round, 260x260 per the SDK's platform table).

The one line that matters: **aplite is the 24 KB platform, where compiled code
lives in the same budget as data.** Adding a window *type* there costs RAM even
if it is never pushed -- though an extra instance of a type already compiled in
costs only a struct and a `Window`, not more code. So the settings UI is inline
rows on aplite and a sub-menu tree (the `List` and `Timer` groups) on every
other platform, all of it drawn by the one `SettingsWindow` type.
