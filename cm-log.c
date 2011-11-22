#include <stdlib.h>
#include <malloc.h>

#include "coremu-config.h"

#define CONFIG_REPLAY
#include "coremu-debug.h"

#include "cm-log.h"

static const char *cm_log_name[] = {
    "intr", "pc", "in", "rdtsc", "mmio", "dma", "crewinc", "allpc", "tlbflush",
    "gencode", "read", "write", "tlbfill", "tbflush"
};

log_t **cm_log;
#ifdef REPLAY_LOGBUF
CMLogBuf ***cm_log_buf;
#endif

#define LOG_BUFSIZE 1024

#define MAXLOGLEN 256

#define LOGDIR "replay-log/"
static FILE *open_log1(const char* logname, const char *mode, int coreid)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, LOGDIR"%s-%d", logname, coreid);

    FILE *log = fopen(logpath, mode);
    if (!(log)) {
        printf("Can't open log file %s\n", logpath);
        exit(1);
    }
    return log;
}

static void cm_open_log(const char *mode, int coreid)
{
    int i;
    for (i = 0; i < N_CM_LOG; i++) {
        cm_log[coreid][i] = open_log1(cm_log_name[i], mode, coreid);
#ifdef REPLAY_LOGBUF
        cm_log_buf[coreid][i] =
            coremu_logbuf_new(LOG_BUFSIZE, cm_log[coreid][i]);
#endif
    }
}

void cm_replay_flush_log(int coreid) {
    int i;
    for (i = 0; i < N_CM_LOG; i++) {
#ifdef REPLAY_LOGBUF
        coremu_logbuf_flush(cm_log_buf[coreid][i]);
#else
        fflush(cm_log[coreid][i]);
#endif
    }
}

extern int cm_run_mode;

static void cm_debug_open_log(int coreid)
{
    /*
     *if (cm_run_mode == 1)
     *    open_log1(&(cm_log[coreid][MEMREC]), "memrec", "w", coreid);
     *else {
     *    open_log1(&(cm_log[coreid][MEMPLAY]), "memplay", "w", coreid);
     *    open_log1(&(cm_log[coreid][MEMREC]), "memrec", "r", coreid);
     *}
     */
}

extern int smp_cpus;

void cm_log_init(const char *mode)
{
    cm_log = calloc(smp_cpus, sizeof(FILE **));
    assert(cm_log);
#ifdef REPLAY_LOGBUF
    cm_log_buf = calloc(smp_cpus, sizeof(*cm_log_buf));
    assert(cm_log_buf);
#endif
    int i;
    for (i = 0; i < smp_cpus; i++) {
        cm_log[i] = calloc(N_CM_LOG, sizeof(FILE *));
        assert(cm_log[i]);
#ifdef REPLAY_LOGBUF
        cm_log_buf[i] = calloc(N_CM_LOG, sizeof(*cm_log_buf[0]));
        assert(cm_log_buf[i]);
#endif

        cm_open_log(mode, i);
        cm_debug_open_log(i);
    }
}

