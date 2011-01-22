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
#include <stdint.h>
#include "coremu-core.h"
#include "cm-replay.h"

#define MAXLOGLEN 256

int cm_run_mode;

__thread uint64_t cm_tb_exec_cnt;

static __thread FILE *cm_intr_log;
#define LOGFILEFMT "log/%d-intr", coremu_get_core_id()

void cm_replay_core_init(void)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, LOGFILEFMT);
    cm_intr_log = fopen(logpath, "w");
}

void cm_record_intr(int intno) {
    fprintf(cm_intr_log, "%d %lu\n", intno, cm_tb_exec_cnt);
}

void cm_replay_intr(void) {
    
}
