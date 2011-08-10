#include <stdlib.h>
#include <malloc.h>

#define CONFIG_REPLAY
#include "coremu-debug.h"

#include "cm-log.h"

static const char *cm_log_name[] = {
    "intr", "pc", "in", "rdtsc", "mmio", "dma", "crewinc", "allpc", "tlbflush",
    "gencode"
};

log_t **cm_log;

#define MAXLOGLEN 256

#define LOGDIR "replay-log/"
static void open_log1(FILE **log, const char* logname, const char *mode, int coreid)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, LOGDIR"%s-%d", logname, coreid);

    *log = fopen(logpath, mode);
    if (!(*log)) {
        printf("Can't open log file %s\n", logpath);
        exit(1);
    }
}

static void cm_open_log(const char *mode, int coreid)
{
    int i;
    for (i = 0; i < N_CM_LOG - 2; i++)
        open_log1(&(cm_log[coreid][i]), cm_log_name[i], mode, coreid);
}

void cm_replay_flush_log(int coreid)
{
    int i;
    coremu_debug("flushing log core %hu", coreid);
    for (i = 0; i < N_CM_LOG; i++)
        fflush(cm_log[coreid][i]);
    coremu_debug("flushed log core %hu", coreid);
}

extern int cm_run_mode;

static void cm_debug_open_log(int coreid)
{
    if (cm_run_mode == 1)
        open_log1(&(cm_log[coreid][MEMREC]), "memrec", "w", coreid);
    else {
        open_log1(&(cm_log[coreid][MEMPLAY]), "memplay", "w", coreid);
        open_log1(&(cm_log[coreid][MEMREC]), "memrec", "r", coreid);
    }
}

extern int smp_cpus;

void cm_log_init(const char *mode)
{
    cm_log = calloc(smp_cpus, sizeof(FILE **));
    assert(cm_log);
    int i;
    for (i = 0; i < smp_cpus; i++) {
        cm_log[i] = calloc(N_CM_LOG, sizeof(FILE *));
        assert(cm_log[i]);

        cm_open_log(mode, i);
        cm_debug_open_log(i);
    }
}

