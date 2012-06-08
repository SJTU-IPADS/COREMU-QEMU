#ifndef _CM_LOG_H
#define _CM_LOG_H

#include <stdio.h>
#include "coremu-config.h"

enum {
    INTR,
    IPI,
    IN,
    RDTSC,
    MMIO,
    DISK_DMA,
    CREW_INC,
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
    READ,
    WRITE,
#endif
#ifdef ASSERT_REPLAY_TLBFILL
    TLBFILL,
#endif
#ifdef ASSERT_REPLAY_TBFILL
    TBFLUSH,
#endif
    N_CM_LOG,
};

typedef FILE *log_t;

/* ALLPC log requires one core to write log to other cores' log */
extern log_t *cm_log_allpc;

/* use cm_log[logent] to access specific log. */
extern __thread log_t *cm_log;

void cm_replay_flush_log(int coreid);

void cm_log_init(void);
void cm_log_init_core(void);

#endif /* _CM_LOG_H */
