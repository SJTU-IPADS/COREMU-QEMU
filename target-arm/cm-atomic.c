/*
 * Copyright (C) 2010 Parallel Processing Institute (PPI), Fudan Univ.
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

/* We include this file in op_helper.c */

#include <stdlib.h>
#include <pthread.h>
#include "coremu-atomic.h"
#include "coremu-sched.h"
#include "cm-mmu.h"
#include "cm-crew.h"

COREMU_THREAD uint64_t cm_exclusive_val;
COREMU_THREAD uint32_t cm_exclusive_addr = -1;

#define DATA_BITS 8
#include "cm-atomic-template.h"

#define DATA_BITS 16
#include "cm-atomic-template.h"

#define DATA_BITS 32
#include "cm-atomic-template.h"

void HELPER(load_exclusiveq)(uint32_t reg, uint32_t addr)
{
    ram_addr_t q_addr = 0;
    uint64_t val = 0;

    cm_exclusive_addr = addr;
    CM_GET_QEMU_ADDR(q_addr,addr);
#ifdef CONFIG_MEM_ORDER
    CM_START_ATOMIC_INSN(q_addr);
#endif
    val = *(uint64_t *)q_addr;
#ifdef CONFIG_MEM_ORDER
    CM_END_ATOMIC_INSN(val);
#endif
    cm_exclusive_val = val;
    cpu_single_env->regs[reg] = (uint32_t)val;
    cpu_single_env->regs[reg + 1] = (uint32_t)(val>>32);
}

void HELPER(store_exclusiveq)(uint32_t res, uint32_t reg, uint32_t addr)
{
    ram_addr_t q_addr = 0;
    uint64_t val = 0;
    uint64_t r = 0;

    if(addr != cm_exclusive_addr)
        goto fail;

    CM_GET_QEMU_ADDR(q_addr,addr);
    val = (uint32_t)cpu_single_env->regs[reg];
    val |= ((uint64_t)cpu_single_env->regs[reg + 1]) << 32;

#ifdef CONFIG_MEM_ORDER
    CM_START_ATOMIC_INSN(q_addr);
#endif
    r = atomic_compare_exchangeq((uint64_t *)q_addr,
            (uint64_t)cm_exclusive_val, val);
#ifdef CONFIG_MEM_ORDER
    CM_END_ATOMIC_INSN(val);
#endif

    if(r == (uint64_t)cm_exclusive_val) {
        cpu_single_env->regs[res] = 0;
        goto done;
    } else {
        goto fail;
    }

fail:
    cpu_single_env->regs[res] = 1;

done:
    cm_exclusive_addr = -1;
    return;
}

void HELPER(clear_exclusive)(void)
{
    cm_exclusive_addr = -1;
}

void HELPER(swpb)(uint32_t dst, uint32_t src, uint32_t addr)
{
    uint8_t old, val;
    ram_addr_t q_addr;
    CM_GET_QEMU_ADDR(q_addr,cpu_single_env->regs[addr]);
    val = (uint8_t)cpu_single_env->regs[src];
    old = atomic_exchangeb((uint8_t *)q_addr, (uint8_t)val);
    cpu_single_env->regs[dst] = old;
    //printf("SWPB\n");
}

void HELPER(swp)(uint32_t dst, uint32_t src, uint32_t addr)
{
    uint32_t old, val;
    ram_addr_t q_addr;
    CM_GET_QEMU_ADDR(q_addr,cpu_single_env->regs[addr]);
    val = cpu_single_env->regs[src];
    old = atomic_exchangel((uint32_t *)q_addr, val);
    cpu_single_env->regs[dst] = old;
    //printf("SWP\n");
}
