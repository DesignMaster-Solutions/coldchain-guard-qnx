#include "alarm.h"

#include <assert.h>
#include <string.h>

static uint64_t ms(uint32_t seconds)
{
    return (uint64_t)seconds * 1000;
}

alarm_config alarm_defaults(void)
{
    return (alarm_config){2000, 8000, 300, 900, 120, 600, 3600};
}

bool alarm_init(alarm_state *a, const alarm_config *c, uint64_t t_ms)
{
    if (!a || !c || c->low_mc >= c->high_mc || c->hysteresis_mc < 0 ||
        (int64_t)c->hysteresis_mc * 2 > (int64_t)c->high_mc - c->low_mc ||
        !c->sensor_s || !c->esc_s || !c->re_alarm_s) return false;
    *a = (alarm_state){ .config = *c, .now_ms = t_ms, .last_valid_ms = t_ms };
    return true;
}

static void advance(alarm_state *a, uint64_t t_ms)
{
    /* Disjoint intervals within a non-wrapping uint64 clock cannot overflow. */
    if (a->excursion) a->total_out_ms += t_ms - a->now_ms;
    a->now_ms = t_ms;
}

static alarm_event *emit(const alarm_state *a, alarm_result *r, enum alarm_event_type type)
{
    /* At most 7 events per step: two transitions, two fault onsets,
     * one ladder start, and two escalation levels. No allocation or dropping. */
    assert(r->count < ALARM_MAX_EVENTS);
    alarm_event *e = &r->events[r->count++];
    *e = (alarm_event){
        .t_ms = a->now_ms, .type = type, .temperature_mc = a->temperature_mc,
        .temperature_valid = a->temperature_valid, .level = a->level,
        .duration_ms = a->excursion ? a->now_ms - a->excursion_ms : 0,
        .min_mc = a->excursion ? a->min_mc : 0,
        .max_mc = a->excursion ? a->max_mc : 0
    };
    return e;
}

bool alarm_step(alarm_state *a, uint64_t t_ms, int32_t temp, bool valid, alarm_result *r)
{
    if (!a || !r) return false;
    *r = (alarm_result){0};
    if (t_ms < a->now_ms) return false;
    advance(a, t_ms);
    a->temperature_valid = valid;
    if (valid) {
        a->temperature_mc = temp;
        a->last_valid_ms = t_ms;
        if (a->sensor_alarm) {
            a->sensor_alarm = false;
            emit(a, r, ALARM_SENSOR_RECOVERY);
        }
        bool outside = temp < a->config.low_mc || temp > a->config.high_mc;
        if (outside && !a->outside) a->outside_ms = t_ms;
        a->outside = outside;
        bool recovered = (int64_t)temp >= (int64_t)a->config.low_mc + a->config.hysteresis_mc &&
                         (int64_t)temp <= (int64_t)a->config.high_mc - a->config.hysteresis_mc;
        if (!a->excursion && outside) {
            a->excursion = true;
            a->excursion_ms = t_ms;
            a->min_mc = a->max_mc = temp;
            emit(a, r, ALARM_EXCURSION);
        } else if (a->excursion && recovered) {
            /* Recovery reading terminates, but is not part of the excursion. */
            emit(a, r, ALARM_RECOVERY);
            a->excursion = a->temperature_alarm = false;
        } else if (a->excursion) {
            if (temp < a->min_mc) a->min_mc = temp;
            if (temp > a->max_mc) a->max_mc = temp;
        }
    }
    if (!valid && !a->sensor_alarm && t_ms - a->last_valid_ms >= ms(a->config.sensor_s)) {
        a->sensor_alarm = true;
        emit(a, r, ALARM_SENSOR_FAILURE);
    }
    if (a->outside && !a->temperature_alarm && t_ms - a->outside_ms >= ms(a->config.grace_s)) {
        a->temperature_alarm = true;
        emit(a, r, ALARM_TEMPERATURE);
    }
    bool fault = a->temperature_alarm || a->sensor_alarm;
    if (!fault && a->active) {
        a->active = a->acknowledged = false;
        a->level = 0;
        r->actions |= ALARM_SILENCE;
    } else if (fault) {
        if (!a->active || (a->acknowledged && t_ms - a->ack_ms >= ms(a->config.re_alarm_s))) {
            bool rearm = a->active;
            a->active = true;
            a->acknowledged = false;
            a->ladder_ms = t_ms;
            a->level = 1;
            r->actions |= ALARM_LOCAL;
            emit(a, r, rearm ? ALARM_REARM : ALARM_ESCALATION);
        }
        if (!a->acknowledged) {
            while (a->level < 3 && t_ms - a->ladder_ms >= ms(a->config.esc_s) * a->level) {
                ++a->level;
                r->actions |= a->level == 2 ? ALARM_CONTACT1 : ALARM_CONTACT2;
                emit(a, r, ALARM_ESCALATION);
            }
        }
    }
    return true;
}

bool alarm_ack(alarm_state *a, uint64_t t_ms, const char *who, alarm_result *r)
{
    if (!a || !r) return false;
    *r = (alarm_result){0};
    if (!who || !who[0] || t_ms < a->now_ms || !a->active || a->acknowledged) return false;
    size_t n = 0;
    while (n < ALARM_WHO_SIZE && who[n]) ++n;
    if (n == ALARM_WHO_SIZE) return false;
    advance(a, t_ms);
    a->acknowledged = true;
    a->ack_ms = t_ms;
    r->actions = ALARM_SILENCE;
    alarm_event *e = emit(a, r, ALARM_ACK);
    memcpy(e->who, who, n + 1);
    return true;
}
