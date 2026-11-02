#ifndef CCG_ALARM_H
#define CCG_ALARM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ALARM_WHO_SIZE 32
#define ALARM_MAX_EVENTS 10

enum alarm_action {
    ALARM_LOCAL = 1u, ALARM_CONTACT1 = 2u, ALARM_CONTACT2 = 4u,
    ALARM_SILENCE = 8u
};
enum alarm_event_type {
    ALARM_EXCURSION = 1, ALARM_TEMPERATURE, ALARM_SENSOR_FAILURE,
    ALARM_SENSOR_RECOVERY, ALARM_ESCALATION, ALARM_ACK,
    ALARM_REARM, ALARM_RECOVERY, AUDIT_GAP
};

typedef struct {
    int32_t low_mc, high_mc, hysteresis_mc;
    uint32_t grace_s, sensor_s, esc_s, re_alarm_s;
} alarm_config;

typedef struct {
    uint64_t t_ms, duration_ms;
    enum alarm_event_type type;
    int32_t temperature_mc, min_mc, max_mc;
    unsigned level;
    bool temperature_valid;
    char who[ALARM_WHO_SIZE];
} alarm_event;

typedef struct {
    unsigned actions;
    size_t count;
    alarm_event events[ALARM_MAX_EVENTS];
} alarm_result;

/* Caller-owned storage; no allocation or I/O in the alarm path. */
typedef struct {
    alarm_config config;
    uint64_t now_ms, last_valid_ms, excursion_ms, outside_ms, ladder_ms, ack_ms;
    uint64_t total_out_ms;
    int32_t temperature_mc, min_mc, max_mc;
    unsigned level;
    bool temperature_valid, excursion, outside, temperature_alarm, sensor_alarm;
    bool active, acknowledged;
} alarm_state;

alarm_config alarm_defaults(void);
/* Invalid config/time returns false without changing state. */
bool alarm_init(alarm_state *a, const alarm_config *config, uint64_t t_ms);
/* Nondecreasing timestamps required, including ack. Call periodically even
 * during sensor loss, with valid=false. Deadlines are emitted on the first
 * call at/after expiry (not backdated); a late call emits all due levels.
 * Any valid in-range reading resets the grace clock. Missing readings do
 * not establish temperature recovery; sensor timeout is independent.
 * Excursions end inside the inclusive hysteresis band. Duration includes
 * the hysteresis margin and missing readings while an excursion is open; extrema use
 * valid samples only. total_out_ms spans all such excursions since init.
 * Sensor timeout runs from init or the last valid sample. */
bool alarm_step(alarm_state *a, uint64_t t_ms, int32_t temperature_mc,
                bool valid, alarm_result *result);
/* Ack suppresses the shared ladder, not either fault. Re-arming starts a
 * fresh ladder. who must fit, including NUL; inactive/duplicate ack fails. */
bool alarm_ack(alarm_state *a, uint64_t t_ms, const char *who, alarm_result *result);

#endif
