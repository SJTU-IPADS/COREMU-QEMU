/*
 * Copyright (C) 2010 PPI, Fudan Univ. <http://ppi.fudan.edu.cn/system_research_group>
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

/* We include this file in op_helper.c */

#include <assert.h>
#include "coremu-atomic.h"

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

/* The following several cm_getxxx function is used in implementing the atomic
 * instruction.
 * XXX: These functions are not platform specific, move them to other place
 * later. */

/* Given the guest virtual address, get the corresponding host address.
 * This function resembles __ldxxx in softmmu_template.h */
static target_ulong cm_get_qemu_addr(target_ulong v_addr)
{
    int mmu_idx, index, pd;
    CPUState *env1 = cpu_single_env;
    target_ulong q_addr = 0;
    void *retaddr;

    index = (v_addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);

    /* get the CPL, hence determine the MMU mode */
    mmu_idx = cpu_mmu_index(env1);
    if (unlikely(env1->tlb_table[mmu_idx][index].addr_write
                 != (v_addr & TARGET_PAGE_MASK))) {
            retaddr = GETPC();
            tlb_fill(v_addr, 1, mmu_idx, retaddr);
    }

    /* We use this function when in the implementation of atomic instructions,
     * and we are going to modify these memory. So we use addr_write. */
    pd = env1->tlb_table[mmu_idx][index].addr_write & ~TARGET_PAGE_MASK;
    q_addr = v_addr + env1->tlb_table[mmu_idx][index].addend;

    return q_addr;
}

static target_ulong cm_get_reg_val(int ot, int hregs, int reg)
{
    target_ulong val, offset;
    CPUState *env1 = cpu_single_env;

    switch(ot)
    {
    case 0:  /*OT_BYTE*/
        if (reg < 4 || reg >= 8 || hregs) {
            goto std_case;
        } else {
            offset = offsetof(CPUState, regs[reg - 4]) + REG_H_OFFSET;
            val = *(((uint8_t *) env1) + offset);
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

      switch(ot)
      {
      case 0: /* OT_BYTE */
          if (reg < 4 || reg >= 8 || hregs) {
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

#define LD_b ldub_raw
#define LD_w lduw_raw
#define LD_l ldl_raw
#define LD_q ldq_raw

/* Lightweight transactional memory. */
#define TX(vaddr, type, value, command) \
    target_ulong __q_addr = cm_get_qemu_addr(vaddr);          \
    DATA_##type __oldv;                                       \
    DATA_##type value;                                        \
    do {                                                      \
        __oldv = value = LD_##type((DATA_##type *)__q_addr);  \
        {command;};                                           \
    } while (__oldv != (atomic_compare_exchange##type(        \
                    (DATA_##type *)__q_addr, __oldv, value)))

/* Atomically emulate INC instruction using CAS1 and memory transaction. */

#define GEN_ATOMIC_INC(type, TYPE) \
void helper_atomic_inc##type(target_ulong a0, int c, int cpu_cc_op);  \
void helper_atomic_inc##type(target_ulong a0, int c, int cpu_cc_op)   \
{                                                                     \
    int eflags_c, eflags;                                             \
    int cc_op;                                                        \
                                                                      \
    /* compute the previous instruction c flags */                    \
    eflags_c = helper_cc_compute_c(CC_OP);                            \
                                                                      \
    TX(a0, type, value, {                                             \
        if (c > 0) {                                                  \
            value++;                                                  \
            cc_op = CC_OP_INC##TYPE;                                  \
        } else {                                                      \
            value--;                                                  \
            cc_op = CC_OP_DEC##TYPE;                                  \
        }                                                             \
    });                                                               \
                                                                      \
    CC_SRC = eflags_c;                                                \
    CC_DST = value;                                                   \
                                                                      \
    eflags = helper_cc_compute_all(cc_op);                            \
    CC_SRC = eflags;                                                  \
    mb();                                                             \
}                                                                     \

GEN_ATOMIC_INC(b, B);
GEN_ATOMIC_INC(w, W);
GEN_ATOMIC_INC(l, L);
GEN_ATOMIC_INC(q, Q);

#define VAL_b 0
#define VAL_w 1
#define VAL_l 2
#define VAL_q 3

#define GEN_XCHG(type) \
void helper_xchg##type(target_ulong a0, int reg, int hreg);   \
void helper_xchg##type(target_ulong a0, int reg, int hreg)    \
{                                                             \
    DATA_##type val, out;                                     \
    target_ulong q_addr;                                      \
                                                              \
    q_addr = cm_get_qemu_addr(a0);                            \
    val = (DATA_##type)cm_get_reg_val(VAL_##type, hreg, reg); \
    out = atomic_exchange##type((DATA_##type *)q_addr, val);  \
                                                              \
    cm_set_reg_val(VAL_##type, hreg, reg, out);               \
    mb();                                                     \
}

GEN_XCHG(b);
GEN_XCHG(w);
GEN_XCHG(l);
GEN_XCHG(q);

#define GEN_OP(type, TYPE) \
void helper_atomic_op##type(target_ulong a0, target_ulong t1,    \
        int op, int cpu_cc_op);                                  \
void helper_atomic_op##type(target_ulong a0, target_ulong t1,    \
        int op, int cpu_cc_op)                                   \
{                                                                \
    DATA_##type operand;                                         \
    int eflags_c, eflags;                                        \
    int cc_op;                                                   \
    target_ulong q_addr;                                         \
                                                                 \
    q_addr = cm_get_qemu_addr(a0);                               \
    assert(cpu_cc_op == CC_OP);                                  \
                                                                 \
    /* compute the previous instruction c flags */               \
    eflags_c = helper_cc_compute_c(CC_OP);                       \
    operand = (DATA_##type)t1;                                   \
                                                                 \
    TX(a0, type, value, {                                        \
        switch(op) {                                             \
        case OP_ADCL:                                            \
            value += operand + eflags_c;                         \
            cc_op = CC_OP_ADD##TYPE + (eflags_c << 2);           \
            CC_SRC = operand;                                    \
            break;                                               \
        case OP_SBBL:                                            \
            value = value - operand - eflags_c;                  \
            cc_op = CC_OP_SUB##TYPE + (eflags_c << 2);           \
            CC_SRC = operand;                                    \
            break;                                               \
        case OP_ADDL:                                            \
            value += operand;                                    \
            cc_op = CC_OP_ADD##TYPE;                             \
            CC_SRC = operand;                                    \
            break;                                               \
        case OP_SUBL:                                            \
            value = value - operand;                             \
            cc_op = CC_OP_SUB##TYPE;                             \
            CC_SRC = operand;                                    \
            break;                                               \
        default:                                                 \
        case OP_ANDL:                                            \
            value &= operand;                                    \
            cc_op = CC_OP_LOGIC##TYPE;                           \
            break;                                               \
        case OP_ORL:                                             \
            value |= operand;                                    \
            cc_op = CC_OP_LOGIC##TYPE;                           \
            break;                                               \
        case OP_XORL:                                            \
            value ^= operand;                                    \
            cc_op = CC_OP_LOGIC##TYPE;                           \
            break;                                               \
        case OP_CMPL:                                            \
            assert(0);                                           \
            break;                                               \
        }                                                        \
    });                                                          \
    CC_DST = value;                                              \
    /* successful transaction, compute the eflags */             \
    eflags = helper_cc_compute_all(cc_op);                       \
    CC_SRC = eflags;                                             \
    mb();                                                        \
}

GEN_OP(b, B);
GEN_OP(w, W);
GEN_OP(l, L);
GEN_OP(q, Q);

/* xadd */
