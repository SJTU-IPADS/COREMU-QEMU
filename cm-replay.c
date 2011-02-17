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

static __thread FILE *cm_intr_log;
static __thread FILE *cm_pc_log;

static void open_log(FILE **log, const char* logname, const char *mode)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, logname, coremu_get_core_id());

    *log = fopen(logpath, mode);
    if (!cm_intr_log) {
        printf("Can't open log file %s\n", logpath);
        exit(1);
    }
}

#define LOGDIR "replay-log/"

static void open_intr_log(const char *mode) {
    open_log(&cm_intr_log, LOGDIR"intr-%d", mode);
}

static void open_pc_log(const char *mode) {
    open_log(&cm_pc_log, LOGDIR"pc-%d", mode);
}

#define CM_INTR_LOG_FMT "%d %lu %p\n"

static inline void cm_read_intr_log(void)
{
    if (fscanf(cm_intr_log, CM_INTR_LOG_FMT, &cm_inject_intno,
               &cm_inject_exec_cnt, (void **)&cm_inject_eip) == EOF) {
        cm_inject_exec_cnt = -1;
    }
}

static inline void cm_write_intr_log(int intno, long eip)
{
    fprintf(cm_intr_log, CM_INTR_LOG_FMT, intno, cm_tb_exec_cnt, (void *)(long)eip);
}

void cm_replay_core_init(void)
{
    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        open_intr_log("r");
        open_pc_log("r");
        cm_read_intr_log();
        break;
    case CM_RUNMODE_RECORD:
        open_intr_log("w");
        open_pc_log("w");
        break;
    }
}

void cm_record_intr(int intno, long eip) {
    cm_write_intr_log(intno, eip);
}

#define PC_LOG_FMT "%lu\n"

/* Check whether the next eip is the same as recorded. */
void cm_replay_assert_pc(unsigned long eip) {
    unsigned long next_eip;

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (eip >= 0x100000) {
            if (fscanf(cm_pc_log, PC_LOG_FMT, &next_eip) == EOF) {
                printf("no more pc log\n");
                exit(1);
            }
            coremu_assert(eip == next_eip, "eip = %p, recorded eip = %p\n",
                          (void *)eip, (void *)next_eip);
        }
        break;
    case CM_RUNMODE_RECORD:
        if (eip >= 0x100000)
            fprintf(cm_pc_log, PC_LOG_FMT, eip);
        break;
    }
}

int cm_replay_intr(void) {
    int intno;

    if (cm_tb_exec_cnt == cm_inject_exec_cnt) {
        intno = cm_inject_intno;
        cm_read_intr_log(); /* Read next log entry. */
        return intno;
    }
    return -1;
}
