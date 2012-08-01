#ifndef _CM_REPLAY_H
#define _CM_REPLAY_H

#include "coremu-config.h"
#include "cm-log.h"
#include <stdint.h>

enum {
    CM_RUNMODE_NORMAL, /* Not being recorded or replayed, this is default */
    CM_RUNMODE_RECORD,
    CM_RUNMODE_REPLAY,
};

#define NINTR 10 // For debug

typedef struct {
    uint64_t exec_cnt;
#ifdef DEBUG_REPLAY
    uint64_t eip;
#endif
    int intno;
} __attribute__ ((packed)) IntrLog;
extern __thread IntrLog cm_inject_intr;
extern __thread volatile int cm_ipi_intr_handler_cnt;

/* Mark the interrupt being generated from the log. */
#define CM_REPLAY_INT 0x80000000
#define CM_CPU_INIT 0xfffffffe
#define CM_CPU_SIPI 0xfffffffd
#define CM_CPU_EXIT 0xfffffffc
//#define CM_CPU_TLBFLUSH 0xfffffffc

extern int cm_run_mode;
int cm_get_run_mode(void);
extern uint64_t *cm_tb_exec_cnt;

typedef int8_t cpuid_t;
extern __thread cpuid_t cm_coreid;

void cm_replay_init(void);
void cm_replay_core_init(void);

void cm_record_intr(int intno, long eip);
int  cm_replay_intr(void);
void cm_record_ipi_handler_cnt(int);
int  cm_replay_ipi_handler_cnt(int *);

enum {
    DO_CPU_INIT,
    DO_CPU_SIPI,
};

void cm_record_all_exec_cnt(void);
void cm_replay_all_exec_cnt(void);

#define GEN_HEADER(name, type) \
    void cm_record_##name(type arg); \
    int cm_replay_##name(type *arg);

GEN_HEADER(in, uint32_t);
GEN_HEADER(mmio, uint32_t);
GEN_HEADER(rdtsc, uint64_t);

extern volatile uint64_t cm_dma_cnt;
void cm_record_disk_dma(void);

void cm_debug_mmio(void *);

void cm_replay_assert_pc(uint64_t);
void cm_replay_assert_tlbflush(uint64_t exec_cnt, uint64_t eip, int coreid);
void cm_replay_assert_gencode(uint64_t);
void cm_replay_assert_tbflush(uint64_t);
void cm_replay_assert_tlbfill(uint64_t addr);

void cm_print_replay_info(void);

#undef GEN_HEADER

#endif /* _CM_REPLAY_H */
