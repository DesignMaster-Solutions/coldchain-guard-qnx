#ifndef CCG_PORTABLE_H
#define CCG_PORTABLE_H
#include "alarm.h"
#include <stdio.h>
#define FRIDGE_TEXT_SIZE 320
#define SCENARIO_CAPACITY 4096

typedef struct { uint64_t t_ms; int32_t temp_mc; bool valid; } scenario_row;
/* Strict CSV, optional exact header, first timestamp zero, strictly increasing.
 * Empty temperature means unavailable. Final row is held indefinitely. */
bool scenario_load(FILE *f, scenario_row *rows, size_t capacity, size_t *count);
scenario_row scenario_sample(const scenario_row *rows, size_t count, uint64_t t_ms);
/* One complete command per write, all three keys required, arbitrary order.
 * Invalid input leaves config unchanged; preserves all non-limit settings. */
bool fridge_command(const char *text, size_t length, alarm_config *config);
/* Returns length excluding NUL, or -1 on insufficient storage. */
int fridge_format(char *out, size_t capacity, const alarm *state);
#endif
