#define _POSIX_C_SOURCE 200809L
#include "fgctl.h"
#include "portable.h"
#include "storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char path[] = "/tmp/fgctl-test-XXXXXX";
static char output[65536];
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
static void test_report(void)
{
    audit_chain c; audit_init(&c); audit_record r[14];
    assert(audit_append(&c,&r[0],0,AUDIT_SAMPLE,5000,"valid=1"));
    assert(audit_append(&c,&r[1],1000,ALARM_EXCURSION,9000,"valid=1"));
    assert(audit_append(&c,&r[2],2000,ALARM_TEMPERATURE,9000,"valid=1"));
    assert(audit_append(&c,&r[3],2000,ALARM_ESCALATION,9000,"level=1"));
    alarm_event e = {.t_ms=3000,.type=ALARM_ACK,.temperature_mc=9000};
    strcpy(e.who,"<script>bad</script>");
    assert(audit_append_event(&c,&r[4],&e));
    assert(audit_append(&c,&r[5],4000,AUDIT_SAMPLE,10000,"valid=1"));
    assert(audit_append(&c,&r[6],5000,ALARM_RECOVERY,5000,"duration_ms=4000 min_mc=9000 max_mc=10000"));
    assert(audit_append(&c,&r[7],6000,ALARM_SENSOR_FAILURE,5000,"valid=0"));
    assert(audit_append(&c,&r[8],7000,ALARM_SENSOR_RECOVERY,5000,"valid=1"));
    assert(audit_append(&c,&r[9],8000,AUDIT_FORECAST,5000,"trend=to_high <img src=x> & \" '"));
    assert(audit_append(&c,&r[10],0,AUDIT_GAP,0,"interval=unknown <script>gap</script>"));
    assert(audit_append(&c,&r[11],1000,AUDIT_SAMPLE,-500,"valid=1"));
    assert(audit_append(&c,&r[12],2000,AUDIT_SAMPLE,0,"valid=0"));
    assert(audit_append(&c,&r[13],3661000,ALARM_EXCURSION,-500,"valid=1"));
    write_bytes(r,sizeof(r));
    assert(run("report",path) == 0);
    assert(strstr(output,"<!doctype html>") && strstr(output,"lang=\"en\""));
    const char *labels[] = {"Temperature out of range", "Alarm", "Escalation level 1",
        "Acknowledged by &lt;script&gt;bad&lt;/script&gt;", "Recovered", "Sensor lost", "Sensor back",
        "Forecast hint", "UNKNOWN INTERVAL", "Session 2"};
    const char *cursor = output;
    for (size_t i=0; i<sizeof(labels)/sizeof(labels[0]); ++i) {
        cursor = strstr(cursor,labels[i]); assert(cursor); cursor += strlen(labels[i]);
    }
    const char *card = strstr(output,"<article class=episode>");
    assert(card);
    const char *card_end = strstr(card,"</article>");
    const char *card_ack = strstr(card,"Acknowledged by &lt;script&gt;bad&lt;/script&gt;");
    assert(card_end && card_ack && card_ack < card_end);
    const char *next_card = strstr(card_end,"<article class=episode>");
    assert(next_card && strstr(next_card,"not acknowledged"));
    assert(!strstr(next_card,"Acknowledged by"));
    assert(strstr(output,"Start: 0:00:01") && strstr(output,"End: 0:00:05"));
    assert(strstr(output,"Duration: 0:00:04") && strstr(output,"Minimum: 9.000 °C; maximum: 10.000 °C"));
    assert(strstr(output,"not acknowledged") && strstr(output,"End: not observed before end of file"));
    assert(strstr(output,"Not recorded by this prototype: calendar date and time, room temperature, vaccine inventory, actions taken."));
    assert(strstr(output,"0:00:04") && strstr(output,"9.000 °C") && strstr(output,"10.000 °C"));
    assert(strstr(output,"-0.500 °C") && strstr(output,"1:01:01"));
    assert(strstr(output,"Samples: 2; valid: 2; unavailable: 0") && strstr(output,"Samples: 2; valid: 1; unavailable: 1"));
    size_t timeline_rows = 0;
    for (const char *p=output; (p=strstr(p,"<li>")) != NULL; p += 4) ++timeline_rows;
    assert(timeline_rows == 9); /* Four samples and the gap are not event rows. */
    assert(!strstr(output,"<script>") && !strstr(output,"<img") && strstr(output,"&lt;img src=x&gt; &amp; &quot; &#39;"));
    assert(strstr(output,"Chain OK") && strstr(output,"Records: 14"));
    assert(strstr(output,"Integrity only: no trusted external anchor; tail removal/full rewrite cannot be detected."));
    assert(strstr(output,"Simulated sensor. Prototype, not a medical device or a medical record."));
    assert(strstr(output,"@media print") && strstr(output,"name=\"viewport\""));
    r[4].bytes[30] ^= 1; write_bytes(r,sizeof(r));
    assert(run("report",path) != 0 && output[0] == 0);
    r[4].bytes[30] ^= 1;
    for (size_t tail=1; tail<AUDIT_RECORD_SIZE; ++tail) {
        write_bytes(r,AUDIT_RECORD_SIZE+tail);
        assert(run("report",path) != 0 && output[0] == 0);
    }
    write_bytes(r,0); assert(run("report",path) == 0 && strstr(output,"No observations"));
}
int main(void)
{
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    test_report();
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
    assert(run("report",path) == 0);
    assert(strstr(output,"End: unknown (restart gap)") && strstr(output,"Duration: unknown"));
    const char *second = strstr(output,"<h2>Session 2</h2>");
    assert(second && strstr(second,"Start: not observed") && strstr(second,"not acknowledged"));
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
    assert(run("report",path) == 2 && output[0] == 0);
    puts("PASS fgctl: counts, excursions, gaps, middle/last tamper, deletion, all partial tails, empty, file errors, CSV diagnostics/capacity");
    return 0;
}
