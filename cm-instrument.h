#ifndef _CM_WATCH_UTIL_H
#define _CM_WATCH_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include "coremu-logbuffer.h"

target_ulong cm_get_cpu_eip(void);
int cm_get_cpu_idx(void);
target_ulong cm_get_stack_page_addr(void);

void cm_record_access(target_ulong eip, char type, uint64_t order);

void cm_print_dumpstack(FILE *logfile, void *paddr);
void cm_dump_stack(int level, CMLogbuf *buf);
#endif /* _CM_WATCH_UTIL_H */
