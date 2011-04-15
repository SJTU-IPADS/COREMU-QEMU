#include <stdlib.h>

#include "cm-log.h"

static const char *cm_log_name[] = {
    "intr", "pc", "in", "rdtsc", "mmio", "dma", "crewinc"
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
    for (i = 0; i < N_CM_LOG; i++)
        open_log1(&(cm_log[cm_coreid][i]), cm_log_name[i], mode);
}

void cm_replay_flush_log(void)
{
    int i;
    for (i = 0; i < N_CM_LOG; i++)
        fflush(cm_log[cm_coreid][i]);
}

