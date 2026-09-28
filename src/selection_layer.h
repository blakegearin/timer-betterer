/*
 * somewhat modified implementation of the firmware's SelectionLayer
 * the changes include adding B&W support and the ability to move the
 * selector field backward and forward.
 */

#pragma once

#include "pebble.h"
#include "touch.h"

#define MAX_SELECTION_LAYER_CELLS 3

typedef char* (*SelectionLayerGetCellText)(unsigned index, void *callback_context);

typedef void (*SelectionLayerCompleteCallback)(void *callback_context);

typedef void (*SelectionLayerIncrementCallback)(unsigned selected_cell_idx,
                                                uint8_t reapeating_count, void *callback_context);

typedef void (*SelectionLayerDecrementCallback)(unsigned selected_cell_idx,
                                                uint8_t reapeating_count, void *callback_context);

typedef struct SelectionLayerCallbacks {
  SelectionLayerGetCellText get_cell_text;
  SelectionLayerCompleteCallback complete;
  SelectionLayerIncrementCallback increment;
  SelectionLayerDecrementCallback decrement;
} SelectionLayerCallbacks;


typedef struct SelectionLayerData {
  unsigned num_cells;
  unsigned cell_widths[MAX_SELECTION_LAYER_CELLS];
  unsigned cell_padding;
  unsigned selected_cell_idx;

  // If is_active = false the the selected cell will become invalid, and any clicks will be ignored
  bool is_active;

  GFont font;
  GColor inactive_background_color;
  GColor active_background_color;

  SelectionLayerCallbacks callbacks;
  void *callback_context;

  // Animation stuff
  Animation *value_change_animation;
  bool bump_is_upwards;
  unsigned bump_text_anim_progress;
  AnimationImplementation bump_text_impl;
  unsigned bump_settle_anim_progress;
  AnimationImplementation bump_settle_anim_impl;

  Animation *next_cell_animation;
  bool slide_is_forward;
  unsigned slide_amin_progress;
  AnimationImplementation slide_amin_impl;
  unsigned slide_settle_anim_progress;
  AnimationImplementation slide_settle_anim_impl;

#if TOUCH_INPUT
  // leftover pixels of the touch pan not yet traded for a value step
  int16_t touch_pan_accum;
#endif
} SelectionLayerData;



Layer* selection_layer_init(SelectionLayerData *selection_layer_data, GRect frame,
                            unsigned num_cells);

Layer* selection_layer_create(GRect frame, unsigned num_cells);

void selection_layer_deinit(Layer* layer);

void selection_layer_destroy(Layer* layer);

void selection_layer_set_cell_width(Layer *layer, unsigned cell_idx, unsigned width);

void selection_layer_set_font(Layer *layer, GFont font);

void selection_layer_set_inactive_bg_color(Layer *layer, GColor color);

void selection_layer_set_active_bg_color(Layer *layer, GColor color);

void selection_layer_set_cell_padding(Layer *layer, unsigned padding);

// When transitioning from inactive -> active, the selected cell will be index 0
void selection_layer_set_active(Layer *layer, bool is_active);

void selection_layer_set_click_config_onto_window(Layer *layer, struct Window *window);

void selection_layer_set_callbacks(Layer *layer, void *callback_context,
                                   SelectionLayerCallbacks callbacks);

#if TOUCH_INPUT
// Touch entry points, for a window feeding its own recognizers into the layer.
// selection_layer_touch_panned takes the vertical movement of a finger since
// the last call, screen orientation (negative is a finger moving up, which
// increments); whole 8 px blocks step the active cell, leftovers accumulate.
// selection_layer_touch_tapped takes a screen-space tap: tapping a cell
// activates it, tapping the active cell acts as the select button. Taps
// outside the layer's bounds are ignored.
void selection_layer_touch_panned(Layer *layer, int16_t dy);
// drops the leftover of a pan gesture that has ended, so it is not owed to
// the next one
void selection_layer_touch_pan_ended(Layer *layer);
void selection_layer_touch_tapped(Layer *layer, GPoint tap_point);
// steps back through the fields and out of the window, the back button's job
void selection_layer_touch_back(Layer *layer);
#endif  // TOUCH_INPUT
