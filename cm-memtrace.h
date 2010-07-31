#ifndef _CM_MEMTRACE_H
#define _CM_MEMTRACE_H
void memtrace_logging(uint64_t addr, int write);
void memtrace_start(void);
void memtrace_stop(void);
#endif