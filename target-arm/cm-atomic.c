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
   val = *(uint64_t *)q_addr;
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

   r = atomic_compare_exchangeq((uint64_t *)q_addr,
                                    (uint64_t)cm_exclusive_val, val);

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

static struct timeval tv[2];

static int cm_time_backdoor_cnt;

void HELPER(swpb)(uint32_t dst, uint32_t src, uint32_t addr)
{
    /*
     *uint8_t old, val;
     *ram_addr_t q_addr;
     *CM_GET_QEMU_ADDR(q_addr,cpu_single_env->regs[addr]);
     *val = (uint8_t)cpu_single_env->regs[src];
     *old = atomic_exchangeb((uint8_t *)q_addr, (uint8_t)val);
     *cpu_single_env->regs[dst] = old;
     */
    int i = cm_time_backdoor_cnt++ & 1;
    if (gettimeofday(&tv[i], NULL) != 0) {
        printf("Error in gettimeofday, in time backdoor!\n");
    }
    if (i == 1) {
        time_t sec = tv[1].tv_sec - tv[0].tv_sec;
        suseconds_t usec = 0;
        if (tv[1].tv_usec >= tv[0].tv_usec) {
            usec = tv[1].tv_usec - tv[0].tv_usec;
        } else {
            sec--;
            usec = tv[1].tv_usec + 1000000 - tv[0].tv_usec;
        }
        printf("\n==========\nCOREMU HOST TIME: %d.%03d seconds\n==========\n", (int)sec, (int)(usec / 1000));
    } else {
        printf("\n==========\nCOREMU TIMING START\n==========\n");
    }
}

void HELPER(swp)(uint32_t dst, uint32_t src, uint32_t addr)
{
    /*
     *uint32_t old, val;
     *ram_addr_t q_addr;
     *CM_GET_QEMU_ADDR(q_addr,cpu_single_env->regs[addr]);
     *val = cpu_single_env->regs[src];
     *old = atomic_exchangel((uint32_t *)q_addr, val);
     *cpu_single_env->regs[dst] = old;
     */
    int i = cm_time_backdoor_cnt++ & 1;
    if (gettimeofday(&tv[i], NULL) != 0) {
        printf("Error in gettimeofday, in time backdoor!\n");
    }
    if (i == 1) {
        time_t sec = tv[1].tv_sec - tv[0].tv_sec;
        suseconds_t usec = 0;
        if (tv[1].tv_usec >= tv[0].tv_usec) {
            usec = tv[1].tv_usec - tv[0].tv_usec;
        } else {
            sec--;
            usec = tv[1].tv_usec + 1000000 - tv[0].tv_usec;
        }
        printf("\n==========\nCOREMU HOST TIME: %d.%03d seconds\n==========\n", (int)sec, (int)(usec / 1000));
    } else {
        printf("\n==========\nCOREMU TIMING START\n==========\n");
    }
}
