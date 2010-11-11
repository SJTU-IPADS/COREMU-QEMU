#ifndef _CM_MEMTRACE_H
#define _CM_MEMTRACE_H

void cm_memtrace_init(int cpuidx);
void cm_memtrace_logging(uint64_t addr, int write);

void cm_trigger_Dldb(unsigned long addr);
void cm_trigger_Dldw(unsigned long addr);
void cm_trigger_Dldl(unsigned long addr);
void cm_trigger_Dldq(unsigned long addr);

void cm_trigger_Dstb(unsigned long addr);
void cm_trigger_Dstw(unsigned long addr);
void cm_trigger_Dstl(unsigned long addr);
void cm_trigger_Dstq(unsigned long addr);

#include "cm-features/logbuffer.h"
extern __thread CMLogbuf *memtrace_buf;

extern int cachesim_enable;

#endif
