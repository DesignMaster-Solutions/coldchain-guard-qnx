/* QNX test load: one pinned FIFO worker per online CPU, no worker I/O. */
#include <sys/neutrino.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "portable.h"

static atomic_uint ready;
static void *busy(void *arg)
{
    unsigned cpu = (unsigned)(uintptr_t)arg;
    if (ThreadCtl(_NTO_TCTL_RUNMASK,(void *)(uintptr_t)(1u << cpu)) == -1) _Exit(1);
    atomic_fetch_add(&ready,1);
    volatile unsigned work = 0;
    for (;;) work = work * 1664525u + 1013904223u;
    return NULL;
}
int main(int argc, char **argv)
{
    unsigned priority;
    if (argc != 2 || !fridge_uint(argv[1],1,63,&priority)) return 1;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    /* Simple runmask API is intentionally bounded; never silently omit a CPU. */
    if (cpus < 1 || cpus > 32) return 1;
    pthread_attr_t attr;
    struct sched_param param = { .sched_priority = (int)priority };
    if (pthread_attr_init(&attr) || pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED) ||
        pthread_attr_setschedpolicy(&attr,SCHED_FIFO) || pthread_attr_setschedparam(&attr,&param)) return 1;
    for (long cpu = 0; cpu < cpus; ++cpu) {
        pthread_t thread;
        if (pthread_create(&thread,&attr,busy,(void *)(uintptr_t)cpu)) return 1;
    }
    pthread_attr_destroy(&attr);
    struct timespec delay = {0,10000000};
    while (atomic_load(&ready) != (unsigned)cpus) nanosleep(&delay,NULL);
    /* stdout is a private readiness pipe, not a performance report. */
    if (write(STDOUT_FILENO,"R",1) != 1) return 1;
    for (;;) pause();
}
