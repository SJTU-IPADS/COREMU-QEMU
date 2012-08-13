#include "coremu-config.h"
#include "cm-log.h"
#include "cm-replay.h"
#include <stdlib.h>
#include <malloc.h>

#define DEBUG_COREMU
#include "coremu-debug.h"

static const char *cm_log_name[] = {
#ifdef COMBINE_LOG
    "combined",
#else
    "ipi",
    "in",
    "rdtsc",
    "mmio",
    "dma",
#endif // COMBINE_LOG
    "intr",
    "pgflt",
#ifdef ASSERT_REPLAY_PC
    "pc",
#endif
#ifdef ASSERT_REPLAY_TLBFLU
    "tlbflush",
#endif
#ifdef ASSERT_REPLAY_GENCODE
    "gencode",
#endif
#ifdef DEBUG_MEM_ACCESS
    "memacc",
#endif
#ifdef ASSERT_REPLAY_TLBFILL 
    "tlbfill",
#endif
#ifdef ASSERT_REPLAY_TBFILL 
    "tbflush"
#endif
};

__thread log_t *cm_log;
log_t *cm_log_allpc;

#ifdef REPLAY_LOGBUF
CMLogBuf ***cm_log_buf;
#endif

static FILE *open_log1(const char* logname, const char *mode, int coreid)
{
    char logpath[MAX_PATH_LEN];
    cm_logpath(logpath, logname, coreid);

    FILE *log = fopen(logpath, mode);
    if (!(log)) {
        printf("Can't open log file %s\n", logpath);
        exit(1);
    }
    return log;
}

void cm_replay_flush_log(int coreid)
{
    int i;
    for (i = 0; i < N_CM_LOG; i++) {
        fflush(cm_log[i]);
    }
}

extern int smp_cpus;

void cm_log_init(void)
{
    cm_log_allpc = calloc(smp_cpus, sizeof(*cm_log_allpc));
    assert(cm_log_allpc);
}

void cm_log_init_core(void)
{
    cm_log = calloc(N_CM_LOG, sizeof(*cm_log));
    assert(cm_log);

    const char *mode = (cm_run_mode == CM_RUNMODE_REPLAY) ? "r" : "w";

    int i;
    for (i = 0; i < N_CM_LOG; i++) {
        cm_log[i] = open_log1(cm_log_name[i], mode, cm_coreid);
    }

    cm_log_allpc[cm_coreid] = open_log1("allpc", mode, cm_coreid);
}
