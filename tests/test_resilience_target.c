/* Pass/fail against our configured restart bounds; no measured times printed. */
#include <sys/neutrino.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "storage.h"

#define LOG_PATH "/tmp/m4-audit.bin"
#define CONTROL "/dev/slm-coldchain"
#define READY_MS 20000u
#define RESTART_MS 20000u
#define STOP_MS 5000u
#define CAPACITY 1024
static audit_record records[CAPACITY], before[CAPACITY];
static uint64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) exit(1);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void nap(void) { struct timespec t = {0,20000000}; nanosleep(&t,NULL); }
/* Verify an immutable prefix while the daemon is running. Final verification
 * below runs only after SLM has stopped it, and rejects any partial tail. */
static bool snapshot(size_t *count, bool final)
{
    FILE *f = fopen(LOG_PATH,"rb");
    if (!f) { *count = 0; return !final && errno == ENOENT; }
    size_t bytes = fread(records,1,sizeof(records),f), bad;
    bool ok = !ferror(f) && bytes < sizeof(records);
    fclose(f);
    if (final && bytes % AUDIT_RECORD_SIZE) ok = false;
    *count = bytes / AUDIT_RECORD_SIZE;
    return ok && audit_verify(records,*count*AUDIT_RECORD_SIZE,&bad,NULL) == AUDIT_OK;
}
static bool samples(size_t count, bool resumed)
{
    size_t gaps = 0, pre = 0, post = 0;
    for (size_t i = 0; i < count; ++i) {
        const audit_record *r = &records[i];
        if (r->bytes[19] == AUDIT_GAP) {
            ++gaps;
            if (!memchr(r->bytes+24,0,AUDIT_DETAIL_SIZE) ||
                !strstr((const char *)r->bytes+24,"interval=unknown") ||
                !strstr((const char *)r->bytes+24,"valid=0") ||
                memcmp(r->bytes+20,"\0\0\0\0",4)) return false;
        }
        if (r->bytes[19] == AUDIT_SAMPLE) {
            if (gaps) ++post; else ++pre;
        }
    }
    return resumed ? gaps == 1 && pre >= 2 && post >= 2 : gaps == 0 && pre >= 2;
}
/* Query the process that actually serves this opened device, not a name or
 * stale pidfile. Also require it to answer a real read. Outer alarm bounds I/O. */
static pid_t serving(void)
{
    int fd = open("/dev/fridge0",O_RDONLY);
    if (fd < 0) return -1;
    struct _server_info info;
    char text[256];
    bool ok = ConnectServerInfo(0,fd,&info) == fd && !(info.flags & _NTO_COF_DEAD);
    ssize_t n = ok ? read(fd,text,sizeof(text)-1) : -1;
    close(fd);
    if (n <= 0) return -1;
    text[n] = 0;
    return strstr(text,"temp_mc=") && strstr(text,"sample_ms=") ? info.pid : -1;
}
static bool stop_component(void)
{
    int fd = open(CONTROL,O_RDWR);
    if (fd < 0) return false;
    const char cmd[] = "stop coldchain-fridged";
    bool ok = write(fd,cmd,sizeof(cmd)-1) == (ssize_t)sizeof(cmd)-1;
    if (close(fd)) ok = false;
    return ok;
}
int main(void)
{
    alarm(60); /* Entire test including blocked open/read and control writes. */
    pid_t first = -1, next = -1;
    size_t n = 0, prefix = 0;
    uint64_t deadline = now_ms()+READY_MS;
    for (;;) {
        if (!snapshot(&n,false)) goto fail;
        first = serving();
        if (first > 0 && samples(n,false)) break;
        if (now_ms() > deadline) goto fail;
        nap();
    }
    prefix = n;
    memcpy(before,records,n*sizeof(*records));
    if (kill(first,SIGKILL)) goto fail;
    deadline = now_ms()+RESTART_MS;
    for (;;) {
        next = serving();
        if (!snapshot(&n,false) || now_ms() > deadline) goto fail;
        if (next > 0 && next != first && samples(n,true)) break;
        nap();
    }
    puts("coldchain: new serving process and restart acceptance PASS");
    if (!stop_component()) goto fail;
    deadline = now_ms()+STOP_MS;
    while (kill(next,0) == 0 || errno != ESRCH) {
        if (now_ms() > deadline) goto fail;
        nap();
    }
    if (access("/dev/fridge0",F_OK) == 0 || !snapshot(&n,true) ||
        n < prefix || memcmp(before,records,prefix*sizeof(*records)) || !samples(n,true)) goto fail;
    alarm(0);
    puts("coldchain: complete chain, preserved prefix, one unknown gap and new samples PASS");
    return 0;
fail:
    puts("coldchain: resilience acceptance FAILED");
    return 1;
}
