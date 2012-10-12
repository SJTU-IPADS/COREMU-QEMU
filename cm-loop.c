/*
 * COREMU Parallel Emulator Framework
 * The definition of core thread function
 *
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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include "cpu.h"
#include "cpus.h"

#include "coremu-config.h"
#include "coremu-sched.h"
#include "coremu-core.h"
#include "cm-loop.h"
#include "cm-timer.h"
#include "cm-init.h"
#include "cm-intr.h"
#include "cm-replay.h"
#include "cm-crew.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

int cm_cpu_can_run(CPUState *);
static bool cm_tcg_cpu_exec(void);
static bool cm_tcg_cpu_exec(void)
{
    int ret = 0;
    CPUState *env = cpu_single_env;

    for (;;) {
        if (cm_local_alarm_pending())
            cm_run_all_local_timers();

        cm_receive_intr();
        if (cm_cpu_can_run(env))
            ret = cpu_exec(env);
        else if (env->stop)
            break;

#if defined(CONFIG_MEM_ORDER) && defined(LAZY_LOCK_RELEASE)
        cm_release_contending_memobj();
#endif

        if (!cm_vm_can_run())
            break;

        if (ret == EXCP_DEBUG) {
            coremu_assert(0, "debug support hasn't been finished\n");
            break;
        }
        if (ret == EXCP_HALTED || ret == EXCP_HLT) {
            coremu_cpu_sched(CM_EVENT_HALTED);
        }
    }
    return ret;
}

void *cm_cpu_loop(void *args)
{
    /* Must initialize cpu_single_env before initializing core thread. */
    assert(args);
    cpu_single_env = (CPUState *)args;

    /* Setup dynamic translator */
    cm_cpu_exec_init_core();

    for (;;) {
        cm_tcg_cpu_exec();
        if (cm_test_reset_request()) {
            coremu_pause_core();
            continue;
        }
        break;
    }
    cm_stop_local_timer();
    coremu_core_exit(NULL);
    assert(0);
    return NULL;
}
