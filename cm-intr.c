/*
 * COREMU Parallel Emulator Framework
 *
 * The common interface for hardware interrupt sending and handling.
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
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "cpu.h"

#include "coremu-config.h"
#include "coremu-core.h"
#include "coremu-malloc.h"
#include "coremu-intr.h"
#include "cm-intr.h"
#include "cm-replay.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

/* The common interface to handle the interrupt, this function should to
   be registered to coremu */
void cm_common_intr_handler(CMIntr *intr)
{
#ifdef CONFIG_REPLAY
    /* XXX We have to ignore timer interrupt since we do not enable timer during
     * replay, so the intr handler execution counter is updated in specific
     * handler. */
    /*cm_intr_handler_cnt++;*/
#endif
    /*assert(cm_run_mode != CM_RUNMODE_REPLAY);*/
    coremu_assert_core_thr();
    if (!intr)
        return;
    intr->handler(intr);
    coremu_free(intr);
}

/* To notify there is an event coming, what qemu need to do is
   just exit current cpu loop */
void cm_notify_event(void)
{
    if (cpu_single_env)
        cpu_exit(cpu_single_env);
}

void cm_receive_intr(void)
{
    /*if (cm_run_mode != CM_RUNMODE_REPLAY)*/
        coremu_receive_intr();
}

static void cm_exit_intr_handler(void *opaque)
{
    coremu_debug("exiting");
    cm_replay_flush_log(cm_coreid);
    cm_crew_core_finish();
#ifdef CONFIG_REPLAY
    cm_print_replay_info();
#endif
    pthread_exit(NULL);
}

void cm_send_exit_intr(int target)
{
    CMExitIntr *intr = coremu_mallocz(sizeof(*intr));
    ((CMIntr *)intr)->handler = cm_exit_intr_handler;

    coremu_send_intr(intr, target);
}

