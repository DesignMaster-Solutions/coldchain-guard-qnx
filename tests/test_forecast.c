#include "forecast.h"

#include <assert.h>
#include <stdio.h>

#define MINUTE UINT64_C(60000)
#define LOW 2000
#define HIGH 8000

static forecast_result assess(const forecast *f, uint64_t horizon_ms)
{
    forecast_result r;
    assert(forecast_eval(f, LOW, HIGH, horizon_ms, &r));
    return r;
}

static void test_contract(void)
{
    forecast f;
    forecast_result r;
    forecast_init(&f, MINUTE);
    assert(!forecast_eval(NULL, LOW, HIGH, MINUTE, &r));
    assert(!forecast_eval(&f, HIGH, LOW, MINUTE, &r));
    assert(!forecast_eval(&f, LOW, HIGH, MINUTE, NULL));
    assert(!forecast_add(NULL, 0, 5000));
    for (unsigned i = 0; i + 1 < FORECAST_MIN_POINTS; ++i) assert(forecast_add(&f, i * MINUTE, 5000));
    assert(assess(&f, 60 * MINUTE).trend == FORECAST_UNKNOWN);
    size_t kept = f.count;
    assert(!forecast_add(&f, (kept - 1) * MINUTE, 6000));        /* same time as the newest */
    assert(!forecast_add(&f, (kept - 2) * MINUTE, 6000));        /* time going back */
    assert(forecast_add(&f, (kept - 1) * MINUTE + 1000, 9000));  /* inside spacing: skipped */
    assert(f.count == kept);
    assert(forecast_add(&f, kept * MINUTE, 5000));
    r = assess(&f, 60 * MINUTE);
    assert(r.trend == FORECAST_STEADY && r.slope_mc_per_min == 0 && r.eta_ms == 0);
    forecast_reset(&f);
    assert(assess(&f, 60 * MINUTE).trend == FORECAST_UNKNOWN);
}

static void test_linear_rise(void)
{
    forecast f;
    forecast_init(&f, MINUTE);
    for (unsigned i = 0; i < 10; ++i) assert(forecast_add(&f, i * MINUTE, 5000 + 100 * (int32_t)i));
    /* newest 5900 at +100 mc/min: the upper limit is 21 minutes away */
    forecast_result r = assess(&f, 60 * MINUTE);
    assert(r.trend == FORECAST_TO_HIGH && r.slope_mc_per_min == 100 && r.eta_ms == 21 * MINUTE);
    assert(assess(&f, 20 * MINUTE).trend == FORECAST_STEADY); /* beyond a shorter horizon */
}

static void test_linear_fall(void)
{
    forecast f;
    forecast_init(&f, MINUTE);
    for (unsigned i = 0; i < 8; ++i) assert(forecast_add(&f, i * MINUTE, 5000 - 150 * (int32_t)i));
    /* newest 3950 at -150 mc/min: the lower limit is 13 minutes away */
    forecast_result r = assess(&f, 60 * MINUTE);
    assert(r.trend == FORECAST_TO_LOW && r.slope_mc_per_min == -150 && r.eta_ms == 13 * MINUTE);
}

static void test_noise_is_steady(void)
{
    forecast f;
    forecast_init(&f, MINUTE);
    for (unsigned i = 0; i < FORECAST_WINDOW; ++i)
        assert(forecast_add(&f, i * MINUTE, 5000 + (i % 2 ? 40 : -40)));
    assert(assess(&f, 60 * MINUTE).trend == FORECAST_STEADY);
}

static void test_outside(void)
{
    forecast f;
    forecast_init(&f, MINUTE);
    for (unsigned i = 0; i < FORECAST_MIN_POINTS; ++i)
        assert(forecast_add(&f, i * MINUTE, 7000 + 300 * (int32_t)i)); /* newest 8500 */
    forecast_result r = assess(&f, 60 * MINUTE);
    assert(r.trend == FORECAST_OUTSIDE && r.eta_ms == 0);
}

static void test_window_keeps_newest(void)
{
    forecast f;
    forecast_init(&f, MINUTE);
    for (unsigned i = 0; i < FORECAST_WINDOW; ++i) assert(forecast_add(&f, i * MINUTE, 5000));
    for (unsigned i = 0; i < FORECAST_WINDOW; ++i)
        assert(forecast_add(&f, (FORECAST_WINDOW + i) * MINUTE, 3000 + 200 * (int32_t)i));
    /* only the ramp is left: newest 6000 at +200 mc/min, 10 minutes to the limit */
    forecast_result r = assess(&f, 60 * MINUTE);
    assert(f.count == FORECAST_WINDOW);
    assert(r.trend == FORECAST_TO_HIGH && r.slope_mc_per_min == 200 && r.eta_ms == 10 * MINUTE);
}

/* A failed compressor: the cabinet warms toward a 25 C room with a two-hour
 * time constant (one-minute Euler steps, no libm). The pre-alarm must come at
 * least 10 minutes before the first reading above the limit, and must not
 * promise more time than is left (a straight line on a slowing curve is early). */
static void test_lead_time_on_warming_curve(void)
{
    forecast f;
    forecast_init(&f, MINUTE);
    int32_t temp = 5000;
    bool warned = false;
    uint64_t warned_at = 0, crossed_at = 0, promised_ms = 0;
    for (uint64_t m = 0; m < 120; ++m) {
        if (temp > HIGH) { crossed_at = m; break; }
        assert(forecast_add(&f, m * MINUTE, temp));
        forecast_result r = assess(&f, 60 * MINUTE);
        if (!warned && r.trend == FORECAST_TO_HIGH) {
            warned = true; warned_at = m; promised_ms = r.eta_ms;
        }
        temp += (25000 - temp) / 120;
    }
    assert(warned && crossed_at > warned_at && crossed_at - warned_at >= 10);
    assert(promised_ms <= (crossed_at - warned_at) * MINUTE);
}

int main(void)
{
    test_contract();
    test_linear_rise();
    test_linear_fall();
    test_noise_is_steady();
    test_outside();
    test_window_keeps_newest();
    test_lead_time_on_warming_curve();
    puts("PASS forecast: contract, rise/fall ETA, noise, outside, window, warming-curve lead time");
    return 0;
}
