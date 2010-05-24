/*
 * COREMU Parallel Emulator Framework
 * The definition of core thread function
 *
 * Copyright (C) 2010 PPI, Fudan Univ.
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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "cpu.h"
#include "cpus.h"

#include "cm-loop.h"
#include "cm-timer.h"
#include "coremu-intr.h"
#include "coremu-debug.h"

static bool cm_tcg_cpu_exec(void);
static bool cm_tcg_cpu_exec(void)
{
    int ret = 0;
    CPUState *env = cpu_single_env;
    struct timespec halt_interval;
    halt_interval.tv_sec = 0;
    halt_interval.tv_nsec = 10000;

    for (;;) {
        if (cm_local_alarm_pending())
            cm_run_all_local_timers();

        coremu_receive_intr();
        if (cm_cpu_can_run(env))
            ret = cpu_exec(env);
        else if (env->stop)
            break;

        if (ret == EXCP_DEBUG) {
            cm_assert(0, "debug support hasn't been finished\n");
            break;
        }

        if (ret == EXCP_HALTED || ret == EXCP_HLT) {
            nanosleep(&halt_interval, NULL);
        }
    }
    //return tcg_has_work();
    return 1;
}

void *cm_cpu_loop(void *args)
{
    /* do some initialization */
    /* Need to initial per cpu timer */
    cpu_single_env = (CPUState *)args;
    assert(cpu_single_env);

    int res;
    res = cm_init_local_timer_alarm();
    if (res < 0) {
        printf("initialize local alarm failed\n");
        cm_assert(0, "local alarm initialize error");
    }

    /* not complete */
    int ret;

    for (;;) {
        do {
            ret = cm_tcg_cpu_exec();
            cm_assert(ret, "CPU Stop mechanism hasn't been implemented!\n");
        } while (cm_vm_can_run());

        cm_assert(0, "not finish here\n");
    }
    pause_all_vcpus();
}

