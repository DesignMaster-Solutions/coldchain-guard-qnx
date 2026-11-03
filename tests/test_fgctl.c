#define _POSIX_C_SOURCE 200809L
#include "fgctl.h"
#include "portable.h"
#include "storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char path[] = "/tmp/fgctl-test-XXXXXX";
static char output[8192];
static void write_bytes(const void *p, size_t n)
{
    FILE *f = fopen(path,"wb"); assert(f);
    assert(fwrite(p,1,n,f) == n); assert(!fclose(f));
}
static int run(const char *cmd, const char *file)
{
    FILE *out = tmpfile(); assert(out);
    char *argv[] = {"fgctl",(char *)cmd,(char *)file};
    int rc = fgctl_run(3,argv,out);
    assert(!fflush(out)); rewind(out);
    size_t n = fread(output,1,sizeof(output)-1,out); output[n] = 0;
    assert(!ferror(out)); assert(!fclose(out));
    return rc;
}
int main(void)
{
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    audit_chain chain; audit_init(&chain);
    audit_record records[12];
    /* Every known type, including a gap; generated with the production core. */
    for (size_t i = 0; i < 11; ++i)
        assert(audit_append(&chain,&records[i],i*1000,(uint32_t)i+1,5000,
            i == 8 ? "interval=unknown valid=0 discarded_bytes=7 clock=process-relative state=reset" : "valid=1"));
    assert(audit_append(&chain,&records[11],3000,ALARM_EXCURSION,9000,"valid=1"));
    write_bytes(records,sizeof(records));
    assert(run("verify",path) == 0);
    /* Whole line: a lone "recovery=1;" also matches inside "sensor recovery=1;". */
    assert(strstr(output,"records=12\n") && strstr(output,"\nunknown=0; excursion=2; temperature alarm=1; sensor failure=1; "
        "sensor recovery=1; escalation=1; acknowledgement=1; rearm=1; recovery=1; restart gap=1; sample=1; forecast hint=1\n"));
    assert(strstr(output,"first_ms=0 last_ms=3000") && strstr(output,"discarded_bytes=7"));
    assert(strstr(output,"start_ms=0 end_ms=7000") && strstr(output,"session=1 start_ms=3000 end=not observed"));
    records[5].bytes[30] ^= 1;
    write_bytes(records,sizeof(records));
    assert(run("verify",path) == 1 && strstr(output,"index=5 (zero-based)"));
    records[5].bytes[30] ^= 1;
    records[11].bytes[AUDIT_HASH_OFFSET] ^= 1;
    write_bytes(records,sizeof(records));
    assert(run("verify",path) == 1 && strstr(output,"index=11 (zero-based)"));
    records[11].bytes[AUDIT_HASH_OFFSET] ^= 1;
    audit_record removed[11];
    memcpy(removed,records,5*sizeof(audit_record));
    memcpy(removed+5,records+6,6*sizeof(audit_record));
    write_bytes(removed,sizeof(removed));
    assert(run("verify",path) == 1 && strstr(output,"index=5 (zero-based)"));
    for (size_t tail = 1; tail < AUDIT_RECORD_SIZE; ++tail) {
        write_bytes(records,2*AUDIT_RECORD_SIZE+tail);
        assert(run("verify",path) == 1 && strstr(output,"index=2 (zero-based)"));
        assert(strstr(output,"incomplete tail:") && strstr(output,"file unchanged"));
        FILE *f = fopen(path,"rb"); assert(f); assert(!fseek(f,0,SEEK_END));
        assert(ftell(f) == (long)(2*AUDIT_RECORD_SIZE+tail)); fclose(f);
    }
    write_bytes(records,1);
    assert(run("verify",path) == 1 && strstr(output,"index=0 (zero-based)"));
    write_bytes(records,0);
    assert(run("verify",path) == 0 && strstr(output,"empty; no observations") && strstr(output,"first_ms=none"));
    /* A valid removed tail is undetectable without a trusted final anchor. */
    write_bytes(records,11*AUDIT_RECORD_SIZE);
    assert(run("verify",path) == 0 && strstr(output,"records=11"));
    audit_record episodes[3]; audit_init(&chain);
    assert(audit_append(&chain,&episodes[0],100,ALARM_EXCURSION,9000,"valid=1"));
    assert(audit_append(&chain,&episodes[1],0,AUDIT_GAP,0,"interval=unknown valid=0\033"));
    assert(audit_append(&chain,&episodes[2],200,ALARM_RECOVERY,5000,"valid=1"));
    write_bytes(episodes,sizeof(episodes));
    assert(run("verify",path) == 0);
    assert(strstr(output,"session=0 start_ms=100 end=unknown (restart gap)"));
    assert(strstr(output,"session=1 start_ms=unknown end_ms=200"));
    assert(strstr(output,"\\x1b") && !strchr(output,27));
    const char *good = "t_ms,temp_mc\n0,5000\n123,\n456,-200\n";
    write_bytes(good,strlen(good));
    assert(run("check-scenario",path) == 0 && strstr(output,"rows=3"));
    const char *bad = "t_ms,temp_mc\n0,5000\n123,wrong\n";
    write_bytes(bad,strlen(bad));
    assert(run("check-scenario",path) == 1 && strstr(output,"line=3:") && strstr(output,"int32"));
    FILE *f = fopen(path,"wb"); assert(f);
    for (size_t i = 0; i <= SCENARIO_CAPACITY; ++i) fprintf(f,"%zu,5000\n",i);
    fclose(f);
    assert(run("check-scenario",path) == 1 && strstr(output,"line=4097:") && strstr(output,"capacity"));
    assert(run("nonsense",path) == 2);
    assert(!unlink(path));
    assert(run("verify",path) == 2 && strstr(output,"cannot open"));
    assert(run("check-scenario",path) == 2);
    puts("PASS fgctl: counts, excursions, gaps, middle/last tamper, deletion, all partial tails, empty, file errors, CSV diagnostics/capacity");
    return 0;
}
