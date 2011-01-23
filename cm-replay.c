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
static __thread uint64_t cm_inject_exec_cnt;
static __thread int cm_inject_intno;
__thread long cm_inject_eip;

static __thread FILE *cm_intr_log;

static void open_intr_log(const char *mode)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, "log-intr/%d-intr", coremu_get_core_id());

    cm_intr_log = fopen(logpath, mode);
    if (!cm_intr_log) {
        printf("Can't open interrupt log\n");
        exit(1);
    }
}

#define CM_INTR_LOG_FMT "%d %lu %p\n"

static inline void cm_read_intr_log(void)
{
    if (fscanf(cm_intr_log, CM_INTR_LOG_FMT, &cm_inject_intno,
               &cm_inject_exec_cnt, (void **)&cm_inject_eip) == EOF) {
        printf("No more log entry\n");
        exit(1);
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
        cm_read_intr_log();
        break;
    case CM_RUNMODE_RECORD:
        open_intr_log("w");
        break;
    }
}

void cm_record_intr(int intno, long eip) {
    cm_write_intr_log(intno, eip);
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
