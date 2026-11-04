#pragma once
// The wall clock for the top strip. Time comes from Signal K navigation.datetime; between
// updates it advances on the monotonic timer. Pure: no LVGL, no I/O.
#include <stdbool.h>
#include <stdint.h>

// "YYYY-MM-DDTHH:MM:SS[.fff]" followed by "Z" or "+hh:mm" / "-hh:mm". False on anything else
// (including a time with no zone), leaving *epoch_s untouched.
bool ui_clock_parse_iso(const char *iso, int64_t *epoch_s);

// Records a good UTC time (seconds since 1970) and the monotonic time it arrived.
void ui_clock_feed(int64_t epoch_s, int64_t now_ms);

// Writes local "HH:MM" (UTC + offset_min). False, writing nothing, when no time was ever fed, or
// the last feed is older than 10 minutes while the source is down.
bool ui_clock_format(int64_t now_ms, int offset_min, bool source_up, char out[6]);

void ui_clock_reset(void);
