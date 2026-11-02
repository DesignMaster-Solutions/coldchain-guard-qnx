/* POSIX storage lives outside the pure C11 core. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "storage.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

bool fridge_log_sync(FILE *file)
{
    return fflush(file) == 0 && fsync(fileno(file)) == 0;
}

FILE *fridge_log_open(const char *path, audit_chain *chain)
{
    bool existing = false;
    int fd = open(path,O_RDWR|O_APPEND|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if (fd < 0 && errno == EEXIST) {
        existing = true;
        fd = open(path,O_RDWR|O_APPEND|O_NOFOLLOW);
    }
    if (fd < 0) return NULL;
    struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    struct stat st;
    FILE *file = NULL;
    if (fcntl(fd,F_SETLK,&lock) < 0 || fstat(fd,&st) < 0) goto fail;
    if (!S_ISREG(st.st_mode) || st.st_size < 0) { errno = EINVAL; goto fail; }
    file = fdopen(fd,"a+b");
    if (!file) goto fail;
    audit_chain verified;
    audit_init(&verified);
    if (fseeko(file,0,SEEK_SET)) goto fail;
    off_t complete = st.st_size / AUDIT_RECORD_SIZE;
    size_t tail = (size_t)(st.st_size % AUDIT_RECORD_SIZE);
    for (off_t i = 0; i < complete; ++i) {
        audit_record record;
        if (fread(record.bytes,1,AUDIT_RECORD_SIZE,file) != AUDIT_RECORD_SIZE) goto fail;
        if (!audit_verify_next(&verified,record.bytes)) { errno = EILSEQ; goto fail; }
    }
    if (existing) {
        audit_record gap;
        char detail[AUDIT_DETAIL_SIZE];
        snprintf(detail,sizeof(detail),
                 "interval=unknown valid=0 discarded_bytes=%zu clock=process-relative state=reset",tail);
        if (!audit_append(&verified,&gap,0,AUDIT_GAP,0,detail)) { errno = EOVERFLOW; goto fail; }
        /* Verify EVERYTHING complete before changing even one byte. */
        if (tail && ftruncate(fd,st.st_size-(off_t)tail)) goto fail;
        if (fseeko(file,0,SEEK_END) || !audit_write(file,&gap) || !fridge_log_sync(file)) goto fail;
    }
    *chain = verified;
    return file;
fail:
    {
        int saved = errno ? errno : EIO;
        if (file) fclose(file); else close(fd);
        errno = saved;
        return NULL;
    }
}

bool fridge_log_sample(FILE *file, audit_chain *chain, uint64_t elapsed,
                       int32_t temperature, bool valid)
{
    audit_record record;
    return audit_append(chain,&record,elapsed,AUDIT_SAMPLE,valid ? temperature : 0,
                        valid ? "valid=1" : "valid=0") &&
           audit_write(file,&record) && fridge_log_sync(file);
}
