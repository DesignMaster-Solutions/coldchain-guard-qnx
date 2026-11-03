/* Local device acknowledgement; pass/fail fixture, not a QNX benchmark. */
#define _POSIX_C_SOURCE 200809L
#include "portable.h"
#include "storage.h"
#include "sha256.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t clock_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) exit(1);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void pause_poll(void)
{
    struct timespec t = {0,20000000}; nanosleep(&t,NULL);
}
static bool read_state(char text[FRIDGE_TEXT_SIZE])
{
    int fd = open("/dev/fridge0",O_RDONLY);
    if (fd < 0) return false;
    size_t used = 0; ssize_t n;
    while ((n = read(fd,text+used,FRIDGE_TEXT_SIZE-1-used)) > 0) {
        used += (size_t)n;
        if (used == FRIDGE_TEXT_SIZE-1) { close(fd); return false; }
    }
    text[used] = 0;
    return close(fd) == 0 && n == 0 && used > 0;
}
static bool command(const char *s, bool accept)
{
    int fd = open("/dev/fridge0",O_WRONLY);
    if (fd < 0) return false;
    errno = 0;
    ssize_t n = write(fd,s,strlen(s)); int error = errno;
    bool ok = accept ? n == (ssize_t)strlen(s) : n == -1 && error == EINVAL;
    return close(fd) == 0 && ok;
}
static bool live(void)
{
    char text[FRIDGE_TEXT_SIZE];
    uint64_t deadline = clock_ms()+20000;
    do {
        if (!read_state(text)) return false;
        if (strstr(text," alarm=1 ")) break;
        pause_poll();
    } while (clock_ms() < deadline);
    unsigned level;
    char *p = strstr(text," level=");
    if (!strstr(text," alarm=1 ") || !strstr(text," ack=0 ") || !p ||
        sscanf(p," level=%u",&level) != 1 || level >= 3 ||
        !strstr(text," trend=outside ")) return false;
    if (!command("ack who=<bad>",false) || !read_state(text) ||
        !strstr(text," ack=0 ")) return false;
    if (!command("ack who=Clinic.Team_1\n",true) || !read_state(text) ||
        !strstr(text," ack=1 ") || !strstr(text," trend=outside ")) return false;
    if (!command("ack who=Second",false) || !command("ack who=two names",false)) return false;
    char expected[64]; snprintf(expected,sizeof(expected)," alarm=1 level=%u ack=1 ",level);
    deadline = clock_ms()+2500; /* More than two configured one-second ladder periods. */
    do {
        if (!read_state(text) || !strstr(text,expected) ||
            !strstr(text," trend=outside ") ||
            !strstr(text," low=2000 high=8000 hyst=300 ")) return false;
        pause_poll();
    } while (clock_ms() < deadline);
    puts("coldchain: local ack, frozen ladder, duplicate/invalid rejection, forecast preserved PASS");
    return true;
}
static bool resumed(void)
{
    char text[FRIDGE_TEXT_SIZE];
    if (!read_state(text) || !strstr(text," ack=0 ")) return false;
    /* Wait for several samples so the report includes both real sessions. */
    uint64_t deadline = clock_ms()+500;
    do { pause_poll(); } while (clock_ms() < deadline);
    return read_state(text) && strstr(text," ack=0 ");
}
static bool audit(const char *path)
{
    FILE *f = fopen(path,"rb"); if (!f) return false;
    size_t count; audit_chain final;
    if (audit_verify_file(f,&count,&final) != AUDIT_OK || !count) { fclose(f); return false; }
    rewind(f);
    audit_record r;
    unsigned acks=0, gaps=0, before=0, after=0;
    while (fread(&r,sizeof(r),1,f) == 1) {
        if (r.bytes[19] == ALARM_ACK) {
            if (++acks != 1 || gaps || !memchr(r.bytes+24,0,AUDIT_DETAIL_SIZE) ||
                !strstr((char *)r.bytes+24," who=Clinic.Team_1")) { fclose(f); return false; }
            const char *who = strstr((char *)r.bytes+24," who=");
            if (strcmp(who," who=Clinic.Team_1")) { fclose(f); return false; }
        }
        if (r.bytes[19] == AUDIT_GAP) ++gaps;
        if (r.bytes[19] == AUDIT_SAMPLE) { if (gaps) ++after; else ++before; }
    }
    if (ferror(f) || acks != 1 || gaps != 1 || !before || !after) { fclose(f); return false; }
    rewind(f);
    sha256_ctx hash; sha256_init(&hash);
    size_t n, bytes=0; uint8_t block[AUDIT_RECORD_SIZE], digest[32];
    puts("M7-AUDIT-BEGIN v1");
    while ((n=fread(block,1,sizeof(block),f)) != 0) {
        sha256_update(&hash,block,n); bytes += n;
        fputs("M7-DATA ",stdout);
        for (size_t i=0;i<n;++i) printf("%02x",block[i]);
        putchar('\n');
    }
    bool ok = !ferror(f); if (fclose(f)) ok=false;
    sha256_final(&hash,digest);
    printf("M7-AUDIT-END bytes=%zu sha256=",bytes);
    for (size_t i=0;i<sizeof(digest);++i) printf("%02x",digest[i]);
    putchar('\n');
    return ok && !fflush(stdout) && !ferror(stdout);
}
int main(int argc, char **argv)
{
    alarm(30);
    bool ok = argc == 2 && (!strcmp(argv[1],"live") ? live() :
                           !strcmp(argv[1],"resumed") ? resumed() : audit(argv[1]));
    return ok ? 0 : 1;
}
