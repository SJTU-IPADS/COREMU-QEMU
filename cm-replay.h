#ifndef _CM_REPLAY_H
#define _CM_REPLAY_H

#include "cm-log.h"

enum {
    CM_RUNMODE_NORMAL, /* Not being recorded or replayed, this is default */
    CM_RUNMODE_RECORD,
    CM_RUNMODE_REPLAY,
};

#define NINTR 10 // For debug

typedef struct {
    int intno;
    uint64_t exec_cnt;
    uint64_t eip;
} IntrLog;
extern __thread IntrLog cm_inject_intr;

/* Mark the interrupt being generated from the log. */
#define CM_REPLAY_INT 0x80000000

extern int cm_run_mode;
int cm_get_run_mode(void);
extern uint64_t *cm_tb_exec_cnt;
extern __thread int cm_coreid;

void cm_replay_init(void);
void cm_replay_core_init(void);

void cm_record_intr(int intno, long eip);
int cm_replay_intr(void);

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

void cm_record_buffer_init(void);
void* cm_record_thread(void* arg);

void cm_print_replay_info(void);

#undef GEN_HEADER

#endif /* _CM_REPLAY_H */
