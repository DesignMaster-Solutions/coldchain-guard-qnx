#include "audit.h"
#include "sha256.h"

#include <inttypes.h>
#include <string.h>

_Static_assert(sizeof(audit_record) == AUDIT_RECORD_SIZE, "wire size mismatch");

static void put_be(uint8_t *dst, uint64_t value, size_t n)
{
    for (size_t i = 0; i < n; ++i) dst[n - i - 1] = (uint8_t)(value >> (8 * i));
}

static uint64_t get_be(const uint8_t *src, size_t n)
{
    uint64_t value = 0;
    for (size_t i = 0; i < n; ++i) value = (value << 8) | src[i];
    return value;
}

void audit_init(audit_chain *chain)
{
    *chain = (audit_chain){0};
}

bool audit_append(audit_chain *chain, audit_record *record, uint64_t t_ms,
                  uint32_t type, int32_t temperature_mc, const char *detail)
{
    if (!chain || !record || !detail || chain->seq == UINT64_MAX) return false;
    size_t len = 0;
    while (len < AUDIT_DETAIL_SIZE && detail[len]) ++len;
    if (len == AUDIT_DETAIL_SIZE) return false;
    *record = (audit_record){0};
    put_be(record->bytes, chain->seq + 1, 8);
    put_be(record->bytes + 8, t_ms, 8);
    put_be(record->bytes + 16, type, 4);
    put_be(record->bytes + 20, (uint32_t)temperature_mc, 4);
    memcpy(record->bytes + 24, detail, len);
    memcpy(record->bytes + AUDIT_PREV_OFFSET, chain->hash, 32);
    sha256(record->bytes, AUDIT_HASH_OFFSET, record->bytes + AUDIT_HASH_OFFSET);
    ++chain->seq;
    memcpy(chain->hash, record->bytes + AUDIT_HASH_OFFSET, 32);
    return true;
}

bool audit_append_event(audit_chain *chain, audit_record *record, const alarm_event *e)
{
    if (!e || !memchr(e->who, '\0', ALARM_WHO_SIZE)) return false;
    char detail[AUDIT_DETAIL_SIZE];
    int n = snprintf(detail, sizeof(detail),
                     "valid=%u level=%u duration_ms=%" PRIu64 " min_mc=%" PRId32
                     " max_mc=%" PRId32 " who=%s",
                     (unsigned)e->temperature_valid, e->level, e->duration_ms,
                     e->min_mc, e->max_mc, e->who);
    if (n < 0 || (size_t)n >= sizeof(detail)) return false;
    return audit_append(chain, record, e->t_ms, (uint32_t)e->type, e->temperature_mc, detail);
}

static bool verify_record(audit_chain *chain, const uint8_t *record)
{
    uint8_t digest[32];
    if (chain->seq == UINT64_MAX || get_be(record, 8) != chain->seq + 1 ||
        memcmp(record + AUDIT_PREV_OFFSET, chain->hash, 32)) return false;
    sha256(record, AUDIT_HASH_OFFSET, digest);
    if (memcmp(record + AUDIT_HASH_OFFSET, digest, 32)) return false;
    ++chain->seq;
    memcpy(chain->hash, digest, 32);
    return true;
}

enum audit_status audit_verify(const void *buffer, size_t bytes, size_t *bad_index, audit_chain *final)
{
    audit_chain chain;
    audit_init(&chain);
    *bad_index = 0;
    if (!buffer && bytes) return AUDIT_BROKEN;
    const uint8_t *p = buffer;
    while (bytes >= AUDIT_RECORD_SIZE) {
        if (!verify_record(&chain, p)) return AUDIT_BROKEN;
        ++*bad_index;
        p += AUDIT_RECORD_SIZE;
        bytes -= AUDIT_RECORD_SIZE;
    }
    if (bytes) return AUDIT_BROKEN;
    if (final) *final = chain;
    return AUDIT_OK;
}

enum audit_status audit_verify_file(FILE *file, size_t *bad_index, audit_chain *final)
{
    audit_chain chain;
    audit_record record;
    audit_init(&chain);
    *bad_index = 0;
    if (!file) return AUDIT_IO_ERROR;
    for (;;) {
        size_t n = fread(record.bytes, 1, AUDIT_RECORD_SIZE, file);
        if (ferror(file)) return AUDIT_IO_ERROR;
        if (n == 0) break;
        if (n != AUDIT_RECORD_SIZE || !verify_record(&chain, record.bytes)) return AUDIT_BROKEN;
        ++*bad_index;
    }
    if (final) *final = chain;
    return AUDIT_OK;
}

bool audit_write(FILE *file, const audit_record *record)
{
    return file && record && fwrite(record->bytes, 1, AUDIT_RECORD_SIZE, file) == AUDIT_RECORD_SIZE;
}
