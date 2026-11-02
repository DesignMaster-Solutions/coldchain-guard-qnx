#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "portable.h"
#include "audit.h"

static void read_state(char *buf)
{
    int fd = open("/dev/fridge0",O_RDONLY); assert(fd >= 0);
    size_t n = 0; ssize_t got;
    /* Exercise fragmented reads and per-open EOF. */
    while ((got = read(fd,buf+n,1)) == 1) { ++n; assert(n < FRIDGE_TEXT_SIZE-1); }
    assert(got == 0); buf[n] = 0; assert(close(fd) == 0);
}
static void command(const char *s, bool valid)
{
    int fd = open("/dev/fridge0",O_WRONLY); assert(fd >= 0);
    errno = 0; ssize_t n = write(fd,s,strlen(s));
    if (valid) assert(n == (ssize_t)strlen(s));
    else assert(n == -1 && errno == EINVAL);
    assert(close(fd) == 0);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    char buf[FRIDGE_TEXT_SIZE];
    bool ready = false;
    for (unsigned i = 0; i < 200; ++i) {
        read_state(buf);
        if (strstr(buf,"temp_mc=5000 valid=1")) { ready = true; break; }
        struct timespec pause = {0,100000000}; nanosleep(&pause,NULL);
    }
    assert(ready);
    assert(strstr(buf,"low=2000 high=8000 hyst=300"));
    puts("coldchain: initial device read verified");
    command("low=2500 high=7500 hyst=500\n",true);
    read_state(buf); fputs(buf,stdout);
    assert(strstr(buf,"low=2500 high=7500 hyst=500"));
    command("low=8000 high=2000 hyst=500",false);
    command("low=2000 high=8000",false);
    command("low=2000 high=8000 hyst=500 junk",false);
    read_state(buf); assert(strstr(buf,"low=2500 high=7500 hyst=500"));
    puts("coldchain: write and EINVAL atomicity verified");
    FILE *f = fopen(argv[1],"rb"); assert(f);
    size_t bad; audit_chain final;
    assert(audit_verify_file(f,&bad,&final) == AUDIT_OK && final.seq >= 2);
    rewind(f);
    audit_record record; bool excursion = false, recovery = false;
    FILE *changed = tmpfile(); assert(changed);
    while (fread(&record,sizeof(record),1,f) == 1) {
        if (record.bytes[19] == ALARM_EXCURSION) excursion = true;
        if (record.bytes[19] == ALARM_RECOVERY) recovery = true;
        record.bytes[24] ^= 1; assert(audit_write(changed,&record));
    }
    assert(!ferror(f) && excursion && recovery); assert(fclose(f) == 0);
    rewind(changed); assert(audit_verify_file(changed,&bad,NULL) == AUDIT_BROKEN); fclose(changed);
    puts("coldchain: audit chain verified; excursion/recovery present; tamper rejected");
    return 0;
}
