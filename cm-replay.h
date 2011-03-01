#ifndef _CM_REPLAY_H
#define _CM_REPLAY_H

enum {
    CM_RUNMODE_NORMAL, /* Not being recorded or replayed, this is default */
    CM_RUNMODE_RECORD,
    CM_RUNMODE_REPLAY,
};

#define NINTR 20 // For debug
extern __thread long cm_inject_eip; // For debug

/* Mark the interrupt being generated from the log. */
#define CM_REPLAY_INT 0x80000000

extern int cm_run_mode;
int cm_get_run_mode(void);
extern __thread uint64_t cm_tb_exec_cnt;

void cm_replay_core_init(void);

void cm_record_intr(int intno, long eip);
int cm_replay_intr(void);

#define GEN_HEADER(name, type) \
    void cm_record_##name(type arg); \
    int cm_replay_##name(type *arg);

GEN_HEADER(in, uint32_t);
GEN_HEADER(mmio, uint32_t);
GEN_HEADER(rdtsc, uint64_t);

void cm_debug_mmio(void *);

void cm_replay_assert_pc(unsigned long eip);

void cm_replay_flush_log(void);

#undef GEN_HEADER

#endif /* _CM_REPLAY_H */
