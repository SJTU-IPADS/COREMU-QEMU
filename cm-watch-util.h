#ifndef _CM_WATCH_UTIL_H
#define _CM_WATCH_UTIL_H

#include <stdint.h>

void cm_watch_util_init(void);

void cm_print_dumpstack(void *addr);
void cm_dump_stack(int level);

void cm_record_access(target_ulong eip, char type, uint64_t order);

#endif /* _CM_WATCH_UTIL_H */
