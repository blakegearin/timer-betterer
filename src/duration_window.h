/*******************************************************************************
 * FILENAME :        duration_window.h
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
 *      void            duration_window_set_snooze_mode(DurationWindow
 *                          *duration_window, int64_t current);
 *      bool            duration_window_get_snooze_mode(DurationWindow
 *                          *duration_window);
 *      void            duration_window_set_highlight_color(DurationWindow
 *                          *duration_window, GColor color);
 *
 * AUTHOR :    Eric Phillips        START DATE :    07/12/15
 *
 */

#pragma once

#include <pebble.h>
#include "countdown_timer.h"



/*******************************************************************************
 * CALLBACK DECLARATIONS
 */

/*
 * Callback:    DurationWindowComplete
 * ----------------------------------
 * called when the user is done setting the time
 */

typedef void (*DurationWindowComplete)(int64_t time_duration, void *context);



/*
 * Structure:   DurationWindowCallbacks
 * ----------------------------------
 * structure containing all DurationWindow callbacks
 */

typedef struct DurationWindowCallbacks {
  DurationWindowComplete duration_complete;
} DurationWindowCallbacks;



/*******************************************************************************
 * STRUCTURE DECLARATION
 */

/*
 * Structure:   DurationWindow
 * -----------------------
 * main structure containing all data for a DurationWindow
 */

typedef struct DurationWindow DurationWindow;



/*******************************************************************************
 * API FUNCTIONS
 */

/*
 * Function:    duration_window_create
 * -------------------------------
 * creates a new DurationWindow in memory but does not push it into view
 *
 *  duration_window_callbacks: callbacks for communication
 *
 *  returns: a pointer to a new DurationWindow structure
 */

DurationWindow *duration_window_create(DurationWindowCallbacks duration_window_callbacks);



/*
 * Function:    duration_window_destroy
 * ----------------------------------
 * destroys an existing DurationWindow
 *
 *  duration_window: a pointer to the DurationWindow being destroyed
 */

void duration_window_destroy(DurationWindow *duration_window);



/*
 * Function:    duration_window_push
 * -------------------------------
 * push the window onto the stack
 *
 *  duration_window: a pointer to the DurationWindow being pushed
 *  animated: whether to animate the push or not
 */

void duration_window_push(DurationWindow *duration_window, bool animated);



/*
 * Function:    duration_window_pop
 * ------------------------------
 * pop the window off the stack
 *
 *  duration_window: a pointer to the DurationWindow to pop
 *  animated: whether to animate the pop or not
 */

void duration_window_pop(DurationWindow *duration_window, bool animated);



/*
 * Function:    duration_window_get_topmost_window
 * ---------------------------------------------
 * gets whether it is the topmost window or not
 *
 *  duration_window: a pointer to the DurationWindow being checked
 *
 *  returns: a boolean indicating if it is the topmost window
 */

bool duration_window_get_topmost_window(DurationWindow *duration_window);



/*
 * Function:    duration_window_set_timer
 * -------------------------------------
 * sets a CountdownTimer to be associated with this DurationWindow
 *
 *  duration_window: a pointer to the DurationWindow being assigned the timer
 *  countdown_timer: the CountdownTimer being assigned the window
 */

void duration_window_set_timer(DurationWindow *duration_window, CountdownTimer *countdown_timer);



/*
 * Function:    duration_window_get_timer
 * -------------------------------------
 * gets the CountdownTimer associated with this DurationWindow
 *
 *  duration_window: a pointer to the DurationWindow being checked
 *
 *  returns: a pointer to the CountdownTimer associated with the DurationWindow
 */

CountdownTimer *duration_window_get_timer(DurationWindow *duration_window);



/*
 * Function:    duration_window_set_snooze_mode
 * --------------------------------------------
 * puts the picker in Snooze mode: it dials a snooze delay for the Snooze
 * Length setting instead of a timer duration, and 00:00:00 is a valid
 * submission meaning "Off".
 *
 *  duration_window: a pointer to the DurationWindow being switched
 *  current: the delay in milliseconds to seed the fields with
 */

void duration_window_set_snooze_mode(DurationWindow *duration_window, int64_t current);



/*
 * Function:    duration_window_get_snooze_mode
 * --------------------------------------------
 * gets whether the picker is dialling a snooze delay
 *
 *  duration_window: a pointer to the DurationWindow being checked
 *
 *  returns: true if the next complete callback carries a snooze delay
 */

bool duration_window_get_snooze_mode(DurationWindow *duration_window);



/*
 * Function:    duration_window_set_highlight_color
 * --------------------------------------------
 * sets the over-all color scheme of the window
 *
 *  color: the GColor to set the highlight to
 */

void duration_window_set_highlight_color(DurationWindow *duration_window, GColor color);
