#include "alarm.h"
#include "audit.h"
#include "sha256.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define MINUTE UINT64_C(60000)

static alarm_state fresh(void)
{
    alarm_state a;
    alarm_config c = alarm_defaults();
    assert(alarm_init(&a, &c, 0));
    return a;
}

static alarm_result step(alarm_state *a, uint64_t t, int32_t temp, bool valid)
{
    alarm_result r;
    assert(alarm_step(a, t, temp, valid, &r));
    assert(r.count <= ALARM_MAX_EVENTS);
    return r;
}

static const alarm_event *event(const alarm_result *r, enum alarm_event_type type)
{
    for (size_t i = 0; i < r->count; ++i) {
        if (r->events[i].type == type) return &r->events[i];
    }
    return NULL;
}

static void check_digest(const void *data, size_t len, const char *expected)
{
    uint8_t digest[32];
    char hex[65];
    sha256(data, len, digest);
    for (size_t i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    assert(strcmp(hex, expected) == 0);
}

static void test_sha(void)
{
    check_digest("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_digest("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *long_msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check_digest(long_msg, 56, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    sha256_ctx ctx;
    uint8_t digest[32], one_shot[32];
    char hex[65], chunk[1000];
    memset(chunk, 'a', sizeof(chunk));
    sha256_init(&ctx);
    for (size_t i = 0; i < 1000; ++i) sha256_update(&ctx, chunk, sizeof(chunk));
    sha256_final(&ctx, digest);
    for (size_t i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    assert(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    for (size_t len = 0; len <= sizeof(chunk); ++len) {
        sha256(chunk, len, one_shot);
        sha256_init(&ctx);
        for (size_t i = 0; i < len; ++i) sha256_update(&ctx, chunk + i, 1);
        sha256_final(&ctx, digest);
        assert(memcmp(digest, one_shot, 32) == 0);
    }
    puts("PASS SHA-256: four FIPS vectors and streaming boundaries");
}

static void test_door(void)
{
    alarm_state a = fresh();
    step(&a, 0, 9000, true);
    alarm_result r = step(&a, 3 * MINUTE, 5000, true);
    assert(r.actions == 0 && !a.active && !a.excursion);
    assert(event(&r, ALARM_RECOVERY)->duration_ms == 3 * MINUTE);
    step(&a, 4 * MINUTE, 9000, true);
    r = step(&a, 19 * MINUTE - 1, 9000, true);
    assert(r.actions == 0);
    r = step(&a, 19 * MINUTE, 9000, true);
    assert(r.actions == ALARM_LOCAL && a.level == 1);
    r = step(&a, 29 * MINUTE - 1, 9000, true);
    assert(r.actions == 0);
    r = step(&a, 29 * MINUTE, 9000, true);
    assert(r.actions == ALARM_CONTACT1 && a.level == 2);
    r = step(&a, 39 * MINUTE - 1, 9000, true);
    assert(r.actions == 0);
    r = step(&a, 39 * MINUTE, 9000, true);
    assert(r.actions == ALARM_CONTACT2 && a.level == 3);
    r = step(&a, 44 * MINUTE, 9000, true);
    assert(r.actions == 0);
    puts("PASS door: 3-minute grace, reset, 40-minute excursion and exact ladder deadlines");
}

static void test_ack(void)
{
    alarm_state a = fresh();
    alarm_result r;
    step(&a, 0, 9000, true);
    step(&a, 15 * MINUTE, 9000, true);
    step(&a, 25 * MINUTE, 9000, true);
    assert(alarm_ack(&a, 25 * MINUTE, "nurse-1", &r));
    assert(r.actions == ALARM_SILENCE && a.excursion && a.active);
    assert(strcmp(event(&r, ALARM_ACK)->who, "nurse-1") == 0);
    assert(!alarm_ack(&a, 26 * MINUTE, "nurse-2", &r));
    r = step(&a, 85 * MINUTE - 1, 9500, true);
    assert(r.actions == 0 && a.level == 2);
    r = step(&a, 85 * MINUTE, 9500, true);
    assert(r.actions == ALARM_LOCAL && event(&r, ALARM_REARM));
    r = step(&a, 95 * MINUTE, 9500, true);
    assert(r.actions == ALARM_CONTACT1);
    r = step(&a, 105 * MINUTE, 9500, true);
    assert(r.actions == ALARM_CONTACT2);
    r = step(&a, 106 * MINUTE, 5000, true);
    assert(!a.active && r.actions == ALARM_SILENCE);
    assert(!alarm_ack(&a, 107 * MINUTE, "nurse-1", &r));
    puts("PASS ack: stops escalation, preserves fault, re-arms after 60 minutes");
}

static void test_power_and_recovery(void)
{
    alarm_state a = fresh();
    for (uint64_t m = 0; m <= 30; ++m) {
        alarm_result r = step(&a, m * MINUTE, 5000 + (int32_t)m * 250, true);
        assert(r.actions == (m == 28 ? ALARM_LOCAL : 0u));
    }
    alarm_result r = step(&a, 31 * MINUTE, 7700, true);
    const alarm_event *e = event(&r, ALARM_RECOVERY);
    assert(e && e->duration_ms == 18 * MINUTE);
    assert(e->min_mc == 8250 && e->max_mc == 12500);
    assert(a.total_out_ms == 18 * MINUTE && !a.excursion);
    step(&a, 32 * MINUTE, 1000, true);
    r = step(&a, 34 * MINUTE, 2300, true);
    assert(event(&r, ALARM_RECOVERY)->duration_ms == 2 * MINUTE);
    assert(a.total_out_ms == 20 * MINUTE);
    puts("PASS power loss: slow rise, recovery duration/extrema and cumulative duration");
}

static void test_hysteresis(void)
{
    alarm_state a = fresh();
    step(&a, 0, 8100, true);
    for (uint64_t m = 1; m <= 30; ++m) {
        alarm_result r = step(&a, m * MINUTE, m % 2 ? 7900 : 8100, true);
        assert(!a.active && r.actions == 0);
    }
    a = fresh();
    step(&a, 0, 8100, true);
    step(&a, 15 * MINUTE, 8100, true);
    for (uint64_t m = 16; m < 24; ++m) {
        alarm_result r = step(&a, m * MINUTE, m % 2 ? 8100 : 7900, true);
        assert(a.excursion && a.active && !event(&r, ALARM_RECOVERY));
    }
    alarm_result r = step(&a, 24 * MINUTE, 7700, true);
    assert(!a.active && event(&r, ALARM_RECOVERY));
    step(&a, 25 * MINUTE, 1900, true);
    step(&a, 40 * MINUTE, 1900, true);
    step(&a, 41 * MINUTE, 2100, true);
    assert(a.excursion && a.active);
    r = step(&a, 42 * MINUTE, 2300, true);
    assert(!a.active && event(&r, ALARM_RECOVERY));
    puts("PASS hysteresis: upper/lower oscillation and inclusive recovery boundaries");
}

static void test_limits(void)
{
    /* 2 C and 8 C are inside the range: a fridge held at a limit is not an excursion. */
    alarm_config c = alarm_defaults();
    const int32_t inside[] = { c.low_mc, c.high_mc };
    const int32_t outside[] = { c.low_mc - 1, c.high_mc + 1 };
    for (size_t i = 0; i < 2; ++i) {
        alarm_state a = fresh();
        alarm_result r = step(&a, 0, inside[i], true);
        assert(!a.outside && !a.excursion && !event(&r, ALARM_EXCURSION));
        r = step(&a, 60 * MINUTE, inside[i], true);
        assert(!a.active && r.actions == 0);
        a = fresh();
        r = step(&a, 0, outside[i], true);
        assert(a.outside && a.excursion && event(&r, ALARM_EXCURSION));
    }
    puts("PASS limits: a reading at 2 C or 8 C is in range, one millidegree beyond is an excursion");
}

static void test_sensor(void)
{
    alarm_state a = fresh();
    alarm_result r = step(&a, 119999, INT32_MIN, false);
    assert(!a.active && r.actions == 0);
    r = step(&a, 120000, INT32_MIN, false);
    assert(a.sensor_alarm && r.actions == ALARM_LOCAL);
    assert(!event(&r, ALARM_SENSOR_FAILURE)->temperature_valid);
    r = step(&a, 121000, 5000, true);
    assert(!a.active && r.actions == ALARM_SILENCE);
    assert(event(&r, ALARM_SENSOR_RECOVERY));
    r = step(&a, 240999, 0, false);
    assert(r.actions == 0);
    r = step(&a, 241000, 0, false);
    assert(r.actions == ALARM_LOCAL);
    /* Temperature recovery must not occur on an invalid reading. */
    a = fresh();
    step(&a, 0, 9000, true);
    r = step(&a, 15 * MINUTE, 0, false);
    assert(a.sensor_alarm && a.temperature_alarm && a.excursion);
    assert(!event(&r, ALARM_RECOVERY));
    r = step(&a, 16 * MINUTE, 9500, true);
    assert(!a.sensor_alarm && a.temperature_alarm && a.active);
    assert(r.actions == 0 && event(&r, ALARM_SENSOR_RECOVERY));
    r = step(&a, 17 * MINUTE, 5000, true);
    assert(event(&r, ALARM_RECOVERY)->min_mc == 9000);
    assert(event(&r, ALARM_RECOVERY)->max_mc == 9500);
    assert(!a.active);
    puts("PASS sensor: startup timeout, last-valid deadline, overlapping faults and invalid samples");
}

static void test_edges(void)
{
    alarm_state a = fresh(), saved;
    alarm_result r;
    alarm_config c = alarm_defaults();
    saved = a;
    c.hysteresis_mc = 4000;
    assert(!alarm_init(&a, &c, 0));
    assert(memcmp(&a, &saved, sizeof(a)) == 0);
    c = alarm_defaults(); c.esc_s = 0;
    assert(!alarm_init(&a, &c, 0));
    step(&a, 100, 9000, true);
    saved = a;
    assert(!alarm_step(&a, 99, 0, false, &r));
    assert(memcmp(&a, &saved, sizeof(a)) == 0);
    step(&a, 15 * MINUTE + 100, 9000, true);
    r = step(&a, 40 * MINUTE + 100, 9000, true);
    assert(r.actions == (ALARM_CONTACT1 | ALARM_CONTACT2));
    assert(r.count == 2);
    saved = a;
    assert(!alarm_ack(&a, 0, "nurse", &r));
    assert(!alarm_ack(&a, a.now_ms, "", &r));
    assert(!alarm_ack(&a, a.now_ms, "01234567890123456789012345678901XX", &r));
    assert(memcmp(&a, &saved, sizeof(a)) == 0);
    c = alarm_defaults();
    c.low_mc = INT32_MIN; c.high_mc = INT32_MAX;
    assert(alarm_init(&a, &c, UINT64_MAX - 100));
    r = step(&a, UINT64_MAX, 0, false);
    assert(r.actions == 0);
    c = alarm_defaults(); c.grace_s = 1; c.sensor_s = 2; c.esc_s = 3; c.re_alarm_s = 4;
    c.low_mc = -1000; c.high_mc = 1000; c.hysteresis_mc = 100;
    assert(alarm_init(&a, &c, 0));
    step(&a, 0, -2000, true);
    r = step(&a, 1000, -2000, true); assert(r.actions == ALARM_LOCAL);
    r = step(&a, 4000, -2000, true); assert(r.actions == ALARM_CONTACT1);
    assert(alarm_ack(&a, 4000, "nurse", &r));
    r = step(&a, 8000, -2000, true); assert(r.actions == ALARM_LOCAL);
    r = step(&a, 9000, -900, true); assert(!a.active && event(&r, ALARM_RECOVERY));
    puts("PASS edges: invalid inputs unchanged, late ladder catch-up, custom config and uint64 clock");
}

static void test_audit(void)
{
    audit_chain chain, verified;
    audit_record records[4], changed[4];
    size_t bad;
    audit_init(&chain);
    assert(audit_append(&chain, &records[0], 0, ALARM_EXCURSION, -1500, "excursion"));
    /* Independent canonical-format reference generated with Python hashlib. */
    const uint8_t expected_hash[32] = {0x1c, 0x53, 0x5d, 0x52, 0xa9, 0x9e, 0x77, 0x64, 0xa4, 0x88, 0x24, 0x3f, 0x37, 0x2a, 0xf7, 0xd7, 0x6d, 0x22, 0x22, 0xc4, 0x84, 0x9f, 0xb6, 0x70, 0x79, 0xb3, 0x51, 0xbc, 0x0f, 0x00, 0x21, 0x78};
    assert(memcmp(records[0].bytes + AUDIT_HASH_OFFSET, expected_hash, 32) == 0);
    assert(audit_append(&chain, &records[1], 100, ALARM_TEMPERATURE, 9000, "local"));
    assert(audit_verify(records, 2 * AUDIT_RECORD_SIZE, &bad, &verified) == AUDIT_OK);
    chain = verified;
    assert(audit_append(&chain, &records[2], 200, AUDIT_GAP, 0, "process restarted; interval unknown"));
    alarm_state a = fresh();
    step(&a, 0, 9000, true);
    alarm_result r = step(&a, 3 * MINUTE, 5000, true);
    assert(audit_append_event(&chain, &records[3], event(&r, ALARM_RECOVERY)));
    assert(audit_verify(records, sizeof(records), &bad, &verified) == AUDIT_OK && bad == 4);
    assert(verified.seq == 4 && memcmp(verified.hash, chain.hash, 32) == 0);
    assert(records[0].bytes[7] == 1 && records[0].bytes[20] == 0xff);
    assert(strstr((char *)records[3].bytes + 24, "duration_ms=180000") != NULL);
    assert(strstr((char *)records[3].bytes + 24, "min_mc=9000 max_mc=9000") != NULL);
    for (size_t byte = 0; byte < AUDIT_RECORD_SIZE; ++byte) {
        memcpy(changed, records, sizeof(records));
        changed[1].bytes[byte] ^= 1;
        assert(audit_verify(changed, sizeof(changed), &bad, NULL) == AUDIT_BROKEN && bad == 1);
    }
    memcpy(changed, records, sizeof(records));
    changed[1] = records[2]; changed[2] = records[3];
    assert(audit_verify(changed, 3 * AUDIT_RECORD_SIZE, &bad, NULL) == AUDIT_BROKEN && bad == 1);
    assert(audit_verify(records + 1, 3 * AUDIT_RECORD_SIZE, &bad, NULL) == AUDIT_BROKEN && bad == 0);
    assert(audit_verify(records, sizeof(records) - 1, &bad, NULL) == AUDIT_BROKEN && bad == 3);
    assert(audit_verify(NULL, 0, &bad, NULL) == AUDIT_OK && bad == 0);
    FILE *f = tmpfile(); assert(f);
    for (size_t i = 0; i < 4; ++i) assert(audit_write(f, &records[i]));
    rewind(f);
    assert(audit_verify_file(f, &bad, &verified) == AUDIT_OK && bad == 4);
    assert(memcmp(verified.hash, chain.hash, 32) == 0);
    assert(fseek(f, AUDIT_RECORD_SIZE + 24, SEEK_SET) == 0);
    assert(fputc('X', f) != EOF);
    rewind(f);
    assert(audit_verify_file(f, &bad, NULL) == AUDIT_BROKEN && bad == 1);
    assert(fclose(f) == 0);
    f = tmpfile(); assert(f);
    assert(audit_write(f, &records[0])); assert(fputc(0, f) != EOF); rewind(f);
    assert(audit_verify_file(f, &bad, NULL) == AUDIT_BROKEN && bad == 1);
    assert(fclose(f) == 0);
    char long_detail[AUDIT_DETAIL_SIZE + 1]; memset(long_detail, 'x', sizeof(long_detail));
    long_detail[AUDIT_DETAIL_SIZE] = '\0';
    audit_chain before = chain;
    assert(!audit_append(&chain, &records[0], 1, AUDIT_GAP, 0, long_detail));
    assert(chain.seq == before.seq && memcmp(chain.hash, before.hash, 32) == 0);
    alarm_event extreme = { .t_ms = UINT64_MAX, .duration_ms = UINT64_MAX,
        .type = ALARM_ACK, .temperature_mc = INT32_MIN, .min_mc = INT32_MIN,
        .max_mc = INT32_MAX, .level = 3, .temperature_valid = true,
        .who = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" };
    assert(audit_append_event(&chain, &records[0], &extreme));
    assert(strstr((char *)records[0].bytes + 24, extreme.who));
    chain.seq = UINT64_MAX;
    assert(!audit_append(&chain, &records[0], 1, AUDIT_GAP, 0, "overflow"));
    puts("PASS audit: canonical records, recovery data, resume/gap, every-byte tamper, deletion and file verification");
}

int main(void)
{
    test_sha(); test_door(); test_ack(); test_power_and_recovery();
    test_hysteresis(); test_limits(); test_sensor(); test_edges(); test_audit();
    puts("All core tests passed.");
    return 0;
}
