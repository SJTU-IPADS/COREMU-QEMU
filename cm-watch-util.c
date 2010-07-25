/*
 * COREMU Parallel Emulator Framework
 *
 * Utils for cwatcher.
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

#include "dyngen-exec.h"
#include "cpu.h"
#include "cm-mmu.h"
#include "cm-watch-util.h"

#define env cpu_single_env
#include "softmmu_exec.h"

void cm_dump_stack(unsigned long ebp, int level)
{
    unsigned long qaddr; /* Address in qemu. */
    unsigned long retaddr;
    int i;

    for (i = 0; i < level && ebp; i++) {
        /* XXX Are we calling this in helper function? If so, this function
         * needs to be inlined. */
        CM_GET_QEMU_ADDR(qaddr, ebp); /* Get the address pointed by ebp. */

        retaddr = *((unsigned long *)(qaddr) + 1);

        /* TODO: store backtrace info in other places. */
        printf("CYF: backtrace: %p\n", (void *)retaddr);
        ebp = *(unsigned long *)qaddr;
    }
}

