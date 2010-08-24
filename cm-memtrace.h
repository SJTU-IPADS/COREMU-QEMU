#ifndef _CM_MEMTRACE_H
#define _CM_MEMTRACE_H

void cm_memtrace_init(int cpuidx);
void memtrace_logging(uint64_t addr, int write);

extern __thread CMLogbuf *memtrace_buf;

#endif
