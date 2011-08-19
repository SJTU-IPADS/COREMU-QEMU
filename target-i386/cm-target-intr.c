/*
 * COREMU Parallel Emulator Framework
 * The definition of interrupt related interface for i386
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
#include <pthread.h>
#include "cpu.h"
#include "exec-all.h"
#include "../hw/apic.h"

#include "coremu-intr.h"
#include "coremu-malloc.h"
#include "coremu-atomic.h"
#include "cm-intr.h"
#include "cm-target-intr.h"
#include "cm-replay.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

/* Handle the interrupt from the i8259 chip */
static void cm_pic_intr_handler(void *opaque)
{
    CMPICIntr *pic_intr = (CMPICIntr *) opaque;

    CPUState *self = cpu_single_env;
    int level = pic_intr->level;

    if (self->apic_state) {
        if (apic_accept_pic_intr(self->apic_state))
            apic_deliver_pic_intr(self->apic_state, pic_intr->level);
    } else {
        if (level)
            cpu_interrupt(self, CPU_INTERRUPT_HARD);
        else
            cpu_reset_interrupt(self, CPU_INTERRUPT_HARD);
    }
}

/* Handle the interrupt from the apic bus.
   Because hardware connect to ioapic and inter-processor interrupt
   are all delivered through apic bus, so this kind of interrupt can
   be hw interrupt or IPI */
static void cm_apicbus_intr_handler(void *opaque)
{
    CMAPICBusIntr *apicbus_intr = (CMAPICBusIntr *)opaque;

    CPUState *self = cpu_single_env;

    if (apicbus_intr->vector_num >= 0) {
        cm_apic_set_irq(self->apic_state, apicbus_intr->vector_num,
                apicbus_intr->trigger_mode);
    } else {
        /* For NMI, SMI and INIT the vector information is ignored */
        cpu_interrupt(self, apicbus_intr->mask);
    }
#ifdef CONFIG_REPLAY
    assert(cm_is_in_tc == 0);
    /* Ignore timer interrupt in Linux. Note local apic timer is 0xef.  */
    if (apicbus_intr->vector_num != 0x30)
        atomic_incq((uint64_t *)&cm_intr_handler_cnt);
    coremu_debug("cm_coreid = %u cm_intr_handler_cnt = %lu vector_num = 0x%x",
                 cm_coreid, cm_intr_handler_cnt, apicbus_intr->vector_num);
#endif
}

/* Handle the inter-processor interrupt (Only for INIT De-assert or SIPI) */
static void cm_ipi_intr_handler(void *opaque)
{
    CMIPIIntr *ipi_intr = (CMIPIIntr *)opaque;

    CPUState *self = cpu_single_env;

    if (ipi_intr->deliver_mode) {
        /* SIPI */
        /*coremu_debug("core %u handling START IPI", cm_coreid);*/
        cm_apic_startup(self->apic_state, ipi_intr->vector_num);
    } else {
        /* the INIT level de-assert */
        /*coremu_debug("core %u handling INIT IPI", cm_coreid);*/
        cm_apic_setup_arbid(self->apic_state);
    }
#ifdef CONFIG_REPLAY
    cm_intr_handler_cnt++;
#endif
}

/* Handler the tlb flush request */
static void cm_tlb_flush_req_handler(void *opaque)
{
    tlb_flush(cpu_single_env, 1);
}

static void cm_exit_intr_handler(void *opaque)
{
    coremu_debug("exiting");
    cm_replay_flush_log(cm_coreid);
#ifdef CONFIG_REPLAY
    cm_print_replay_info();
#endif
    pthread_exit(NULL);
}

/* The initial function for interrupts */

static CMIntr *cm_pic_intr_init(int level)
{
    CMPICIntr *intr = coremu_mallocz(sizeof(*intr));
    ((CMIntr *)intr)->handler = cm_pic_intr_handler;

    intr->level = level;

    return (CMIntr *)intr;
}

static CMIntr *cm_apicbus_intr_init(int mask, int vector_num, int trigger_mode)
{
    CMAPICBusIntr *intr = coremu_mallocz(sizeof(*intr));
    ((CMIntr *)intr)->handler = cm_apicbus_intr_handler;

    intr->mask = mask;
    intr->vector_num = vector_num;
    intr->trigger_mode = trigger_mode;

    return (CMIntr *)intr;
}

static CMIntr *cm_ipi_intr_init(int vector_num, int deliver_mode)
{
    CMIPIIntr *intr = coremu_mallocz(sizeof(*intr));
    ((CMIntr *)intr)->handler = cm_ipi_intr_handler;

    intr->vector_num = vector_num;
    intr->deliver_mode = deliver_mode;

    return (CMIntr *)intr;
}

static CMIntr *cm_tlb_flush_req_init(void)
{
    CMTLBFlushReq *intr = coremu_mallocz(sizeof(*intr));
    ((CMIntr *)intr)->handler = cm_tlb_flush_req_handler;

    return (CMIntr *)intr;
}

void cm_send_pic_intr(int target, int level)
{
    coremu_send_intr(cm_pic_intr_init(level), target);
}

void cm_send_apicbus_intr(int target, int mask,
                          int vector_num, int trigger_mode)
{
    coremu_send_intr(cm_apicbus_intr_init(mask, vector_num, trigger_mode),
                     target);
}

void cm_send_ipi_intr(int target, int vector_num, int deliver_mode)
{
    coremu_send_intr(cm_ipi_intr_init(vector_num, deliver_mode), target);
}

void cm_send_tlb_flush_req(int target)
{
    assert(0);
    coremu_send_intr(cm_tlb_flush_req_init(), target);
}

void cm_send_exit_intr(int target)
{
    CMExitIntr *intr = coremu_mallocz(sizeof(*intr));
    ((CMIntr *)intr)->handler = cm_exit_intr_handler;

    coremu_send_intr(intr, target);
}

