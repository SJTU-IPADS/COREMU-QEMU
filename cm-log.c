#include <stdlib.h>

#define CONFIG_REPLAY
#include "coremu-debug.h"

#include "cm-log.h"

static const char *cm_log_name[] = {
    "intr", "pc", "in", "rdtsc", "mmio", "dma", "crewinc", "allpc"
};

extern __thread int cm_coreid;
log_t **cm_log;

#define MAXLOGLEN 256

#define LOGDIR "replay-log/"
static void open_log1(FILE **log, const char* logname, const char *mode)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, LOGDIR"%s-%d", logname, cm_coreid);

    *log = fopen(logpath, mode);
    if (!(*log)) {
        printf("Can't open log file %s\n", logpath);
        exit(1);
    }
}

void cm_open_log(const char *mode)
{
    int i;
    for (i = 0; i < N_CM_LOG - 2; i++)
        open_log1(&(cm_log[cm_coreid][i]), cm_log_name[i], mode);
}

void cm_replay_flush_log(void)
{
    int i;
    coremu_debug("flushing log core %hu", cm_coreid);
    for (i = 0; i < N_CM_LOG; i++)
        fflush(cm_log[cm_coreid][i]);
    coremu_debug("flushed log core %hu", cm_coreid);
}

extern int cm_run_mode;

void cm_debug_open_log(void)
{
    if (cm_run_mode == 1)
        open_log1(&(cm_log[cm_coreid][MEMREC]), "memrec", "w");
    else {
        open_log1(&(cm_log[cm_coreid][MEMPLAY]), "memplay", "w");
        open_log1(&(cm_log[cm_coreid][MEMREC]), "memrec", "r");
    }
}
