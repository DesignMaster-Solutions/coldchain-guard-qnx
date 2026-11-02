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
    assert(fridge_format(buf,sizeof(buf),&a) > 0);
    assert(!strcmp(buf,"temp_mc=5000 valid=1 alarm=0 level=0 low=2000 high=8000 hyst=500 sample_ms=123 last_valid_ms=123\n"));
    assert(fridge_format(buf,4,&a) == -1);
    assert(alarm_step(&a,120123,0,false,&r));
    assert(fridge_format(buf,sizeof(buf),&a)>0 && strstr(buf,"valid=0 alarm=1"));
    puts("PASS fridge: option bounds, strict commands, atomic validation, CSV boundaries, invalid sensor and read format");
    return 0;
}
