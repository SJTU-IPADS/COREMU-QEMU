#ifndef _CM_LOG_H
#define _CM_LOG_H

#include <stdio.h>
#include <stdlib.h>
#include "coremu-config.h"

/* For logs that need to be flushed upon exit, put it in this file because it
 * contains the logic to open and flush. */

enum {
    INTR,
    IPI,
    IN,
    RDTSC,
    MMIO,
    DISK_DMA,
    PGFLT,
#ifdef ASSERT_REPLAY_PC
    PC,
#endif
#ifdef ASSERT_REPLAY_TLBFLUSH
    TLBFLUSH,
#endif
#ifdef ASSERT_REPLAY_GENCODE
    GENCODE,
#endif
#ifdef DEBUG_MEM_ACCESS
    MEMACC,
#endif
#ifdef ASSERT_REPLAY_TLBFILL
    TLBFILL,
#endif
#ifdef ASSERT_REPLAY_TBFILL
    TBFLUSH,
#endif
    N_CM_LOG,
};

#define MAX_PATH_LEN 256
#define LOGDIR "replay-log/"

static inline void cm_logpath(char *buf, const char *name, long id) {
    if (snprintf(buf, MAX_PATH_LEN, LOGDIR"%s-%ld", name, id) >= MAX_PATH_LEN) {
        printf("Path name too long\n");
        exit(1);
    }
}

typedef FILE *log_t;

/* ALLPC log requires one core to write log to other cores' log */
extern log_t *cm_log_allpc;

/* use cm_log[logent] to access specific log. */
extern __thread log_t *cm_log;

void cm_replay_flush_log(int coreid);

void cm_log_init(void);
void cm_log_init_core(void);

#endif /* _CM_LOG_H */
