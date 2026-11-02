#include "portable.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool number(const char *s)
{
    if (*s == '-') ++s;
    if (*s < '0' || *s > '9') return false;
    return true;
}

static bool integer(const char *s, char **end, int32_t *out)
{
    if (!number(s)) return false;
    errno = 0;
    intmax_t n = strtoimax(s, end, 10);
    if (errno || n < INT32_MIN || n > INT32_MAX) return false;
    *out = (int32_t)n;
    return true;
}

bool fridge_command(const char *text, size_t length, alarm_config *config)
{
    if (!text || !config || !length || length >= 128 || memchr(text, 0, length)) return false;
    char buf[128]; memcpy(buf, text, length); buf[length] = 0;
    char *p = buf;
    alarm_config next = *config;
    unsigned seen = 0;
    while (*p) {
        while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') ++p;
        if (!*p) break;
        unsigned bit; int32_t *value;
        if (!strncmp(p, "low=", 4)) { bit = 1; value = &next.low_mc; p += 4; }
        else if (!strncmp(p, "high=", 5)) { bit = 2; value = &next.high_mc; p += 5; }
        else if (!strncmp(p, "hyst=", 5)) { bit = 4; value = &next.hysteresis_mc; p += 5; }
        else return false;
        if ((seen & bit) || !integer(p, &p, value)) return false;
        seen |= bit;
        if (*p && *p != ' ' && *p != '\n' && *p != '\t' && *p != '\r') return false;
    }
    alarm_state checked;
    if (seen != 7 || !alarm_init(&checked, &next, 0)) return false;
    *config = next;
    return true;
}

bool scenario_load(FILE *f, scenario_row *rows, size_t capacity, size_t *count)
{
    if (!f || !rows || !count) return false;
    char line[128]; size_t n = 0, lines = 0;
    while (fgets(line, sizeof(line), f)) {
        ++lines;
        size_t len = strlen(line);
        if (len == sizeof(line) - 1) return false;
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (lines == 1 && !strcmp(line, "t_ms,temp_mc")) continue;
        if (n == capacity || line[0] < '0' || line[0] > '9') return false;
        errno = 0; char *end;
        uintmax_t t = strtoumax(line, &end, 10);
        if (errno || t > UINT64_MAX || *end != ',' || (!n && t != 0) || (n && t <= rows[n-1].t_ms)) return false;
        scenario_row row = { .t_ms = (uint64_t)t };
        char *temp = end + 1;
        if (*temp) {
            if (!integer(temp, &end, &row.temp_mc) || *end) return false;
            row.valid = true;
        }
        rows[n++] = row;
    }
    if (ferror(f) || !n) return false;
    *count = n;
    return true;
}

scenario_row scenario_sample(const scenario_row *rows, size_t count, uint64_t t_ms)
{
    scenario_row row = {0};
    for (size_t i = 0; i < count && rows[i].t_ms <= t_ms; ++i) row = rows[i];
    return row;
}

int fridge_format(char *out, size_t capacity, const alarm_state *a)
{
    int n = snprintf(out, capacity,
        "temp_mc=%" PRId32 " valid=%u alarm=%u level=%u low=%" PRId32
        " high=%" PRId32 " hyst=%" PRId32 " sample_ms=%" PRIu64 " last_valid_ms=%" PRIu64 "\n",
        a->temperature_mc, (unsigned)a->temperature_valid, (unsigned)a->active, a->level,
        a->config.low_mc, a->config.high_mc, a->config.hysteresis_mc, a->now_ms, a->last_valid_ms);
    return n < 0 || (size_t)n >= capacity ? -1 : n;
}

/* Bounds are application policy, independent of the host scheduler. */
bool fridge_uint(const char *text, unsigned minimum, unsigned maximum, unsigned *out)
{
    if (!text || !*text || !out || minimum > maximum) return false;
    unsigned value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        unsigned digit = (unsigned)(*p - '0');
        if (digit > maximum || value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (value < minimum) return false;
    *out = value;
    return true;
}
