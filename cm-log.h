#ifndef _CM_LOG_H
#define _CM_LOG_H

#include <stdio.h>

enum {
    INTR,
    IPI,
    PC,
    IN,
    RDTSC,
    MMIO,
    DISK_DMA,
    CREW_INC,
    ALLPC,
    TLBFLUSH,
    GENCODE,
    READ,
    WRITE,
    TLBFILL,
    TBFLUSH,
    SPSR,
    N_CM_LOG,
};

typedef FILE *log_t;
/* 2d array containing logs for each cpu, use cm_log[coreid][logent] to access
 * the specific log. */
extern log_t **cm_log;

void cm_replay_flush_log(int coreid);

void cm_log_init(const char *mode);

#endif /* _CM_LOG_H */
