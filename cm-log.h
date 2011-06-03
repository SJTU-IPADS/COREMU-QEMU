#ifndef _CM_LOG_H
#define _CM_LOG_H

#include <stdio.h>

enum {
    INTR,
    PC,
    IN,
    RDTSC,
    MMIO,
    DISK_DMA,
    CREW_INC,
    CPUSTART,
    N_CM_LOG,
};

typedef FILE *log_t;
/* 2d array containing logs for each cpu, use cm_log[coreid][logent] to access
 * the specific log. */
extern log_t **cm_log;

void cm_open_log(const char *mode);
void cm_replay_flush_log(void);

#endif /* _CM_LOG_H */
