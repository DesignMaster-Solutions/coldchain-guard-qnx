#include "fgctl.h"
#include "storage.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static uint64_t be(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}
static int64_t temperature(const audit_record *r)
{
    uint64_t n = be(r->bytes+20,4);
    return n <= INT32_MAX ? (int64_t)n : (int64_t)n - INT64_C(4294967296);
}
/* All record text, including parsed who and raw detail, uses this one sink. */
static void html(FILE *out, const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        switch (*p) {
        case '&': fputs("&amp;",out); break;
        case '<': fputs("&lt;",out); break;
        case '>': fputs("&gt;",out); break;
        case '"': fputs("&quot;",out); break;
        case '\'': fputs("&#39;",out); break;
        default:
            if (*p >= 32 && *p < 127) fputc(*p,out);
            else fprintf(out,"\\x%02x",*p);
        }
    }
}
static void time_text(FILE *out, uint64_t ms)
{
    uint64_t s = ms/1000;
    fprintf(out,"%" PRIu64 ":%02" PRIu64 ":%02" PRIu64,s/3600,s/60%60,s%60);
}
static void celsius(FILE *out, int64_t mc)
{
    uint64_t magnitude = mc < 0 ? (uint64_t)(-mc) : (uint64_t)mc;
    fprintf(out,"%s%" PRIu64 ".%03" PRIu64 " °C",mc < 0 ? "-" : "",magnitude/1000,magnitude%1000);
}
/* Fields are separated by spaces; reject duplicates and partial numeric parses.
 * The chain authenticates neither the semantics nor the author's claims. */
