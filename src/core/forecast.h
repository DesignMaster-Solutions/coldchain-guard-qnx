#ifndef CCG_FORECAST_H
#define CCG_FORECAST_H

/* Predictive pre-alarm: an online least-squares trend over the most recent
 * valid readings, extrapolated to the configured limits, so staff hear
 * "warming, about 13 minutes to the upper limit" before the excursion starts.
 * A forecast is advice only: it never raises, delays or clears an alarm
 * (alarm.c stays the single source of alarms). Caller-owned storage; no
 * allocation, I/O or libm. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FORECAST_WINDOW 16
#define FORECAST_MIN_POINTS 6

enum forecast_trend {
    FORECAST_UNKNOWN,   /* fewer than FORECAST_MIN_POINTS kept readings */
    FORECAST_STEADY,    /* the fitted line meets no limit within the horizon */
    FORECAST_TO_HIGH,   /* rising; eta_ms until the fitted line meets high */
    FORECAST_TO_LOW,    /* falling; eta_ms until the fitted line meets low */
    FORECAST_OUTSIDE    /* newest kept reading is already outside: the alarm path owns it */
};

typedef struct {
    uint64_t spacing_ms;
    uint64_t t_ms[FORECAST_WINDOW];
    int32_t mc[FORECAST_WINDOW];
    size_t count, next;
} forecast;

typedef struct {
    enum forecast_trend trend;
    int64_t slope_mc_per_min;
    uint64_t eta_ms;    /* from the newest kept reading; 0 unless TO_HIGH/TO_LOW */
} forecast_result;

/* Keep at most one reading per spacing_ms, so the window spans minutes rather
 * than a burst of samples (16 x 60 s = a 15-minute trend at a 1 s period). */
void forecast_init(forecast *f, uint64_t spacing_ms);
/* Valid readings only. A timestamp not after the newest kept reading returns
 * false and changes nothing; one closer than spacing_ms is accepted and skipped. */
bool forecast_add(forecast *f, uint64_t t_ms, int32_t temp_mc);
/* Sensor loss or a limit change: start over, so a trend never spans a gap. */
void forecast_reset(forecast *f);
/* Invalid arguments (NULL, low_mc >= high_mc) return false. */
bool forecast_eval(const forecast *f, int32_t low_mc, int32_t high_mc,
                   uint64_t horizon_ms, forecast_result *out);

#endif
