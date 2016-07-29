#include <pebble.h>

const char START_ACTIVITY_STRING[] = "Press SELECT\n to START activity";
const char END_ACTIVITY_STRING[] = "Press SELECT\n to END activity";

typedef enum ActivityState {
  STATE_NOT_STARTED,
  STATE_IN_PROGRESS,
  STATE_FINISHED
} ActivityState;

static Window *s_window;

static TextLayer *s_status_text_layer;
static TextLayer *s_time_text_layer;
static TextLayer *s_bpm_text_layer;

static TextLayer *s_title_text_layer;
static TextLayer *s_total_time_text_layer;
static TextLayer *s_min_bpm_text_layer;
static TextLayer *s_max_bpm_text_layer;
static TextLayer *s_avg_bpm_text_layer;

static ActivityState s_app_state = STATE_NOT_STARTED;

static time_t s_start_time, s_end_time;

static uint16_t s_curr_hr = 0;
static uint16_t s_min_hr = 65535; // max uint16_t
static uint16_t s_max_hr = 0;

static uint32_t s_hr_samples = 0;
static uint32_t s_hr_total = 0;

int16_t get_min_hr() {
  return s_min_hr;
}

int16_t get_max_hr() {
  return s_max_hr;
}

int16_t get_avg_hr() {
  if (s_hr_samples == 0) return 0;
  return s_hr_total / s_hr_samples;
}

static void prv_on_health_data(HealthEventType type, void *context) {
  // If the update was from the Heart Rate Monitor, update it
  if (type == HealthEventHeartRateUpdate) {
    s_curr_hr = (int16_t) health_service_peek_current_value(HealthMetricHeartRateBPM);

    // Update our metrics
    if (s_curr_hr < s_min_hr) s_min_hr = s_curr_hr;
    if (s_curr_hr > s_max_hr) s_max_hr = s_curr_hr;
    s_hr_samples++;
    s_hr_total += s_curr_hr;
  }
}

static void prv_on_activity_tick(struct tm *tick_time, TimeUnits units_changed) {
  // Update Time
  time_t diff = time(NULL) - s_start_time;
  struct tm *diff_time = gmtime(&diff);

  static char s_time_buffer[9];
  if (diff > SECONDS_PER_HOUR) {
    strftime(s_time_buffer, sizeof(s_time_buffer), "%H:%M:%S", diff_time);
  } else {
    strftime(s_time_buffer, sizeof(s_time_buffer), "%M:%S", diff_time);
  }
  text_layer_set_text(s_time_text_layer, s_time_buffer);

  // Update BPM
  static char s_hrm_buffer[8];
  snprintf(s_hrm_buffer, sizeof(s_hrm_buffer), "%lu BPM", (uint32_t) s_curr_hr);
  text_layer_set_text(s_bpm_text_layer, s_hrm_buffer);
}

static void prv_start_activity(void) {
  // Update application state
  s_app_state = STATE_IN_PROGRESS;
  s_start_time = time(NULL);

  // Set min heart rate sampling period (i.e. fastest sampling rate)
  #if PBL_API_EXISTS(health_service_set_heart_rate_sample_period)
  health_service_set_heart_rate_sample_period(1);
  #endif

  // Subscribe to tick handler to update display
  tick_timer_service_subscribe(SECOND_UNIT, prv_on_activity_tick);

  // Subscribe to health handler
  health_service_events_subscribe(prv_on_health_data, NULL);

  // Update UI
  text_layer_set_text(s_status_text_layer, END_ACTIVITY_STRING);
  layer_set_hidden(text_layer_get_layer(s_time_text_layer), false);
  layer_set_hidden(text_layer_get_layer(s_bpm_text_layer), false);
}

