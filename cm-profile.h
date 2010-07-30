/*
 * COREMU Parallel Emulator Framework
 *
 * Copyright (C) 2010 Parallel Processing Institute, Fudan Univ.
 *  <http://ppi.fudan.edu.cn/system_research_group>
 *
 * Authors:
 *  Zhaoguo Wang    <zgwang@fudan.edu.cn>
 *  Yufei Chen      <chenyufei@fudan.edu.cn>
 *  Ran Liu         <naruilone@gmail.com>
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CM_PROFILE_H
#define _CM_PROFILE_H

#include <stdbool.h>
#include "cm-intr.h"

enum {
    CM_PROFILE_STOP = 0,
    CM_PROFILE_START,
    CM_PROFILE_PREPARE,
    CM_PROFILE_REPORT,
    CM_PROFILE_FLUSH,
    CM_PROFILE_START_TRACE, /* patches the TB to collect backtrace info. */
    CM_PROFILE_REPORT_TRACE,
};

/* In cpu_exec, we need to know whether we should start execution at the
 * original TC (translated code) or the increment count TC. */
extern int cm_profile_state;

void cm_profile_init(void);

/* Implemented in exec.c */
void cm_cpu_unlink_all_tb(void);

/* Used in tb_alloc, otherwise we can make it static */
bool is_hot_pc(target_ulong pc);

/* Called in tb_flush. */
void cm_flush_trace_prologue(void);

/* Called in tb_add_jump */
uint8_t *cm_gen_trace_prologue(int tbid);

/* Also used in tb_add_jump */
#define JMP_ADDR_OFFSET 13
static inline void cm_patch_trace_jmp_addr(unsigned long ptr, unsigned long jmp_addr)
{
    *(uint32_t *)(ptr + JMP_ADDR_OFFSET) = jmp_addr - (ptr + JMP_ADDR_OFFSET + 4);
}

#endif /* _CM_PROFILE_H */
