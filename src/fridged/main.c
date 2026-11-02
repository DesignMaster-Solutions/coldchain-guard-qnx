/* QNX-only adapter. One dispatch thread serializes reads, writes and samples. */
struct fridge_ocb;
#define IOFUNC_OCB_T struct fridge_ocb
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include "portable.h"
#include "audit.h"
#include "storage.h"

struct fridge_ocb { iofunc_ocb_t base; char text[FRIDGE_TEXT_SIZE]; size_t length; bool snapshot; };
static alarm_state state;
static scenario_row rows[SCENARIO_CAPACITY];
static size_t row_count;
static FILE *log_file;
static audit_chain chain;
static uint64_t start_ms;

static void fatal(const char *what) { perror(what); exit(EXIT_FAILURE); }
static uint64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) == -1) fatal("clock_gettime");
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void sample(void)
{
    uint64_t elapsed = now_ms() - start_ms;
    scenario_row row = scenario_sample(rows,row_count,elapsed);
    alarm_result result;
    if (!alarm_step(&state,elapsed,row.temp_mc,row.valid,&result)) fatal("alarm_step");
    for (size_t i = 0; i < result.count; ++i) {
        audit_record record;
        if (!audit_append_event(&chain,&record,&result.events[i]) || !audit_write(log_file,&record)) fatal("audit append");
    }
    if (!fridge_log_sample(log_file,&chain,elapsed,row.temp_mc,row.valid)) fatal("audit sample/sync");
}
static int pulse(message_context_t *ctp, int code, unsigned flags, void *handle)
{
    (void)ctp; (void)code; (void)flags; (void)handle;
    sample(); return 0;
}
static struct fridge_ocb *ocb_alloc(resmgr_context_t *ctp, iofunc_attr_t *attr)
{
    (void)ctp; (void)attr; return calloc(1,sizeof(struct fridge_ocb));
}
static void ocb_free(struct fridge_ocb *ocb) { free(ocb); }
static int fridge_read(resmgr_context_t *ctp, io_read_t *msg, struct fridge_ocb *ocb)
{
    int status = iofunc_read_verify(ctp,msg,&ocb->base,NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;
    if (!ocb->snapshot) {
        int n = fridge_format(ocb->text,sizeof(ocb->text),&state);
        if (n < 0) return EIO;
        ocb->length = (size_t)n; ocb->snapshot = true;
    }
    if (ocb->base.offset < 0) return EINVAL;
    size_t offset = (size_t)ocb->base.offset;
    size_t n = offset < ocb->length ? ocb->length - offset : 0;
    if (n > _IO_READ_GET_NBYTES(msg)) n = _IO_READ_GET_NBYTES(msg);
    _IO_SET_READ_NBYTES(ctp,n);
    if (!n) return _RESMGR_NPARTS(0);
    SETIOV(ctp->iov,ocb->text + offset,n);
    ocb->base.offset += n;
    return _RESMGR_NPARTS(1);
}
static int fridge_write(resmgr_context_t *ctp, io_write_t *msg, struct fridge_ocb *ocb)
{
    int status = iofunc_write_verify(ctp,msg,&ocb->base,NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;
    size_t n = _IO_WRITE_GET_NBYTES(msg);
    if (!n || n >= 128) return EINVAL;
    if ((size_t)ctp->info.srcmsglen < (size_t)ctp->offset + sizeof(msg->i) + n) return EBADMSG;
    char buf[128];
    if (resmgr_msgget(ctp,buf,n,sizeof(msg->i)) != (int)n) return EIO;
    if (!fridge_command(buf,n,&state.config)) return EINVAL;
    ocb->base.attr->flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME;
    _IO_SET_WRITE_NBYTES(ctp,n);
    return _RESMGR_NPARTS(0);
}
int main(int argc, char **argv)
{
    const char *scenario = NULL, *log_path = NULL; unsigned period = 1000, priority = FRIDGE_PRIORITY_DEFAULT;
    alarm_config config = alarm_defaults();
    int opt;
    while ((opt = getopt(argc,argv,"s:p:l:P:g:e:")) != -1) {
        if (opt == 's') scenario = optarg;
        else if (opt == 'l') log_path = optarg;
        else if (opt == 'p') {
            if (!fridge_uint(optarg,1,3600000,&period)) return EXIT_FAILURE;
        } else if (opt == 'P') {
            if (!fridge_uint(optarg,FRIDGE_PRIORITY_MIN,FRIDGE_PRIORITY_MAX,&priority)) return EXIT_FAILURE;
        } else if (opt == 'g' || opt == 'e') {
            unsigned seconds;
            if (!fridge_uint(optarg,1,86400,&seconds)) return EXIT_FAILURE;
            if (opt == 'g') config.grace_s = seconds;
            else config.esc_s = seconds;
        } else return EXIT_FAILURE;
    }
    if (!scenario || !log_path || optind != argc) {
        fprintf(stderr,"usage: fridged -s scenario.csv -p period_ms -l audit.bin [-P priority] [-g grace_s] [-e escalation_s]\n"); return EXIT_FAILURE;
    }
    struct sched_param scheduling = { .sched_priority = (int)priority };
    int error = pthread_setschedparam(pthread_self(),SCHED_FIFO,&scheduling);
    if (error) { errno = error; fatal("sampler scheduling"); }
    FILE *f = fopen(scenario,"r"); if (!f) fatal("scenario open");
    bool loaded = scenario_load(f,rows,SCENARIO_CAPACITY,&row_count);
    if (fclose(f) || !loaded) { fprintf(stderr,"invalid scenario\n"); return EXIT_FAILURE; }
    log_file = fridge_log_open(log_path,&chain);
    if (!log_file) fatal("audit open/verify/resume");
    start_ms = now_ms();
    if (!alarm_init(&state,&config,0)) return EXIT_FAILURE;
    sample();
    int chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY | _NTO_CHF_UNBLOCK | _NTO_CHF_DISCONNECT);
    if (chid == -1) fatal("ChannelCreate");
    dispatch_t *dpp = dispatch_create_channel(chid,0); if (!dpp) fatal("dispatch_create");
    resmgr_connect_funcs_t connect_funcs; resmgr_io_funcs_t io_funcs;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS,&connect_funcs,_RESMGR_IO_NFUNCS,&io_funcs);
    io_funcs.read = fridge_read; io_funcs.read64 = fridge_read;
    io_funcs.write = fridge_write; io_funcs.write64 = fridge_write;
    iofunc_attr_t attr; iofunc_attr_init(&attr,S_IFCHR|0644,NULL,NULL);
    iofunc_funcs_t funcs = { .nfuncs = _IOFUNC_NFUNCS, .ocb_calloc = ocb_alloc, .ocb_free = ocb_free };
    iofunc_mount_t mount;
    iofunc_mount_init(&mount,sizeof(mount)); mount.funcs = &funcs; attr.mount = &mount;
    resmgr_attr_t ra = { .nparts_max = 1, .msg_max_size = 2048 };
    if (resmgr_attach(dpp,&ra,"/dev/fridge0",_FTYPE_ANY,0,&connect_funcs,&io_funcs,&attr) == -1) fatal("resmgr_attach");
    int code = pulse_attach(dpp,MSG_FLAG_ALLOC_PULSE,0,pulse,NULL);
    if (code == -1) fatal("pulse_attach");
    int coid = message_connect(dpp,MSG_FLAG_SIDE_CHANNEL); if (coid == -1) fatal("message_connect");
    struct sigevent event;
    SIGEV_PULSE_INIT(&event,coid,(int)priority,code,0);
    timer_t timer;
    if (timer_create(CLOCK_MONOTONIC,&event,&timer) == -1) fatal("timer_create");
    struct itimerspec interval = { .it_value = {period / 1000,(period % 1000) * 1000000} };
    interval.it_interval = interval.it_value;
    if (timer_settime(timer,0,&interval,NULL)) fatal("timer_settime");
    dispatch_context_t *ctp = dispatch_context_alloc(dpp); if (!ctp) fatal("dispatch_context_alloc");
    for (;;) {
        ctp = dispatch_block(ctp);
        if (!ctp) fatal("dispatch_block");
        dispatch_handler(ctp);
    }
}
