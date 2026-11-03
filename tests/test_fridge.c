#include "portable.h"
#include <assert.h>
#include <string.h>

static bool load(const char *text, scenario_row *rows, size_t cap, size_t *n)
{
    FILE *f = tmpfile(); assert(f);
    assert(fputs(text, f) >= 0); rewind(f);
    bool ok = scenario_load(f, rows, cap, n); assert(fclose(f) == 0); return ok;
}
int main(void)
{
    char who[ALARM_WHO_SIZE] = "unchanged";
    assert(fridge_ack_command("ack who=A",9,who) && !strcmp(who,"A"));
    const char *name = "ack who=Team_1.A-z9\n";
    assert(fridge_ack_command(name,strlen(name),who) && !strcmp(who,"Team_1.A-z9"));
    char maximum[64] = "ack who=";
    memset(maximum+8,'a',ALARM_WHO_SIZE-1); maximum[8+ALARM_WHO_SIZE-1] = 0;
    assert(fridge_ack_command(maximum,strlen(maximum),who));
    maximum[8+ALARM_WHO_SIZE-1] = 'a'; maximum[8+ALARM_WHO_SIZE] = 0;
    const char *bad_ack[] = {"", "ack who=", "ack who=\n", "ack who=a b", "ack who=a\nb",
        "ack who=a\n\n", "ack who=<script>", "ack who=a/b", "ack who=á", "ack who=a\r",
        "ack who=a low=2000", "ACK who=a", " ack who=a", "ack who=a\t", maximum};
    for (size_t i=0; i<sizeof(bad_ack)/sizeof(*bad_ack); ++i) {
        strcpy(who,"unchanged");
        assert(!fridge_ack_command(bad_ack[i],strlen(bad_ack[i]),who));
        assert(!strcmp(who,"unchanged"));
    }
    assert(!fridge_ack_command("ack who=a\0b",11,who));
    assert(!fridge_ack_command(NULL,9,who));
    assert(!fridge_ack_command("ack who=a",9,NULL));
    unsigned priority = FRIDGE_PRIORITY_DEFAULT;
    assert(fridge_uint("1",FRIDGE_PRIORITY_MIN,FRIDGE_PRIORITY_MAX,&priority) && priority == 1);
    assert(fridge_uint("63",FRIDGE_PRIORITY_MIN,FRIDGE_PRIORITY_MAX,&priority) && priority == 63);
    const char *invalid_priority[] = {"0", "64", "-1", "+20", " 20", "20 ", "20x", "", "42949672960"};
    for (size_t i = 0; i < sizeof(invalid_priority)/sizeof(*invalid_priority); ++i) {
        assert(!fridge_uint(invalid_priority[i],FRIDGE_PRIORITY_MIN,FRIDGE_PRIORITY_MAX,&priority));
        assert(priority == 63);
    }
    assert(!fridge_uint(NULL,1,63,&priority));
    assert(!fridge_uint("20",1,63,NULL));
    assert(!fridge_uint("20",63,1,&priority));
    unsigned seconds = 0;
    assert(fridge_uint("86400",1,86400,&seconds) && seconds == 86400);
    assert(!fridge_uint("86401",1,86400,&seconds) && seconds == 86400);
    assert(!fridge_uint("0",1,86400,&seconds));
    alarm_config c = alarm_defaults();
    const char *good = "high=8000 low=2000 hyst=500\n";
    assert(fridge_command(good, strlen(good), &c));
    assert(c.hysteresis_mc == 500 && c.grace_s == 900);
    const char *bad[] = {"", "low=2 high=8", "low=2000 high=8000", "low=8 high=2 hyst=0", "low=2 high=8 hyst=4",
        "low=2 high=8 hyst=-1", "low=2 high=8 hyst=1 x=1", "low=2 high=8 hyst=1 low=3",
        "low=2147483648 high=8 hyst=1", "low=-2147483649 high=8 hyst=1", "low=2high=8 hyst=1",
        "low=+2 high=8 hyst=1", "low=2 high=8 hyst=", "low=2 high=8 hyst=1junk"};
    alarm_config before = c;
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        assert(!fridge_command(bad[i], strlen(bad[i]), &c));
        assert(memcmp(&before, &c, sizeof(c)) == 0);
    }
    assert(!fridge_command("low=2\0 high=8 hyst=1", 20, &c));
    char huge[128]; memset(huge, 'x', sizeof(huge)); assert(!fridge_command(huge, sizeof(huge), &c));
    scenario_row rows[8]; size_t n = 77;
    assert(load("t_ms,temp_mc\r\n0,5000\r\n100,9000\n200,\n300,-1000", rows, 8, &n) && n == 4);
    assert(scenario_sample(rows,n,99).temp_mc == 5000);
    assert(scenario_sample(rows,n,100).temp_mc == 9000);
    assert(!scenario_sample(rows,n,200).valid);
    assert(scenario_sample(rows,n,UINT64_MAX).temp_mc == -1000);
    const char *invalid[] = {"", "t_ms,temp_mc\n", "1,5000\n", "0,1\n0,2\n", "0,1\n2,2\n1,3\n",
        "0,2147483648\n", "0,2,3\n", "0, 2\n", "-1,2\n", "0,1\n18446744073709551616,2\n", "0,1\n\n"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) assert(!load(invalid[i], rows, 8, &n));
    assert(!load("0,1\n1,2\n",rows,1,&n));
    for (size_t i = 0; i < 3; ++i) {
        const char *paths[] = {"scenarios/door-ajar.csv", "scenarios/power-loss.csv", "scenarios/compressor-failure.csv"};
        FILE *f = fopen(paths[i], "r"); assert(f);
        scenario_row all[SCENARIO_CAPACITY]; assert(scenario_load(f,all,SCENARIO_CAPACITY,&n)); fclose(f);
    }
    alarm_state a; assert(alarm_init(&a,&c,0)); alarm_result r;
    assert(alarm_step(&a,123,5000,true,&r)); char buf[FRIDGE_TEXT_SIZE];
    forecast_result f = {FORECAST_UNKNOWN,0,0};
    assert(fridge_format(buf,sizeof(buf),&a,&f) > 0);
    assert(!strcmp(buf,"temp_mc=5000 valid=1 alarm=0 level=0 ack=0 low=2000 high=8000 hyst=500 sample_ms=123 last_valid_ms=123 trend=unknown slope_mc_per_min=0 eta_s=-1\n"));
    assert(fridge_format(buf,4,&a,&f) == -1);
    assert(alarm_step(&a,120123,0,false,&r));
    assert(fridge_format(buf,sizeof(buf),&a,&f)>0 && strstr(buf,"valid=0 alarm=1"));
    const char *names[] = {"unknown","steady","to_high","to_low","outside"};
    for (unsigned i = 0; i < 5; ++i) {
        f = (forecast_result){(enum forecast_trend)i,-123,1001};
        assert(fridge_format(buf,sizeof(buf),&a,&f) > 0);
        char expected[100];
        snprintf(expected,sizeof(expected),"trend=%s slope_mc_per_min=-123 eta_s=%d",
                 names[i],i == 2 || i == 3 ? 2 : -1);
        assert(strstr(buf,expected));
        assert(fridge_format(buf,4,&a,&f) == -1);
        assert(fridge_forecast_format(buf,4,&f) == -1);
        for (unsigned j = 0; j < 5; ++j)
            assert(fridge_forecast_transition(i,j) ==
                   (i != j && (i == 2 || i == 3 || j == 2 || j == 3)));
    }
    unsigned value = 99;
    assert(fridge_uint("60000",1,FRIDGE_FORECAST_SPACING_MAX,&value) && value == 60000);
    assert(fridge_uint("1800",1,FRIDGE_FORECAST_HORIZON_MAX,&value) && value == 1800);
    const char *invalid_fc[] = {"0","-1","+1"," 1","1 ","1x","42949672960",""};
    for (size_t i = 0; i < sizeof(invalid_fc)/sizeof(*invalid_fc); ++i) {
        assert(!fridge_uint(invalid_fc[i],1,FRIDGE_FORECAST_SPACING_MAX,&value));
        assert(!fridge_uint(invalid_fc[i],1,FRIDGE_FORECAST_HORIZON_MAX,&value));
        assert(value == 1800);
    }
    assert(!fridge_uint("3600001",1,FRIDGE_FORECAST_SPACING_MAX,&value));
    assert(!fridge_uint("86401",1,FRIDGE_FORECAST_HORIZON_MAX,&value));
    puts("PASS fridge: option bounds, strict commands, atomic validation, CSV boundaries, invalid sensor and read format");
    return 0;
}
