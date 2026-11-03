#include "fgctl.h"
#include "portable.h"
#include "storage.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static uint64_t be(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}
/* Escape untrusted detail bytes: never let a record inject terminal controls. */
static void detail(FILE *out, const uint8_t *p)
{
    for (size_t i = 0; i < AUDIT_DETAIL_SIZE && p[i]; ++i) {
        if (p[i] >= 32 && p[i] < 127) fputc(p[i],out);
        else fprintf(out,"\\x%02x",p[i]);
    }
}
int fgctl_snapshot(FILE *f, FILE **result, size_t *count, FILE *out)
{
    /* A stable private snapshot prevents a writer changing the summary after
     * verification. It also bounds RAM independently of audit length. */
    FILE *snapshot = tmpfile();
    if (!snapshot) { if (out) fputs("error: cannot create audit snapshot\n",out); return 2; }
    uint8_t block[4096]; size_t n, bytes = 0;
    while ((n = fread(block,1,sizeof(block),f)) != 0) {
        if (n > SIZE_MAX-bytes || fwrite(block,1,n,snapshot) != n) {
            fclose(snapshot); if (out) fputs("error: snapshot write/size\n",out); return 2;
        }
        bytes += n;
    }
    if (ferror(f) || fflush(snapshot) || fseek(snapshot,0,SEEK_SET)) {
        fclose(snapshot); if (out) fputs("error: audit read/seek\n",out); return 2;
    }
    size_t bad;
    enum audit_status status = audit_verify_file(snapshot,&bad,NULL);
    if (status != AUDIT_OK) {
        if (out) fprintf(out,"audit %s: first bad record index=%zu (zero-based)\n",
                status == AUDIT_BROKEN ? "BROKEN" : "I/O error",bad);
        if (out && bytes % AUDIT_RECORD_SIZE)
            fprintf(out,"incomplete tail: %zu bytes; file unchanged (M4 resume alone may discard after prefix verification)\n",bytes % AUDIT_RECORD_SIZE);
        fclose(snapshot); return status == AUDIT_BROKEN ? 1 : 2;
    }
    if (fseek(snapshot,0,SEEK_SET)) { fclose(snapshot); return 2; }
    *result = snapshot;
    *count = bad;
    return 0;
}
static int verify(FILE *f, FILE *out)
{
    FILE *snapshot; size_t bad;
    int rc = fgctl_snapshot(f,&snapshot,&bad,out);
    if (rc) return rc;
    static const char *names[] = {"unknown", "excursion", "temperature alarm", "sensor failure",
        "sensor recovery", "escalation", "acknowledgement", "rearm", "recovery", "restart gap", "sample", "forecast hint"};
    size_t counts[12] = {0}, session = 0;
    uint64_t first = 0, last = 0, start = 0;
    bool open = false;
    audit_record r;
    for (size_t i = 0; i < bad; ++i) {
        if (fread(r.bytes,1,sizeof(r.bytes),snapshot) != sizeof(r.bytes)) { fclose(snapshot); return 2; }
        uint64_t t = be(r.bytes+8,8), type = be(r.bytes+16,4);
        if (!i) first = t;
        last = t;
        ++counts[type < 12 ? type : 0];
        if (type == ALARM_EXCURSION) { start = t; open = true; }
        if (type == ALARM_RECOVERY) {
            fprintf(out,"excursion session=%zu start_ms=",session);
            if (open) fprintf(out,"%" PRIu64,start); else fputs("unknown",out);
            fprintf(out," end_ms=%" PRIu64 " ",t); detail(out,r.bytes+24); fputc('\n',out);
            open = false;
        }
        if (type == AUDIT_GAP) {
            if (open) fprintf(out,"excursion session=%zu start_ms=%" PRIu64 " end=unknown (restart gap)\n",session,start);
            open = false;
            fprintf(out,"gap index=%zu: ",i); detail(out,r.bytes+24); fputc('\n',out);
            ++session;
        }
    }
    if (open) fprintf(out,"excursion session=%zu start_ms=%" PRIu64 " end=not observed\n",session,start);
    fprintf(out,"audit OK: records=%zu%s\n",bad,bad ? "" : " (empty; no observations)");
    for (size_t i = 0; i < 12; ++i) fprintf(out,"%s=%zu%s",names[i],counts[i],i == 11 ? "\n" : "; ");
    if (bad) fprintf(out,"first_ms=%" PRIu64 " last_ms=%" PRIu64 " (record order; process-relative clocks reset at gaps)\n",first,last);
    else fputs("first_ms=none last_ms=none\n",out);
    fputs("Integrity only: no trusted external anchor; tail removal/full rewrite cannot be detected.\n",out);
    return fclose(snapshot) ? 2 : 0;
}
int fgctl_run(int argc, char **argv, FILE *out)
{
    if (argc != 3 || (strcmp(argv[1],"verify") && strcmp(argv[1],"check-scenario") && strcmp(argv[1],"report"))) {
        fputs("usage: fgctl verify <audit.bin> | report <audit.bin> | check-scenario <scenario.csv>\n",out); return 2;
    }
    FILE *f = fopen(argv[2],"rb");
    if (!f) { if (strcmp(argv[1],"report")) fprintf(out,"error: cannot open %s\n",argv[2]); return 2; }
    int rc;
    if (!strcmp(argv[1],"verify")) rc = verify(f,out);
    else if (!strcmp(argv[1],"report")) rc = fgctl_report(f,out);
    else {
        scenario_row rows[SCENARIO_CAPACITY]; size_t count; scenario_error error;
        bool ok = scenario_load_ex(f,rows,SCENARIO_CAPACITY,&count,&error);
        rc = ok ? 0 : (ferror(f) ? 2 : 1);
        if (ok) fprintf(out,"scenario OK: rows=%zu first_ms=0 last_ms=%" PRIu64 "\n",count,rows[count-1].t_ms);
        else fprintf(out,"scenario error: line=%zu: %s\n",error.line,error.reason);
    }
    if (fclose(f)) rc = 2;
    return rc;
}
