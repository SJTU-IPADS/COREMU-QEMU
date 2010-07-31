#ifndef _CM_WATCH_UTIL_H
#define _CM_WATCH_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include "coremu-logbuffer.h"

void cm_print_dumpstack(FILE *logfile, void *paddr);
void cm_dump_stack(int level, CMLogbuf *buf);

#endif /* _CM_WATCH_UTIL_H */