static void prv_end_activity(void) {
  // Update application state
  s_app_state = STATE_FINISHED;
  s_end_time = time(NULL);

  // Set default heart rate sampling period
  #if PBL_API_EXISTS(health_service_set_heart_rate_sample_period)
  health_service_set_heart_rate_sample_period(0);
  #endif

  // Unsubscribe from tick handler
  tick_timer_service_unsubscribe();

  // Unsubscribe from health handler
  health_service_events_unsubscribe();

  // Update UI
  layer_set_hidden(text_layer_get_layer(s_status_text_layer), true);
  layer_set_hidden(text_layer_get_layer(s_time_text_layer), true);
  layer_set_hidden(text_layer_get_layer(s_bpm_text_layer), true);

  static char s_time_buffer[16];
  time_t diff = s_end_time - s_start_time;
  struct tm *diff_time = gmtime(&diff);

  if (diff > SECONDS_PER_HOUR) {
    strftime(s_time_buffer, sizeof(s_time_buffer), "Time: %H:%M:%S", diff_time);
  } else {
    strftime(s_time_buffer, sizeof(s_time_buffer), "Time: %M:%S", diff_time);
  }
  text_layer_set_text(s_total_time_text_layer, s_time_buffer);

  static char s_avg_bpm_buffer[16];
  snprintf(s_avg_bpm_buffer, sizeof(s_avg_bpm_buffer), "Avg: %u BPM", get_avg_hr());
  text_layer_set_text(s_avg_bpm_text_layer, s_avg_bpm_buffer);

  static char s_max_bpm_buffer[16];
  snprintf(s_max_bpm_buffer, sizeof(s_max_bpm_buffer), "Max: %u BPM", get_max_hr());
  text_layer_set_text(s_max_bpm_text_layer, s_max_bpm_buffer);

  static char s_min_bpm_buffer[16];
  snprintf(s_min_bpm_buffer, sizeof(s_min_bpm_buffer), "Min: %u BPM", get_min_hr());
  text_layer_set_text(s_min_bpm_text_layer, s_min_bpm_buffer);

  layer_set_hidden(text_layer_get_layer(s_title_text_layer), false);
  layer_set_hidden(text_layer_get_layer(s_total_time_text_layer), false);
  layer_set_hidden(text_layer_get_layer(s_avg_bpm_text_layer), false);
  layer_set_hidden(text_layer_get_layer(s_min_bpm_text_layer), false);
  layer_set_hidden(text_layer_get_layer(s_max_bpm_text_layer), false);

}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  switch (s_app_state) {
    case STATE_NOT_STARTED:
      // Display activity
      prv_start_activity();
      break;
    case STATE_IN_PROGRESS:
      // Display Metrics
      prv_end_activity();
      break;
    default:
      // Quit
      window_stack_pop(true);
      break;
  }
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Determin how we launched (automaticaly start activity on quick launch)
  bool quick_launch = launch_reason() == APP_LAUNCH_QUICK_LAUNCH;

  // Status
  s_status_text_layer = text_layer_create(GRect(0, 67, bounds.size.w, 40));
  text_layer_set_text(s_status_text_layer, quick_launch ? END_ACTIVITY_STRING
                                                        : START_ACTIVITY_STRING);
  text_layer_set_text_alignment(s_status_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_status_text_layer), false);
  layer_add_child(window_layer, text_layer_get_layer(s_status_text_layer));

  // Time in activity (Hidden)
  s_time_text_layer = text_layer_create(GRect(0, 27, bounds.size.w, 20));
  text_layer_set_text(s_time_text_layer, "00:00");
  text_layer_set_text_alignment(s_time_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_time_text_layer), !quick_launch);
  layer_add_child(window_layer, text_layer_get_layer(s_time_text_layer));

  // Curr BPM (hidden)
  s_bpm_text_layer = text_layer_create(GRect(0, 117, bounds.size.w, 20));
  text_layer_set_text(s_bpm_text_layer, "??? BPM");
  text_layer_set_text_alignment(s_bpm_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_bpm_text_layer), !quick_launch);
  layer_add_child(window_layer, text_layer_get_layer(s_bpm_text_layer));


  // "Workout Summay" (Hidden)
  s_title_text_layer = text_layer_create(GRect(0, 10, bounds.size.w, 20));
  text_layer_set_text(s_title_text_layer, "Workout Summary");
  text_layer_set_text_alignment(s_title_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_title_text_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_title_text_layer));

  // Total Time (Hidden)
  s_total_time_text_layer = text_layer_create(GRect(0, 35, bounds.size.w, 20));
  text_layer_set_text_alignment(s_total_time_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_total_time_text_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_total_time_text_layer));

  // Avg BPM (Hidden)
  s_avg_bpm_text_layer = text_layer_create(GRect(0, 85, bounds.size.w, 20));
  text_layer_set_text_alignment(s_avg_bpm_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_avg_bpm_text_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_avg_bpm_text_layer));

  // Max BPM (Hidden)
  s_max_bpm_text_layer = text_layer_create(GRect(0, 110, bounds.size.w, 20));
  text_layer_set_text_alignment(s_max_bpm_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_max_bpm_text_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_max_bpm_text_layer));

  // Min BPM (Hidden)
  s_min_bpm_text_layer = text_layer_create(GRect(0, 135, bounds.size.w, 20));
  text_layer_set_text_alignment(s_min_bpm_text_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_min_bpm_text_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_min_bpm_text_layer));

  if(quick_launch) prv_start_activity();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_status_text_layer);
  text_layer_destroy(s_time_text_layer);
  text_layer_destroy(s_bpm_text_layer);

  text_layer_destroy(s_title_text_layer);
  text_layer_destroy(s_avg_bpm_text_layer);
  text_layer_destroy(s_max_bpm_text_layer);
  text_layer_destroy(s_min_bpm_text_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });

  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  window_destroy(s_window);

  // Reset Heart Rate Sampling
  #if PBL_API_EXISTS(health_service_set_heart_rate_sample_period)
  health_service_set_heart_rate_sample_period(0);
  #endif

}

int main(void) {
  prv_init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", s_window);

  app_event_loop();
  prv_deinit();
}