static bool field(const char *text, const char *key, char *value)
{
    size_t k = strlen(key); bool found = false;
    for (const char *p = text; *p;) {
        const char *end = strchr(p,' ');
        size_t n = end ? (size_t)(end-p) : strlen(p);
        if (n >= k && !strncmp(p,key,k)) {
            if (found) return false;
            memcpy(value,p+k,n-k); value[n-k] = 0; found = true;
        }
        if (!end) break;
        p = end+1;
    }
    return found;
}
static bool unsigned_field(const char *text, const char *key, uint64_t *v)
{
    char value[AUDIT_DETAIL_SIZE+1];
    if (!field(text,key,value) || !value[0]) return false;
    for (const char *p=value; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0; char *end; uintmax_t n = strtoumax(value,&end,10);
    if (errno || *end || n > UINT64_MAX) return false;
    *v = (uint64_t)n; return true;
}
static bool signed_field(const char *text, const char *key, int64_t *v)
{
    char value[AUDIT_DETAIL_SIZE+1];
    if (!field(text,key,value)) return false;
    const char *p = value; if (*p == '-') ++p;
    if (!*p) return false;
    for (; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0; char *end; intmax_t n = strtoimax(value,&end,10);
    if (errno || *end || n < INT32_MIN || n > INT32_MAX) return false;
    *v = (int64_t)n; return true;
}
static void get_detail(const audit_record *r, char *text)
{
    memcpy(text,r->bytes+24,AUDIT_DETAIL_SIZE); text[AUDIT_DETAIL_SIZE] = 0;
}
static bool read_record(FILE *f, audit_record *r)
{
    return fread(r->bytes,1,sizeof(r->bytes),f) == sizeof(r->bytes);
}
static void event(FILE *out, const audit_record *r, bool *excursion)
{
    uint64_t type = be(r->bytes+16,4), n;
    int64_t low, high;
    char text[AUDIT_DETAIL_SIZE+1], who[AUDIT_DETAIL_SIZE+1]; get_detail(r,text);
    fputs("<li><time>",out); time_text(out,be(r->bytes+8,8)); fputs("</time> <strong>",out);
    switch (type) {
    case ALARM_EXCURSION: fputs("Temperature out of range",out); *excursion = true; break;
    case ALARM_TEMPERATURE: fputs("Alarm",out); break;
    case ALARM_ESCALATION:
        fputs("Escalation level ",out);
        if (unsigned_field(text,"level=",&n)) fprintf(out,"%" PRIu64,n); else fputs("unknown",out);
        break;
    case ALARM_ACK:
        fputs("Acknowledged by ",out);
        if (field(text,"who=",who) && who[0]) html(out,who); else fputs("unknown",out);
        break;
    case ALARM_RECOVERY:
        fputs("Recovered</strong>; duration: ",out);
        if (unsigned_field(text,"duration_ms=",&n)) time_text(out,n); else fputs("unknown",out);
        fputs("; excursion minimum: ",out);
        if (signed_field(text,"min_mc=",&low)) celsius(out,low); else fputs("unknown",out);
        fputs("; maximum: ",out);
        if (signed_field(text,"max_mc=",&high)) celsius(out,high); else fputs("unknown",out);
        if (!*excursion) fputs("; start not observed in this session",out);
        *excursion = false; fputs("<strong>",out); break;
    case ALARM_SENSOR_FAILURE: fputs("Sensor lost",out); break;
    case ALARM_SENSOR_RECOVERY: fputs("Sensor back",out); break;
    case ALARM_REARM: fputs("Alarm re-armed",out); break;
    case AUDIT_FORECAST: fputs("Forecast hint (advisory)",out); break;
    default: fprintf(out,"Unknown event type %" PRIu64,type); break;
    }
    fputs("</strong>",out);
    if (unsigned_field(text,"valid=",&n) && n == 1) {
        fputs("; temperature: ",out); celsius(out,temperature(r));
    }
    fputs("<small>Recorded detail: ",out); html(out,text); fputs("</small></li>\n",out);
}
typedef struct {
    bool open, extrema, ack;
    int64_t minimum, maximum;
} episode;
static void extrema(episode *e, int64_t temp)
{
    if (!e->extrema || temp < e->minimum) e->minimum=temp;
    if (!e->extrema || temp > e->maximum) e->maximum=temp;
    e->extrema=true;
}
static void episode_end(FILE *out, episode *e, const audit_record *recovery, const char *reason)
{
    fputs("<p>End: ",out);
    if (recovery) time_text(out,be(recovery->bytes+8,8)); else html(out,reason);
    fputs("; Duration: ",out);
    char text[AUDIT_DETAIL_SIZE+1]; uint64_t duration; int64_t low, high;
    if (recovery) get_detail(recovery,text);
    if (recovery && unsigned_field(text,"duration_ms=",&duration)) time_text(out,duration);
    else fputs("unknown",out);
    fputs(".</p><p>Minimum: ",out);
    if (recovery && signed_field(text,"min_mc=",&low) && signed_field(text,"max_mc=",&high)) {
        e->minimum=low; e->maximum=high; e->extrema=true;
    }
    if (e->extrema) { celsius(out,e->minimum); fputs("; maximum: ",out); celsius(out,e->maximum); }
    else fputs("not observed; maximum: not observed",out);
    fputs(" (observed excursion readings only).</p>",out);
    if (!e->ack) fputs("<p>not acknowledged</p>",out);
    fputs("</article>\n",out); e->open=false;
}
/* Session-local episode cards; acknowledgements never cross a restart gap.
 * Recovery fields are the core's episode extrema, excluding its recovery sample.
 * Incomplete episodes use only observed valid readings, never a fabricated end. */
static int excursions(FILE *f, FILE *out, size_t records, bool gap)
{
    episode e={0}; size_t number=0;
    fputs("<h3>Excursions</h3>\n",out);
    for (size_t i=0;i<records;++i) {
        audit_record r; if (!read_record(f,&r)) return 2;
        uint64_t type=be(r.bytes+16,4), flag;
        char text[AUDIT_DETAIL_SIZE+1], who[AUDIT_DETAIL_SIZE+1]; get_detail(&r,text);
        if (type == ALARM_EXCURSION || (type == ALARM_RECOVERY && !e.open)) {
            if (e.open) episode_end(out,&e,NULL,"unknown (another excursion start)");
            e=(episode){.open=true};
            fprintf(out,"<article class=episode><h4>Excursion %zu</h4><p>Start: ",++number);
            if (type == ALARM_EXCURSION) time_text(out,be(r.bytes+8,8)); else fputs("not observed",out);
            fputs(".</p>\n",out);
        }
        if (!e.open) continue;
        if (type == ALARM_RECOVERY) { episode_end(out,&e,&r,NULL); continue; }
        if ((type == AUDIT_SAMPLE || type == ALARM_EXCURSION) &&
            unsigned_field(text,"valid=",&flag) && flag == 1) extrema(&e,temperature(&r));
        if (type == ALARM_ACK) {
            fputs("<p>Acknowledged by ",out);
            if (field(text,"who=",who) && *who) html(out,who); else fputs("unknown",out);
            fputs(" at ",out); time_text(out,be(r.bytes+8,8)); fputs(".</p>\n",out); e.ack=true;
        }
    }
    if (e.open) episode_end(out,&e,NULL,gap ? "unknown (restart gap)" : "not observed before end of file");
    if (!number) fputs("<p>No temperature excursions recorded.</p>\n",out);
    return 0;
}

/* One bounded-memory scan per session followed by its timeline: samples never
 * become timeline rows. fpos_t avoids assuming a size_t fits a long offset. */
static int sessions(FILE *f, FILE *out)
{
    size_t session = 0;
    for (;;) {
        fpos_t start, next; if (fgetpos(f,&start)) return 2;
        size_t records=0, samples=0, valid=0;
        int64_t minimum=0, maximum=0;
        uint64_t first=0,last=0; bool gap=false;
        audit_record r, boundary;
        while (read_record(f,&r)) {
            if (be(r.bytes+16,4) == AUDIT_GAP) { boundary=r; gap=true; break; }
            uint64_t t=be(r.bytes+8,8);
            if (!records) first=t;
            last=t; ++records;
            if (be(r.bytes+16,4) == AUDIT_SAMPLE) {
                ++samples; char text[AUDIT_DETAIL_SIZE+1]; uint64_t flag; get_detail(&r,text);
                if (unsigned_field(text,"valid=",&flag) && flag == 1) {
                    int64_t temp=temperature(&r);
                    if (!valid || temp < minimum) minimum=temp;
                    if (!valid || temp > maximum) maximum=temp;
                    ++valid;
                }
            }
        }
        if (ferror(f) || fgetpos(f,&next)) return 2;
        fprintf(out,"<section><h2>Session %zu</h2>\n",++session);
        if (!records) fputs("<p>No observations in this session.</p>\n",out);
        else {
            fputs("<p>Observed record span: ",out); time_text(out,first); fputs(" to ",out); time_text(out,last); fputs(".</p>\n",out);
        }
        fprintf(out,"<p class=summary>Samples: %zu; valid: %zu; unavailable: %zu",samples,valid,samples-valid);
        if (valid) { fputs("; minimum: ",out); celsius(out,minimum); fputs("; maximum: ",out); celsius(out,maximum); }
        else fputs("; minimum and maximum: not observed",out);
        fputs(".</p>\n<ol>\n",out);
        if (fsetpos(f,&start)) return 2;
        bool excursion=false;
        for (size_t i=0; i<records; ++i) {
            if (!read_record(f,&r)) return 2;
            if (be(r.bytes+16,4) != AUDIT_SAMPLE) event(out,&r,&excursion);
        }
        fputs("</ol>\n",out);
        if (excursion) fprintf(out,"<p>Excursion end: %s.</p>\n",gap ? "unknown (restart gap)" : "not observed before end of file");
        if (fsetpos(f,&start) || excursions(f,out,records,gap)) return 2;
        fputs("</section>\n",out);
        if (!gap) break;
        char text[AUDIT_DETAIL_SIZE+1]; get_detail(&boundary,text);
        fputs("<aside class=gap><h2>UNKNOWN INTERVAL</h2><p>Coverage between sessions is unknown. "
              "Clock and alarm state restart here. No outage duration or missing temperature can be inferred.</p><small>",out);
        html(out,text); fputs("</small></aside>\n",out);
        if (fsetpos(f,&next)) return 2;
    }
    return 0;
}
int fgctl_report(FILE *input, FILE *out)
{
    FILE *snapshot; size_t count;
    int rc = fgctl_snapshot(input,&snapshot,&count,NULL);
    if (rc) return rc; /* No HTML or other stdout bytes before complete verification. */
    fputs("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
          "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
          "<title>Cold-Chain Guard | Audit report</title><style>"
          "*{box-sizing:border-box}body{font:17px/1.6 system-ui,sans-serif;color:#182f39;background:#f6f7f5;margin:0}"
          "main{max-width:850px;margin:auto;padding:32px 24px}h1{font-size:2rem;line-height:1.2}"
          "h2{font-size:1.25rem}header{border-top:6px solid #14655e;padding-top:12px}"
          ".verified{border-left:5px solid #14655e;background:#e7f1ed;padding:16px}"
          ".episode{border-left:3px solid #b5c4c5;padding:0 16px;margin:16px 0;break-inside:avoid}section{border-bottom:1px solid #b5c4c5;padding:16px 0}.summary{font-weight:600}"
          "ol{list-style:none;padding:0}li{padding:12px 0;border-top:1px solid #d3dddc;break-inside:avoid}"
          "time{display:inline-block;font-variant-numeric:tabular-nums;min-width:5.5em}"
          "small{display:block;font-size:.8rem;color:#41565d;overflow-wrap:anywhere}"
          ".gap{border:3px dashed #855b11;background:#fff4d5;padding:16px;margin:24px 0;break-inside:avoid}"
          ".gap h2{margin-top:0}footer{font-size:.85rem;border-top:2px solid #182f39;margin-top:32px;padding-top:16px}"
          "@media(max-width:480px){main{padding:20px 16px}h1{font-size:1.65rem}time{display:block}}"
          "@media print{body{background:white;color:black}main{max-width:none;padding:0}.verified,.gap{background:white}"
          "h2{break-after:avoid}small{color:black}footer{position:static}}"
          "</style></head><body><main><header><p>COLD-CHAIN GUARD / QNX</p><h1>Audit report</h1></header>\n",out);
    fprintf(out,"<div class=verified><strong>Chain OK</strong> · Records: %zu<p>Integrity only: no trusted external anchor; tail removal/full rewrite cannot be detected.</p></div>\n",count);
    fputs("<p>Times are h:mm:ss since the start of each process, rounded down to seconds, "
          "not calendar dates. Clock and alarm state restart at every unknown interval. "
          "Read events in record order; times from different sessions cannot be compared.</p>"
          "<p>Acknowledgement is a local device action. It does not establish that anyone was notified. "
          "Chain integrity does not establish the truth of recorded claims.</p>\n",out);
    if (!count) fputs("<p>No observations. The audit is empty.</p>\n",out);
    else rc = sessions(snapshot,out);
    fputs("<p>Not recorded by this prototype: calendar date and time, room temperature, vaccine inventory, actions taken.</p><footer>Simulated sensor. Prototype, not a medical device or a medical record.</footer>"
          "</main></body></html>\n",out);
    if (fclose(snapshot) || ferror(out)) rc=2;
    return rc;
}
