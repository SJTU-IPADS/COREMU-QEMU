#ifndef _CM_REPLAY_H
#define _CM_REPLAY_H

enum {
    CM_RUNMODE_NORMAL, /* Not being recorded or replayed, this is default */
    CM_RUNMODE_RECORD,
    CM_RUNMODE_REPLAY,
};

#define NINTR 3 // For debug
extern __thread long cm_inject_eip; // For debug

#define CM_REPLAY_INT 0x80000000

extern int cm_run_mode;
extern __thread uint64_t cm_tb_exec_cnt;

void cm_replay_core_init(void);
void cm_record_intr(int intno, long eip);
int cm_replay_intr(void);

void cm_replay_assert_pc(unsigned long eip);

#endif /* _CM_REPLAY_H */
