#ifndef _CM_REPLAY_H
#define _CM_REPLAY_H

enum {
    CM_RUNMODE_NORMAL, /* Not being recorded or replayed, this is default */
    CM_RUNMODE_RECORD,
    CM_RUNMODE_REPLAY,
};

#define NINTR 10 // For debug
extern __thread long cm_inject_eip; // For debug

/* Mark the interrupt being generated from the log. */
#define CM_REPLAY_INT 0x80000000

extern int cm_run_mode;
extern __thread uint64_t cm_tb_exec_cnt;

void cm_replay_core_init(void);

void cm_record_intr(int intno, long eip);
int cm_replay_intr(void);

void cm_record_in(uint32_t address, uint32_t value);
int cm_replay_in(uint32_t *value);

void cm_record_rdtsc(uint64_t value);
int cm_replay_rdtsc(uint64_t *value);

void cm_replay_flush_log(void);

void cm_replay_assert_pc(unsigned long eip);

#endif /* _CM_REPLAY_H */
