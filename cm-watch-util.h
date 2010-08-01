#ifndef _CM_WATCH_UTIL_H
#define _CM_WATCH_UTIL_H

#include <stdint.h>

void cm_watch_util_init(void);

void cm_print_memtrace(void *addr);

void cm_print_dumpstack(void *addr);
void cm_dump_stack(int level);

target_ulong cm_get_cpu_eip(void);
int cm_get_cpu_idx(void);
target_ulong cm_get_stack_page_addr(void);

void cm_record_access(target_ulong eip, char type, uint64_t order);

#endif /* _CM_WATCH_UTIL_H */
