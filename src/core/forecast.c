#include "forecast.h"

void forecast_reset(forecast *f)
{
    if (f) f->count = f->next = 0;
}

void forecast_init(forecast *f, uint64_t spacing_ms)
{
    if (!f) return;
    f->spacing_ms = spacing_ms;
    forecast_reset(f);
}

static size_t newest_index(const forecast *f)
{
    return (f->next + FORECAST_WINDOW - 1) % FORECAST_WINDOW;
}

bool forecast_add(forecast *f, uint64_t t_ms, int32_t temp_mc)
{
    if (!f) return false;
    if (f->count) {
        uint64_t newest = f->t_ms[newest_index(f)];
        if (t_ms <= newest) return false;
        if (t_ms - newest < f->spacing_ms) return true;
    }
    f->t_ms[f->next] = t_ms;
    f->mc[f->next] = temp_mc;
    f->next = (f->next + 1) % FORECAST_WINDOW;
    if (f->count < FORECAST_WINDOW) ++f->count;
    return true;
}

bool forecast_eval(const forecast *f, int32_t low_mc, int32_t high_mc,
                   uint64_t horizon_ms, forecast_result *out)
{
    if (!f || !out || low_mc >= high_mc) return false;
    *out = (forecast_result){FORECAST_UNKNOWN, 0, 0};
    if (f->count < FORECAST_MIN_POINTS) return true;
    size_t newest = newest_index(f);
    if (f->mc[newest] > high_mc || f->mc[newest] < low_mc) {
        out->trend = FORECAST_OUTSIDE;
        return true;
    }
    /* Time relative to the newest reading keeps magnitudes small and puts the
     * fitted intercept at "now". Kept timestamps strictly increase, so sxx > 0. */
    double mx = 0, my = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < f->count; ++i) {
        mx -= (double)(f->t_ms[newest] - f->t_ms[i]);
        my += (double)f->mc[i];
    }
    mx /= (double)f->count;
    my /= (double)f->count;
    for (size_t i = 0; i < f->count; ++i) {
        double dx = -(double)(f->t_ms[newest] - f->t_ms[i]) - mx;
        sxx += dx * dx;
        sxy += dx * ((double)f->mc[i] - my);
    }
    double slope = sxy / sxx;          /* milli-Celsius per millisecond */
    double now = my - slope * mx;      /* fitted value at the newest reading */
    double per_min = slope * 60000.0;  /* |per_min| < 2^63: int32 span over >= 1 ms */
    out->slope_mc_per_min = (int64_t)(per_min < 0 ? per_min - 0.5 : per_min + 0.5);
    out->trend = FORECAST_STEADY;
    if (slope == 0) return true;
    double eta = ((double)(slope > 0 ? high_mc : low_mc) - now) / slope;
    if (eta < 0) eta = 0;  /* fitted line already past the limit; newest reading still inside */
    if (eta > (double)horizon_ms || eta >= 18446744073709551616.0) return true;
    out->trend = slope > 0 ? FORECAST_TO_HIGH : FORECAST_TO_LOW;
    out->eta_ms = (uint64_t)(eta + 0.5);
    return true;
}
