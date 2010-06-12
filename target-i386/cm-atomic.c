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

#include <stdlib.h>
#include <pthread.h>
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

/* XXX: This function is not platform specific, move them to other place
 * later. */

/* Given the guest virtual address, get the corresponding host address.
 * This macro resembles ldxxx in softmmu_template.h
 * NOTE: This must be inlined since the use of GETPC needs to get the
 * return address. Using always inline also works, we use macro here to be more
 * explicit. */
#define CM_GET_QEMU_ADDR(q_addr, v_addr) \
do {                                                                        \
    int __mmu_idx, __index;                                                 \
    CPUState *__env1 = cpu_single_env;                                      \
    void *__retaddr;                                                        \
    __index = (v_addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);            \
    /* get the CPL, hence determine the MMU mode */                         \
    __mmu_idx = cpu_mmu_index(__env1);                                      \
    /* We use this function in the implementation of atomic instructions */ \
    /* and we are going to modify these memory. So we use addr_write. */    \
    if (unlikely(__env1->tlb_table[__mmu_idx][__index].addr_write           \
                != (v_addr & TARGET_PAGE_MASK))) {                          \
        __retaddr = GETPC();                                                \
        tlb_fill(v_addr, 1, __mmu_idx, __retaddr);                          \
    }                                                                       \
    q_addr = v_addr + __env1->tlb_table[__mmu_idx][__index].addend;         \
} while(0)

