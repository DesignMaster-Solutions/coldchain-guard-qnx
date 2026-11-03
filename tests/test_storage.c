#define _POSIX_C_SOURCE 200809L
#include "storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static void seed(const char *path)
{
    audit_chain chain;
    FILE *f = fridge_log_open(path,&chain);
    assert(f && chain.seq == 0);
    assert(fridge_log_sample(f,&chain,123,5000,true));
    assert(fridge_log_sample(f,&chain,456,9999,false));
    assert(!fclose(f));
}
static size_t snapshot(const char *path, unsigned char *out)
{
    FILE *f = fopen(path,"rb"); assert(f);
    size_t n = fread(out,1,2048,f); assert(!ferror(f));
    assert(!fclose(f)); return n;
}
/* The whole gap contract, not just its type: a gap never claims a valid
 * interval or a temperature (the QNX M4 observer requires the same). */
static bool gap_ok(const unsigned char *r)
{
    static const unsigned char zero[4];
    const char *d = (const char *)r + 24;
    return r[19] == AUDIT_GAP && !memcmp(r+20,zero,4) && memchr(d,0,AUDIT_DETAIL_SIZE) &&
           strstr(d,"interval=unknown ") && strstr(d,"valid=0 ") && strstr(d,"state=reset");
}
int main(void)
{
    /* TMPDIR can keep all artifacts within an explicitly allowed proof folder. */
    const char *base = getenv("TMPDIR"); if (!base) base = "build";
    char path[1024];
    assert(snprintf(path,sizeof(path),"%s/m4-storage-XXXXXX",base) > 0);
    int fd = mkstemp(path); assert(fd >= 0); close(fd); assert(!unlink(path));
    seed(path);
    unsigned char before[2048], after[2048];
    size_t initial = snapshot(path,before); assert(initial == 432);
    assert(before[19] == AUDIT_SAMPLE && before[216+19] == AUDIT_SAMPLE);
    assert(!memcmp(before+216+20,"\0\0\0\0",4));
    audit_chain chain;
    FILE *f = fridge_log_open(path,&chain); assert(f && chain.seq == 3);
    /* A competing process must not append while the first holds its lock. */
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) {
        FILE *other = fridge_log_open(path,&chain);
        if (other) fclose(other);
        _exit(other ? 1 : 0);
    }
    int status; assert(waitpid(child,&status,0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(fridge_log_sample(f,&chain,0,6000,true)); assert(!fclose(f));
    size_t n = snapshot(path,after), bad;
    assert(n == 864 && !memcmp(before,after,initial));
    assert(audit_verify(after,n,&bad,NULL) == AUDIT_OK);
    assert(gap_ok(after+432) && after[648+19] == AUDIT_SAMPLE);
    assert(strstr((char *)after+432+24,"discarded_bytes=0"));
    assert(!unlink(path));
    /* Every possible partial record length is discarded, explicitly, only
     * after the valid prefix is verified. No complete record is removed. */
    for (size_t tail = 1; tail < AUDIT_RECORD_SIZE; ++tail) {
        seed(path);
        f = fopen(path,"ab"); assert(f);
        assert(fwrite(before,1,tail,f) == tail); assert(!fclose(f));
        f = fridge_log_open(path,&chain); assert(f && chain.seq == 3); assert(!fclose(f));
        n = snapshot(path,after); assert(n == 648 && !memcmp(before,after,initial));
        assert(audit_verify(after,n,&bad,NULL) == AUDIT_OK && gap_ok(after+432));
        char detail[64]; snprintf(detail,sizeof(detail),"discarded_bytes=%zu ",tail);
        assert(strstr((char *)after+432+24,detail));
        assert(!unlink(path));
    }
    /* Corrupt complete prefix WITH a partial tail: refusal must leave every
     * byte intact, so a mutation that skips verification cannot pass. */
    seed(path); f = fopen(path,"r+b"); assert(f);
    assert(!fseek(f,30,SEEK_SET)); assert(fputc(0xff,f) != EOF);
    assert(!fseek(f,0,SEEK_END)); assert(fputc(42,f) != EOF); assert(!fclose(f));
    n = snapshot(path,before);
    f = fridge_log_open(path,&chain); assert(!f);
    assert(snapshot(path,after) == n && !memcmp(before,after,n));
    assert(!unlink(path));
    /* An existing empty log is still a restart, not evidence of no outage. */
    f = fopen(path,"wb"); assert(f); assert(!fclose(f));
    f = fridge_log_open(path,&chain); assert(f && chain.seq == 1); assert(!fclose(f));
    n = snapshot(path,after); assert(n == 216 && gap_ok(after));
    assert(audit_verify(after,n,&bad,NULL) == AUDIT_OK); assert(!unlink(path));
    puts("PASS storage: verified resume, unknown gap, partial tails, corruption refusal, exclusive writer, samples");
    return 0;
}
