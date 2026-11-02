#ifndef CCG_STORAGE_H
#define CCG_STORAGE_H
#include "audit.h"
/* M4 record type; same wire format as M1. Invalid samples carry temp=0. */
#define AUDIT_SAMPLE 10u
/* POSIX adapter. Whole-file advisory write lock held until fclose. Existing
 * complete records are verified before any modification; only a partial tail
 * may be removed. A successful resume synchronizes one explicit unknown gap.
 * Returns NULL on corruption, lock contention or any I/O/durability error. */
FILE *fridge_log_open(const char *path, audit_chain *chain);
bool fridge_log_sync(FILE *file);
bool fridge_log_sample(FILE *file, audit_chain *chain, uint64_t elapsed,
                       int32_t temperature, bool valid);
#endif
