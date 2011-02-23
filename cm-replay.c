/*
 * COREMU Parallel Emulator Framework
 *
 * Copyright (C) 2010 Parallel Processing Institute, Fudan Univ.
 *  <http://ppi.fudan.edu.cn/system_research_group>
 *
 * Authors:
 *  Yufei Chen      <chenyufei@fudan.edu.cn>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "coremu-core.h"
#include "cm-replay.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

#define MAXLOGLEN 256

/* Whether the vm is being recorded or replayed. */
int cm_run_mode;

__thread uint64_t cm_tb_exec_cnt;
/* Inject interrupt when cm_tb_exec_cnt reaches this value */
static __thread uint64_t cm_inject_exec_cnt = -1;
static __thread int cm_inject_intno;
__thread long cm_inject_eip;

static __thread FILE *cm_log_intr;
static __thread FILE *cm_log_pc;
static __thread FILE *cm_log_in;
static __thread FILE *cm_log_rdtsc;

#define LOGDIR "replay-log/"
static void open_log1(FILE **log, const char* logname, const char *mode)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, LOGDIR"%s-%d", logname, coremu_get_core_id());

    *log = fopen(logpath, mode);
    if (!cm_log_intr) {
        printf("Can't open log file %s\n", logpath);
        exit(1);
    }
}

static void cm_open_log(const char *mode) {
    open_log1(&cm_log_intr, "intr", mode);
    open_log1(&cm_log_pc, "pc", mode);
    open_log1(&cm_log_in, "in", mode);
    open_log1(&cm_log_rdtsc, "rdtsc", mode);
}

static int cm_replay_inited = 0;

static inline void cm_read_intr_log(void);
void cm_replay_core_init(void)
{
    if (cm_run_mode == CM_RUNMODE_NORMAL || cm_replay_inited)
        return;
    const char *mode = cm_run_mode == CM_RUNMODE_REPLAY ? "r" : "w";
    cm_open_log(mode);
    if (cm_run_mode == CM_RUNMODE_REPLAY)
        cm_read_intr_log();
    cm_replay_inited = 1;
}

/* interrupt */

#define cm_log_intr_FMT "%d %lu %p\n"

static inline void cm_read_intr_log(void)
{
    if (fscanf(cm_log_intr, cm_log_intr_FMT, &cm_inject_intno,
               &cm_inject_exec_cnt, (void **)&cm_inject_eip) == EOF) {
        cm_inject_exec_cnt = -1;
    }
}

static inline void cm_write_intr_log(int intno, long eip)
{
    fprintf(cm_log_intr, cm_log_intr_FMT, intno, cm_tb_exec_cnt, (void *)(long)eip);
}

void cm_record_intr(int intno, long eip) {
    cm_write_intr_log(intno, eip);
}

int cm_replay_intr(void) {
    int intno;

    if (cm_tb_exec_cnt == cm_inject_exec_cnt) {
        /*coremu_debug("injecting interrupt at %lu", cm_tb_exec_cnt);*/
        intno = cm_inject_intno;
        cm_read_intr_log(); /* Read next log entry. */
        return intno;
    }
    return -1;
}

/* input data */

#define IN_LOG_FMT "%x %x\n"
void cm_record_in(uint32_t address, uint32_t value) {
    fprintf(cm_log_in, IN_LOG_FMT, address, value);
}
/* Returns 0 if ther's no more log entry. */
int cm_replay_in(uint32_t *value) {
    uint32_t address;
    if (fscanf(cm_log_in, IN_LOG_FMT, &address, value) == EOF) {
        printf("no more in log\n");
        exit(0);
        return 0;
    }
    return 1;
}

/* rdtsc */
#define RDTSC_LOG_FMT "%lu\n"
void cm_record_rdtsc(uint64_t value) {
    fprintf(cm_log_rdtsc, RDTSC_LOG_FMT, value);
}

int cm_replay_rdtsc(uint64_t *value) {
    if (fscanf(cm_log_rdtsc, RDTSC_LOG_FMT, value) == EOF)
        return 0;
    return 1;
}

/* Check whether the next eip is the same as recorded. This is used for
 * debugging. */
extern int cm_ioport_read_cnt;
#define PC_LOG_FMT "%08lx\n"
void cm_replay_assert_pc(unsigned long eip) {
    unsigned long next_eip;

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log_pc, PC_LOG_FMT, &next_eip) == EOF) {
            printf("no more pc log\n");
            exit(1);
        }
        coremu_assert(eip == next_eip,
                      "eip = %p, recorded eip = %p, cm_tb_exec_cnt = %lu, cm_ioport_read_cnt = %d",
                      (void *)eip, (void *)next_eip, cm_tb_exec_cnt, cm_ioport_read_cnt);
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log_pc, PC_LOG_FMT, eip);
        break;
    }
}

void cm_replay_flush_log(void) {
    fflush(cm_log_intr);
    fflush(cm_log_in);
    fflush(cm_log_rdtsc);
    fflush(cm_log_pc);
}
