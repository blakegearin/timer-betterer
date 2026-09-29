/*******************************************************************************
 * FILENAME :        menu_window.h
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

#pragma once

#include <pebble.h>
#include "countdown_timer.h"



/*******************************************************************************
 * ROW MODEL
 */

/*
 * Enumeration:   MenuRowKind
 * ---------------------------
 * what a row of the menu list holds. the whole row layout of the window is
 * decided by one classifier, menu_window_row_kind.
 *
 * MenuRowSettings is the cog row and is only ever returned off aplite;
 * MenuRowSetting is one inline settings row and is only ever returned on
 * aplite. Both values are compiled everywhere: an unused enumerator costs
 * nothing and keeps the type out of #ifdefs.
 */

typedef enum {
  MenuRowAdd,       //< the "+" row, row 0 while the list has room; gone at capacity
  MenuRowTimer,     //< one timer
  MenuRowSettings,  //< the cog row that opens the settings window
  MenuRowSetting,   //< one inline setting row (aplite only)
} MenuRowKind;



/*******************************************************************************
 * CALLBACK DECLARATIONS
 */

/*
 * Callback:    MenuWindowGetTimer
 * -------------------------------
 * gets a pointer to a specific CountdownTimer
 */

typedef CountdownTimer* (*MenuWindowGetTimer)(uint8_t index, void *context);



/*
 * Callback:    MenuWindowGetTimerCount
 * ------------------------------------
 * gets the number of CountdownTimers to be displayed in the menu
 */

typedef uint8_t (*MenuWindowGetTimerCount)(void *context);



/*
 * Callback:    MenuWindowIsFull
 * -----------------------------
 * gets whether the timer list has reached its capacity: the "+" row hides
 * itself at that point, so the list forces a delete or an edit instead of
 * ever evicting a timer
 */

typedef bool (*MenuWindowIsFull)(void *context);



/*
 * Callback:    MenuWindowGetSettingName
 * -------------------------------------
 * gets the display name of one setting, e.g. "Sort Order"
 */

typedef const char *(*MenuWindowGetSettingName)(uint8_t setting, void *context);



/*
 * Callback:    MenuWindowGetSettingValue
 * --------------------------------------
 * gets the label of the option a setting is currently set to
 */

typedef const char *(*MenuWindowGetSettingValue)(uint8_t setting, void *context);



/*
 * Callback:    MenuWindowClickCallback
 * ------------------------------------
 * called when a row is clicked on in the menu layer, with the kind of row
 * it was so main.c can switch on it instead of asking back
 */

typedef void (*MenuWindowClickCallback)(MenuRowKind kind, uint8_t row, void *context);



/*
 * Structure:   MenuWindowCallbacks
 * --------------------------------
 * structure containing all MenuWindow callbacks
 *
 * the setting callbacks feed the inline settings rows and are wired on every
 * platform, but only ever invoked on aplite.
 */

typedef struct MenuWindowCallbacks {
  MenuWindowGetTimer get_timer;
  MenuWindowGetTimerCount get_timer_count;
  MenuWindowIsFull is_full;
  MenuWindowGetSettingName get_setting_name;
  MenuWindowGetSettingValue get_setting_value;
  MenuWindowClickCallback clicked;
} MenuWindowCallbacks;



/*******************************************************************************
 * STRUCTURE DECLARATION
 */

/*
 * Structure:   MenuWindow
 * -----------------------
 * main structure containing all data for a MenuWindow
 */

typedef struct MenuWindow MenuWindow;



/*******************************************************************************
 * API FUNCTIONS
 */

/*
 * Function:    menu_window_create
 * -------------------------------
 * creates a new MenuWindow on the stack
 *
 *  menu_window_callbacks: callbacks for communication
 *  animated: whether to push the new window with animation
 *
 *  returns: a pointer to a new MenuWindow structure
 */

