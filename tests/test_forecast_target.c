/* Acceptance against our synthetic fixture, not a QNX performance measurement. */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "portable.h"
#include "storage.h"

#define LOG_PATH "/tmp/forecast-audit.bin"
#define CAPACITY 2048
static audit_record records[CAPACITY];
static uint64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) exit(1);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static bool snapshot(size_t *count)
{
    FILE *f = fopen(LOG_PATH,"rb");
    if (!f) { *count = 0; return errno == ENOENT; }
    size_t bytes = fread(records,1,sizeof(records),f), bad;
    bool ok = !ferror(f) && bytes < sizeof(records);
    fclose(f);
    *count = bytes / AUDIT_RECORD_SIZE;
    return ok && audit_verify(records,*count*AUDIT_RECORD_SIZE,&bad,NULL) == AUDIT_OK;
}
static bool device_read(char text[FRIDGE_TEXT_SIZE])
{
    int fd = open("/dev/fridge0",O_RDONLY);
    if (fd < 0) return false;
    size_t used = 0; ssize_t n = 0;
    while (used < FRIDGE_TEXT_SIZE-1 &&
           (n = read(fd,text+used,FRIDGE_TEXT_SIZE-1-used)) > 0) used += (size_t)n;
    close(fd); text[used] = 0;
    return used > 0 && n == 0;
}
static bool device_hint(void)
{
    char text[FRIDGE_TEXT_SIZE];
    if (!device_read(text)) return false;
    char *eta = strstr(text," eta_s=");
    return strstr(text," trend=to_high ") && strstr(text," alarm=0 ") &&
           eta && strtol(eta+7,NULL,10) > 0;
}
static bool reset_checks(void)
{
    const char *cmd = "low=2000 high=8001 hyst=300";
    int fd = open("/dev/fridge0",O_WRONLY);
    if (fd < 0) return false;
    bool ok = write(fd,cmd,strlen(cmd)) == (ssize_t)strlen(cmd);
    close(fd);
    char text[FRIDGE_TEXT_SIZE];
    if (!ok || !device_read(text) || !strstr(text," trend=unknown ") ||
        !strstr(text," alarm=1 ") || !strstr(text," high=8001 ")) return false;
    bool gap = false;
    uint64_t deadline = now_ms()+12000;
    while (now_ms() <= deadline) {
        if (!device_read(text)) return false;
        if (strstr(text," valid=0 ")) {
            if (!strstr(text," trend=unknown ") || !strstr(text," eta_s=-1")) return false;
            gap = true;
        } else if (gap) {
            if (!strstr(text," trend=unknown ")) return false;
            size_t n;
            return snapshot(&n) && n > 0;
        }
        struct timespec pause = {0,20000000}; nanosleep(&pause,NULL);
    }
    return false;
}
int main(void)
{
    alarm(45); /* Includes device I/O; the boot script always cleans up the daemon. */
    uint64_t deadline = now_ms()+30000;
    bool observed = false;
    for (;;) {
        /* Read device first, then the audit prefix: a hint only counts if no
         * temperature alarm has yet reached the audit after that read. */
        bool hint = device_hint();
        size_t n;
        if (!snapshot(&n)) goto fail;
        size_t high = 0, outside = 0, samples_after = 0, samples = 0;
        bool temperature = false;
        for (size_t i = 0; i < n; ++i) {
            audit_record *r = &records[i];
            if (r->bytes[19] == AUDIT_FORECAST) {
                if (!memchr(r->bytes+24,0,AUDIT_DETAIL_SIZE)) goto fail;
                const char *detail = (const char *)r->bytes+24;
                if (strstr(detail,"trend=to_high ")) {
                    const char *eta = strstr(detail," eta_s=");
                    const char *slope = strstr(detail," slope_mc_per_min=");
                    if (temperature || outside || !eta || strtol(eta+7,NULL,10) <= 0 ||
                        !slope || strtol(slope+18,NULL,10) <= 0 || ++high != 1) goto fail;
                } else if (strstr(detail,"trend=outside ") && strstr(detail," eta_s=-1")) {
                    if (high != 1 || ++outside != 1) goto fail;
                } else goto fail;
            }
            if (r->bytes[19] == ALARM_TEMPERATURE) {
                if (high != 1 || outside != 1) goto fail;
                temperature = true;
            }
            if (r->bytes[19] == AUDIT_SAMPLE) {
                ++samples;
                if (temperature) ++samples_after;
            }
        }
        if (hint && !temperature) observed = true;
        if (temperature && samples_after >= 5) {
            if (!observed || high != 1 || outside != 1 || samples <= high+outside) goto fail;
            if (!reset_checks()) goto fail;
            alarm(0);
            puts("coldchain: device advice precedes alarm, chain, transition-only hints and limit/sensor resets PASS");
            return 0;
        }
        if (now_ms() > deadline) goto fail;
        struct timespec pause = {0,20000000}; nanosleep(&pause,NULL);
    }
fail:
    puts("coldchain: forecast acceptance FAILED");
    return 1;
}
