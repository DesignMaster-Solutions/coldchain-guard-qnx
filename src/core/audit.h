#ifndef CCG_AUDIT_H
#define CCG_AUDIT_H

#include <stdio.h>
#include "alarm.h"

#define AUDIT_FORECAST 11u /* Advisory category transition, never an alarm. */
#define AUDIT_DETAIL_SIZE 128
#define AUDIT_RECORD_SIZE 216
#define AUDIT_PREV_OFFSET 152
#define AUDIT_HASH_OFFSET 184

/* Canonical wire format: seq u64, t_ms u64, type u32, temperature i32
 * (two's complement), detail[128] zero padded, prev_hash[32], hash[32].
 * Integers are big endian. SHA-256 covers bytes [0,184), including prev_hash.
 * No native struct padding is hashed or written. Schema changes need a new
 * format. Sequence starts at 1 with an all-zero previous hash.
 * This detects corruption, not an attacker rewriting the whole unanchored
 * chain. A trusted final seq/hash is needed to detect tail truncation. */
typedef struct { uint8_t bytes[AUDIT_RECORD_SIZE]; } audit_record;
typedef struct { uint64_t seq; uint8_t hash[32]; } audit_chain;

enum audit_status { AUDIT_OK, AUDIT_BROKEN, AUDIT_IO_ERROR };

void audit_init(audit_chain *chain);
/* Incremental verification from an initialized chain. Failure leaves it unchanged. */
bool audit_verify_next(audit_chain *chain, const uint8_t record[AUDIT_RECORD_SIZE]);
bool audit_append(audit_chain *chain, audit_record *record, uint64_t t_ms,
                  uint32_t type, int32_t temperature_mc, const char *detail);
bool audit_append_event(audit_chain *chain, audit_record *record,
                        const alarm_event *event);
/* Verify from genesis. bad_index is zero-based; on success it equals count.
 * Resume after restart only from a fully verified chain, then append AUDIT_GAP.
 * Buffers/files must contain whole records. final receives verified state
 * only on success. NULL final is allowed. File verification starts at current
 * position and reads to EOF; I/O errors are distinct from corruption. */
enum audit_status audit_verify(const void *buffer, size_t bytes,
                               size_t *bad_index, audit_chain *final);
enum audit_status audit_verify_file(FILE *file, size_t *bad_index,
                                    audit_chain *final);
/* Storage is caller-owned: open in append mode and handle durability/locking
 * outside this module. Writing does not claim fsync or transactional append. */
bool audit_write(FILE *file, const audit_record *record);

#endif