MenuWindow *menu_window_create(MenuWindowCallbacks menu_window_callbacks, bool animated);



/*
 * Function:    menu_window_destroy
 * --------------------------------
 * destroys an existing MenuWindow
 *
 *  menu_window: a pointer to the MenuWindow being destroyed
 */

void menu_window_destroy(MenuWindow *menu_window);



/*
 * Function:    menu_window_get_topmost_window
 * -------------------------------------------
 * gets whether it is the topmost window or not
 *
 *  menu_window: a pointer to the MenuWindow being checked
 *
 *  returns: a boolean indicating if it is the topmost window
 */

bool menu_window_get_topmost_window(MenuWindow *menu_window);



/*
 * Function:    menu_window_refresh
 * --------------------------------
 * redraws the menu window
 *
 *  menu_window: a pointer to the MenuWindow being refreshed
 */

void menu_window_refresh(MenuWindow *menu_window);



/*
 * Function:    menu_window_reload_data
 * ------------------------------------
 * reload the menu layer's data
 *
 *  menu_window: a pointer to the window for which to reload the data
 */

void menu_window_reload_data(MenuWindow *menu_window);



/*
 * Function:    menu_window_select_row
 * -----------------------------------
 * move the menu layer's selection to a given row, ignoring out of range rows
 *
 *  menu_window: a pointer to the window whose selection to move
 *  row: the row to select
 */

void menu_window_select_row(MenuWindow *menu_window, uint8_t row);



/*
 * Function:    menu_window_select_timer_index
 * -------------------------------------------
 * move the menu layer's selection onto the row displaying a given timer.
 * the inverse of menu_window_row_to_timer_index: the row layout is this
 * window's business, so callers must not add one to a timer index themselves.
 *
 *  menu_window: a pointer to the window whose selection to move
 *  view_index: the view index of the timer to select
 */

void menu_window_select_timer_index(MenuWindow *menu_window, uint8_t view_index);



/*
 * Function:    menu_window_row_kind
 * ---------------------------------
 * gets what a menu row holds: the "+", a timer, the cog row (off aplite),
 * or one inline setting row (on aplite)
 *
 *  menu_window: a pointer to the window the row belongs to
 *  row: the row to classify
 *
 * returns: the MenuRowKind of the row
 */

MenuRowKind menu_window_row_kind(MenuWindow *menu_window, uint8_t row);



/*
 * Function:    menu_window_row_to_timer_index
 * -------------------------------------------
 * maps a menu row onto the index of the timer it displays
 *
 *  menu_window: a pointer to the window the row belongs to
 *  row: the row to map
 *
 * returns: the timer index, or -1 if the row does not hold a timer
 */

int16_t menu_window_row_to_timer_index(MenuWindow *menu_window, uint8_t row);



/*
 * Function:    menu_window_row_to_setting_index
 * ---------------------------------------------
 * maps an aplite settings row onto the setting it displays
 *
 *  menu_window: a pointer to the window the row belongs to
 *  row: the row to map
 *
 * returns: the SettingId as an integer, or -1 if the row does not hold one
 */

int16_t menu_window_row_to_setting_index(MenuWindow *menu_window, uint8_t row);



/*
 * Function:    menu_window_set_highlight_color
 * --------------------------------------------
 * sets the over-all color scheme of the window
 *
 *  color: the GColor to set the highlight to
 */

void menu_window_set_highlight_color(MenuWindow *menu_window, GColor color);



/*
 * Function:    menu_window_set_wrap_around
 * ----------------------------------------
 * turns the Wrap Around setting on or off: with it on, a step past either end
 * of the list lands on the other end, and with it off the ends stop the cursor
 * the way a list normally does.
 *
 *  menu_window: a pointer to the window whose cursor should wrap
 *  wrap_around: whether the ends of the list wrap onto each other
 */

void menu_window_set_wrap_around(MenuWindow *menu_window, bool wrap_around);
