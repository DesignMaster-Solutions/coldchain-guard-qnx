/* Application deadline acceptance test, never a QNX benchmark. No measured times printed. */
#include <sys/wait.h>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "audit.h"

#define HOG_PRIORITY "10"
#define SAMPLER_PRIORITY "20"
#define PERIOD_MS 100
#define GRACE_S 1
#define ESC_S 1
#define CROSS_MS 100
#define MARGIN_MS 2000u
#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)

extern char **environ;
/* Set the child's INITIAL scheduling explicitly, including loader/startup.
 * fridged then applies its requested sampler priority before any samples. */
static pid_t launch(const char *path, char *const args[], int read_fd, int write_fd)
{
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t actions;
    if (posix_spawnattr_init(&attr)) return -1;
    if (posix_spawn_file_actions_init(&actions)) { posix_spawnattr_destroy(&attr); return -1; }
    struct sched_param param = { .sched_priority = 40 };
    int error = posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETSCHEDULER);
    if (!error) error = posix_spawnattr_setschedpolicy(&attr,SCHED_FIFO);
    if (!error) error = posix_spawnattr_setschedparam(&attr,&param);
    if (!error) error = posix_spawn_file_actions_addclose(&actions,read_fd);
    if (!error && write_fd >= 0) error = posix_spawn_file_actions_adddup2(&actions,write_fd,STDOUT_FILENO);
    if (!error && write_fd >= 0) error = posix_spawn_file_actions_addclose(&actions,write_fd);
    pid_t pid = -1;
    if (!error) error = posix_spawn(&pid,path,&actions,&attr,args,environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    return error ? -1 : pid;
}

static uint64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) exit(1);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void nap(void)
{
    struct timespec t = {0,10000000};
    nanosleep(&t,NULL);
}
static bool reaped(pid_t pid)
{
    pid_t result = waitpid(pid,NULL,WNOHANG);
    return result == pid || (result == -1 && errno == ECHILD);
}
static bool cleanup(pid_t fridge, pid_t hog)
{
    /* Signal both before waiting: an exiting child must not wait for CPU
     * behind load that its observer has not stopped yet. Bound reaping too. */
    if (hog > 0) kill(hog,SIGKILL);
    if (fridge > 0) kill(fridge,SIGKILL);
    uint64_t deadline = now_ms() + 5000;
    while (hog > 0 || fridge > 0) {
        if (hog > 0 && reaped(hog)) hog = -1;
        if (fridge > 0 && reaped(fridge)) fridge = -1;
        if (now_ms() > deadline) return false;
        nap();
    }
    return true;
}
static bool alive(pid_t pid)
{
    return waitpid(pid,NULL,WNOHANG) == 0;
}
/* Read only complete records. Never call the device: that could donate priority
 * or block the observer behind the deliberately starved mutant. */
static bool events(bool *local, bool *escalated)
{
    FILE *f = fopen("/tmp/m3-audit.bin","rb");
    if (!f) return true;
    audit_record records[32];
    size_t n = fread(records,sizeof(*records),32,f), bad;
    bool ok = !ferror(f) && n < 32 && audit_verify(records,n*sizeof(*records),&bad,NULL) == AUDIT_OK;
    fclose(f);
    if (!ok) return false;
    *local = false; *escalated = false;
    bool temperature = false;
    for (size_t i = 0; i < n; ++i) {
        if (records[i].bytes[19] == ALARM_TEMPERATURE) temperature = true;
        if (temperature && records[i].bytes[19] == ALARM_ESCALATION &&
            strstr((char *)records[i].bytes+24,"level=1 ")) *local = true;
        if (records[i].bytes[19] == ALARM_ESCALATION &&
            strstr((char *)records[i].bytes+24,"level=2 ")) *escalated = true;
    }
    return true;
}
int main(void)
{
    /* Above BOTH normal and mutant workers, so failure and cleanup remain runnable. */
    struct sched_param param = { .sched_priority = 40 };
    if (pthread_setschedparam(pthread_self(),SCHED_FIFO,&param)) return 1;
    int pipefd[2];
    if (pipe(pipefd)) return 1;
    char *hog_args[] = {"coldchain_hog",HOG_PRIORITY,NULL};
    pid_t hog = launch("/system/bin/coldchain_hog",hog_args,pipefd[0],pipefd[1]);
    close(pipefd[1]);
    if (hog < 0) { close(pipefd[0]); return 1; }
    pid_t fridge = -1;
    bool passed = false;
    if (fcntl(pipefd[0],F_SETFL,O_NONBLOCK) == -1) goto done;
    uint64_t ready_deadline = now_ms() + 5000;
    char ready = 0;
    while (read(pipefd[0],&ready,1) != 1) {
        if (!alive(hog) || now_ms() > ready_deadline) { puts("coldchain: load readiness FAILED"); goto done; }
        nap();
    }
    if (ready != 'R') goto done;
    /* Common origin before spawn includes startup; margin is a declared test
     * allowance, not a measured platform latency. CSV crosses the upper limit at CROSS_MS. */
    uint64_t origin = now_ms();
    uint64_t local_deadline = origin + CROSS_MS + GRACE_S*1000 + 2*PERIOD_MS + MARGIN_MS;
    uint64_t next_deadline = local_deadline + ESC_S*1000 + PERIOD_MS + MARGIN_MS;
    char *fridge_args[] = {"fridged","-s","/system/etc/m3-scenario.csv",
        "-l","/tmp/m3-audit.bin","-P",SAMPLER_PRIORITY,
        "-p",STR(PERIOD_MS),"-g",STR(GRACE_S),"-e",STR(ESC_S),NULL};
    fridge = launch("/system/bin/fridged",fridge_args,pipefd[0],-1);
    if (fridge < 0) goto done;
    bool saw_local = false;
    for (;;) {
        bool local = false, escalated = false;
        if (!alive(hog) || !alive(fridge)) { puts("coldchain: child liveness FAILED"); goto done; }
        if (!events(&local,&escalated)) { puts("coldchain: audit validation FAILED"); goto done; }
        uint64_t observed = now_ms();
        if (!saw_local) {
            if (observed > local_deadline) { puts("coldchain: local alarm deadline FAILED"); goto done; }
            if (local) { saw_local = true; puts("coldchain: local alarm deadline PASS"); }
        }
        if (observed > next_deadline) { puts("coldchain: escalation deadline FAILED"); goto done; }
        if (saw_local && escalated) { puts("coldchain: escalation deadline PASS"); passed = true; break; }
        nap();
    }
done:
    if (!cleanup(fridge,hog)) { puts("coldchain: cleanup FAILED"); passed = false; }
    close(pipefd[0]);
    puts(passed ? "coldchain: deadline checks PASS" : "coldchain: deadline checks FAILED");
    return passed ? 0 : 1;
}
