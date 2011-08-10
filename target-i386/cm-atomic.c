/*
 * Copyright (C) 2010 Parallel Processing Institute (PPI), Fudan Univ.
 *  <http://ppi.fudan.edu.cn/system_research_group>
 *
 * Authors:
 *  Zhaoguo Wang    <zgwang@fudan.edu.cn>
 *  Yufei Chen      <chenyufei@fudan.edu.cn>
 *  Ran Liu         <naruilone@gmail.com>
 *  Xi Wu           <wuxi@fudan.edu.cn>
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

/* We include this file in op_helper.c */

#include <stdlib.h>
#include <pthread.h>
#include "coremu-atomic.h"
#include "coremu-sched.h"
#include "cm-mmu.h"
#include "dyngen-exec.h"
#include "cm-crew.h"

/* These definitions are copied from translate.c */
#if defined(WORDS_BIGENDIAN)
#define REG_B_OFFSET (sizeof(target_ulong) - 1)
#define REG_H_OFFSET (sizeof(target_ulong) - 2)
#define REG_W_OFFSET (sizeof(target_ulong) - 2)
#define REG_L_OFFSET (sizeof(target_ulong) - 4)
#define REG_LH_OFFSET (sizeof(target_ulong) - 8)
#else
#define REG_B_OFFSET 0
#define REG_H_OFFSET 1
#define REG_W_OFFSET 0
#define REG_L_OFFSET 0
#define REG_LH_OFFSET 4
#endif

#ifdef TARGET_X86_64
#define X86_64_DEF(...)  __VA_ARGS__
#else
#define X86_64_DEF(...)
#endif

#define REG_LOW_MASK (~(uint64_t)0x0>>32)

/* gen_op instructions */
/* i386 arith/logic operations */
enum {
    OP_ADDL,
    OP_ORL,
    OP_ADCL,
    OP_SBBL,
    OP_ANDL,
    OP_SUBL,
    OP_XORL,
    OP_CMPL,
};

/* */
static target_ulong cm_get_reg_val(int ot, int hregs, int reg)
{
    target_ulong val, offset;
    CPUState *env1 = cpu_single_env;

    switch(ot) {
    case 0:  /* OT_BYTE */
        if (reg < 4 X86_64_DEF( || reg >= 8 || hregs)) {
            goto std_case;
        } else {
            offset = offsetof(CPUState, regs[reg - 4]) + REG_H_OFFSET;
            val = *(((uint8_t *)env1) + offset);
        }
        break;
    default:
    std_case:
        val =  env1->regs[reg];
        break;
    }

    return val;
}

static void cm_set_reg_val(int ot, int hregs, int reg, target_ulong val)
{
      target_ulong offset;

      CPUState *env1 = cpu_single_env;

      switch(ot) {
      case 0: /* OT_BYTE */
          if (reg < 4 X86_64_DEF (|| reg >= 8 || hregs)) {
              offset = offsetof(CPUState, regs[reg]) + REG_B_OFFSET;
              *(((uint8_t *) env1) + offset) = (uint8_t)val;
          } else {
              offset = offsetof(CPUState, regs[reg - 4]) + REG_H_OFFSET;
              *(((uint8_t *) env1) + offset) = (uint8_t)val;
          }
          break;
      case 1: /* OT_WORD */
          offset = offsetof(CPUState, regs[reg]) + REG_W_OFFSET;
          *((uint16_t *)((uint8_t *)env1 + offset)) = (uint16_t)val;
          break;
      case 2: /* OT_LONG */
          env1->regs[reg] = REG_LOW_MASK & val;
          break;
      default:
      case 3: /* OT_QUAD */
          env1->regs[reg] = val;
          break;
      }
}

static inline memobj_t *cm_start_atomic_insn(const void *q_addr)
{
    memobj_t *mo = NULL;
    switch (cm_run_mode) {
    case CM_RUNMODE_RECORD:
        mo = cm_write_lock(q_addr);
        break;
    case CM_RUNMODE_REPLAY:
        cm_apply_replay_log();
        break;
    }
    return mo;
}

void debug_mem_access(const void *addr, char c, int is_tc);
static inline void cm_end_atomic_insn(memobj_t *mo)
{
    if (cm_run_mode != CM_RUNMODE_NORMAL) {
        /*debug_mem_access((void *)0xdeadbeef, 'a');*/
        (*memop)++;
    }
    if (cm_run_mode == CM_RUNMODE_RECORD)
        cm_write_unlock(mo);
}

#define DATA_BITS 8
#include "cm-atomic-template.h"

#define DATA_BITS 16
#include "cm-atomic-template.h"

#define DATA_BITS 32
#include "cm-atomic-template.h"

#ifdef TARGET_X86_64
#define DATA_BITS 64
#include "cm-atomic-template.h"
#endif

#define INS 1
#include "cm-atomic-btx.h"
#define INS 2
#include "cm-atomic-btx.h"
#define INS 3
#include "cm-atomic-btx.h"

/* cmpxchgb (8, 16) */
void helper_atomic_cmpxchg8b(target_ulong a0)
{
    uint64_t edx_eax, ecx_ebx, res;
    int eflags;
    unsigned long q_addr;

    CM_GET_QEMU_ADDR(q_addr, a0);

#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif

    eflags = helper_cc_compute_all(CC_OP);

    edx_eax = (((uint64_t)EDX << 32) | (uint32_t)EAX);
    ecx_ebx = (((uint64_t)ECX << 32) | (uint32_t)EBX);

    res = atomic_compare_exchangeq((uint64_t *)q_addr, edx_eax, ecx_ebx);
    mb();

    if (res == edx_eax) {
         eflags |= CC_Z;
    } else {
        EDX = (uint32_t)(res >> 32);
        EAX = (uint32_t)res;
        eflags &= ~CC_Z;
    }

    CC_SRC = eflags;

#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo);
#endif
}

void helper_atomic_cmpxchg16b(target_ulong a0)
{
    uint8_t res;
    int eflags;
    unsigned long q_addr;

    CM_GET_QEMU_ADDR(q_addr, a0);

#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif

    eflags = helper_cc_compute_all(CC_OP);

    uint64_t old_rax = *(uint64_t *)q_addr;
    uint64_t old_rdx = *(uint64_t *)(q_addr + 8);
    res = atomic_compare_exchange16b((uint64_t *)q_addr, EAX, EDX, EBX, ECX);
    mb();

    if (res) {
        eflags |= CC_Z;         /* swap success */
    } else {
        EDX = old_rdx;
        EAX = old_rax;
        eflags &= ~CC_Z;        /* read the old value ! */
    }

    CC_SRC = eflags;
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo);
#endif
}

/* fence **/
void helper_fence(void)
{
    mb();
}

/* pause */
void helper_pause(void)
{
    coremu_cpu_sched(CM_EVENT_PAUSE);
}
