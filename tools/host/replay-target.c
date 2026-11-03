/* Deterministic scenario replay ON the target, one sample per CSV row.
 * Uses scenario timestamps, not wall time: this is not the live timer adapter. */
#include "portable.h"
#include "storage.h"
#include "sha256.h"
#include <inttypes.h>

static scenario_row rows[SCENARIO_CAPACITY];
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *input = fopen(argv[1],"rb"); size_t count;
    if (!input) return 2;
    bool ok = scenario_load(input,rows,SCENARIO_CAPACITY,&count);
    if (fclose(input) || !ok) return 1;
    audit_chain chain;
    FILE *log = fridge_log_open(argv[2],&chain);
    if (!log) return 2;
    /* A replay is a new run, never resume an old scenario's evidence. */
    if (chain.seq) { fclose(log); return 1; }
    alarm_state state; alarm_config config = alarm_defaults();
    if (!alarm_init(&state,&config,0)) return 1;
    forecast window; forecast_init(&window,FRIDGE_FORECAST_SPACING_DEFAULT);
    forecast_result previous = { .trend = FORECAST_UNKNOWN };
    for (size_t i = 0; i < count; ++i) {
        scenario_row row = rows[i]; alarm_result events; audit_record record;
        if (!alarm_step(&state,row.t_ms,row.temp_mc,row.valid,&events)) return 1;
        for (size_t e = 0; e < events.count; ++e)
            if (!audit_append_event(&chain,&record,&events.events[e]) || !audit_write(log,&record)) return 1;
        if (row.valid) { if (!forecast_add(&window,row.t_ms,row.temp_mc)) return 1; }
        else forecast_reset(&window);
        forecast_result next;
        if (!forecast_eval(&window,config.low_mc,config.high_mc,
                           (uint64_t)FRIDGE_FORECAST_HORIZON_DEFAULT*1000,&next)) return 1;
        if (fridge_forecast_transition(previous.trend,next.trend)) {
            char detail[AUDIT_DETAIL_SIZE];
            if (fridge_forecast_format(detail,sizeof(detail),&next) < 0 ||
                !audit_append(&chain,&record,row.t_ms,AUDIT_FORECAST,row.valid ? row.temp_mc : 0,detail) ||
                !audit_write(log,&record)) return 1;
        }
        previous = next;
        if (!fridge_log_sample(log,&chain,row.t_ms,row.temp_mc,row.valid)) return 1;
    }
    if (fseek(log,0,SEEK_SET)) return 2;
    sha256_ctx hash; sha256_init(&hash);
    uint8_t block[AUDIT_RECORD_SIZE], digest[32]; size_t n, bytes = 0;
    puts("FGCTL-AUDIT-BEGIN v1");
    while ((n = fread(block,1,sizeof(block),log)) != 0) {
        sha256_update(&hash,block,n); bytes += n;
        fputs("FGCTL-DATA ",stdout);
        for (size_t i = 0; i < n; ++i) printf("%02x",block[i]);
        putchar('\n');
    }
    if (ferror(log) || fclose(log)) return 2;
    sha256_final(&hash,digest);
    printf("FGCTL-AUDIT-END bytes=%zu sha256=",bytes);
    for (size_t i = 0; i < sizeof(digest); ++i) printf("%02x",digest[i]);
    putchar('\n');
    return fflush(stdout) || ferror(stdout) ? 2 : 0;
}