static target_ulong cm_get_reg_val(int ot, int hregs, int reg)
{
    target_ulong val, offset;
    CPUState *env1 = cpu_single_env;

    switch(ot) {
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
    target_ulong __q_addr;                                    \
    DATA_##type __oldv;                                       \
    DATA_##type value;                                        \
                                                              \
    CM_GET_QEMU_ADDR(__q_addr, vaddr);                        \
    do {                                                      \
        __oldv = value = LD_##type((DATA_##type *)__q_addr);  \
        {command;};                                           \
        mb();                                                 \
    } while (__oldv != (atomic_compare_exchange##type(        \
                    (DATA_##type *)__q_addr, __oldv, value)))

/* Atomically emulate INC instruction using CAS1 and memory transaction. */

#define GEN_ATOMIC_INC(type, TYPE) \
void helper_atomic_inc##type(target_ulong a0, int c)                  \
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
}                                                                     \

GEN_ATOMIC_INC(b, B);
GEN_ATOMIC_INC(w, W);
GEN_ATOMIC_INC(l, L);
GEN_ATOMIC_INC(q, Q);

#define OT_b 0
#define OT_w 1
#define OT_l 2
#define OT_q 3

#define GEN_XCHG(type) \
void helper_xchg##type(target_ulong a0, int reg, int hreg)    \
{                                                             \
    DATA_##type val, out;                                     \
    target_ulong q_addr;                                      \
                                                              \
    CM_GET_QEMU_ADDR(q_addr, a0);                             \
    val = (DATA_##type)cm_get_reg_val(OT_##type, hreg, reg);  \
    out = atomic_exchange##type((DATA_##type *)q_addr, val);  \
    mb();                                                     \
                                                              \
    cm_set_reg_val(OT_##type, hreg, reg, out);                \
}

GEN_XCHG(b);
GEN_XCHG(w);
GEN_XCHG(l);
GEN_XCHG(q);

#define GEN_OP(type, TYPE) \
void helper_atomic_op##type(target_ulong a0, target_ulong t1,    \
                       int op)                                   \
{                                                                \
    DATA_##type operand;                                         \
    int eflags_c, eflags;                                        \
    int cc_op;                                                   \
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
            value -= operand;                                    \
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
            abort();                                             \
            break;                                               \
        }                                                        \
    });                                                          \
    CC_DST = value;                                              \
    /* successful transaction, compute the eflags */             \
    eflags = helper_cc_compute_all(cc_op);                       \
    CC_SRC = eflags;                                             \
}

GEN_OP(b, B);
GEN_OP(w, W);
GEN_OP(l, L);
GEN_OP(q, Q);

/* xadd */
#define GEN_XADD(type, TYPE) \
void helper_atomic_xadd##type(target_ulong a0, int reg,   \
                        int hreg)                         \
{                                                         \
    DATA_##type operand, oldv;                            \
    int eflags;                                           \
                                                          \
    operand = (DATA_##type)cm_get_reg_val(                \
            OT_##type, hreg, reg);                        \
                                                          \
    TX(a0, type, newv, {                                  \
        oldv = newv;                                      \
        newv += operand;                                  \
    });                                                   \
                                                          \
    /* transaction successes */                           \
    /* xchg the register and compute the eflags */        \
    cm_set_reg_val(OT_##type, hreg, reg, oldv);           \
    CC_SRC = oldv;                                        \
    CC_DST = newv;                                        \
                                                          \
    eflags = helper_cc_compute_all(CC_OP_ADD##TYPE);      \
    CC_SRC = eflags;                                      \
}

GEN_XADD(b, B);
GEN_XADD(w, W);
GEN_XADD(l, L);
GEN_XADD(q, Q);

/* cmpxchg */
#define GEN_CMPXCHG(type, TYPE) \
void helper_atomic_cmpxchg##type(target_ulong a0, int reg,       \
                            int hreg)                            \
{                                                                \
    DATA_##type reg_v, eax_v, res;                               \
    int eflags;                                                  \
    target_ulong q_addr;                                         \
                                                                 \
    CM_GET_QEMU_ADDR(q_addr, a0);                                \
    reg_v = (DATA_##type)cm_get_reg_val(OT_##type, hreg, reg);   \
    eax_v = (DATA_##type)cm_get_reg_val(OT_##type, 0, R_EAX);    \
                                                                 \
    res = atomic_compare_exchange##type(                         \
            (DATA_##type *)q_addr, eax_v, reg_v);                \
    mb();                                                        \
                                                                 \
    if (res != eax_v)                                            \
        cm_set_reg_val(OT_##type, 0, R_EAX, res);                \
                                                                 \
    CC_SRC = res;                                                \
    CC_DST = eax_v - res;                                        \
                                                                 \
    eflags = helper_cc_compute_all(CC_OP_SUB##TYPE);             \
    CC_SRC = eflags;                                             \
}

GEN_CMPXCHG(b, B);
GEN_CMPXCHG(w, W);
GEN_CMPXCHG(l, L);
GEN_CMPXCHG(q, Q);

/* cmpxchgb (8, 16) */
void helper_atomic_cmpxchg8b(target_ulong a0)
{
    uint64_t edx_eax, ecx_ebx, res;
    int eflags;
    target_ulong q_addr;

    eflags = helper_cc_compute_all(CC_OP);
    CM_GET_QEMU_ADDR(q_addr, a0);

    edx_eax = (((uint64_t)EDX << 32) | (uint32_t)EAX);
    ecx_ebx = (((uint64_t)ECX << 32) | (uint32_t)EBX);

    res = atomic_compare_exchangeq(
            (uint64_t *)q_addr, edx_eax, ecx_ebx);
    mb();

    if (res == edx_eax) {
         eflags |= CC_Z;
    } else {
        EDX = (uint32_t)(res >> 32);
        EAX = (uint32_t)res;
        eflags &= ~CC_Z;
    }

    CC_SRC = eflags;
}

void helper_atomic_cmpxchg16b(target_ulong a0)
{
    uint8_t res;
    int eflags;
    target_ulong q_addr;

    eflags = helper_cc_compute_all(CC_OP);
    CM_GET_QEMU_ADDR(q_addr, a0);

    uint64_t old_rax = *(uint64_t *)q_addr;
    uint64_t old_rdx = *(uint64_t *)(q_addr + 8);
    res = atomic_compare_exchange16b(
            (uint64_t *)q_addr, EAX, EDX, EBX, ECX);
    mb();

    if (res) {
        eflags |= CC_Z;         /* swap success */
    } else {
        EDX = old_rdx;
        EAX = old_rax;
        eflags &= ~CC_Z;        /* read the old value ! */
    }

    CC_SRC = eflags;
}

/* not */
#define GEN_NOT(type) \
void helper_atomic_not##type(target_ulong a0)  \
{                                              \
    TX(a0, type, value, {                      \
        value = ~value;                        \
    });                                        \
}

GEN_NOT(b);
GEN_NOT(w);
GEN_NOT(l);
GEN_NOT(q);

/* neg */
#define GEN_NEG(type, TYPE) \
void helper_atomic_neg##type(target_ulong a0)        \
{                                                    \
    int eflags;                                      \
                                                     \
    TX(a0, type, value, {                            \
        value = -value;                              \
    });                                              \
                                                     \
    /* We should use the old value to compute CC */  \
    CC_SRC = CC_DST = -value;                        \
                                                     \
    eflags = helper_cc_compute_all(CC_OP_SUB##TYPE); \
    CC_SRC = eflags;                                 \
}                                                    \

GEN_NEG(b, B);
GEN_NEG(w, W);
GEN_NEG(l, L);
GEN_NEG(q, Q);

#define GEN_BTX(ins, command) \
void helper_atomic_##ins(target_ulong a0, target_ulong offset, \
        int ot)                                                \
{                                                              \
    uint8_t old_byte;                                          \
    int eflags;                                                \
                                                               \
    TX(a0, b, value, {                                         \
        old_byte = value;                                      \
        {command;};                                            \
    });                                                        \
                                                               \
    CC_SRC = (old_byte >> offset);                             \
    CC_DST = 0;                                                \
    eflags = helper_cc_compute_all(CC_OP_SARB + ot);           \
    CC_SRC = eflags;                                           \
}

/* bts */
GEN_BTX(bts, {
    value |= (1 << offset);
});
/* btr */
GEN_BTX(btr, {
    value &= ~(1 << offset);
});
/* btc */
GEN_BTX(btc, {
    value ^= (1 << offset);
});

/* fence **/
void helper_fence(void)
{
    mb();
}

/* pause */
void helper_pause(void)
{
    pthread_yield();
}