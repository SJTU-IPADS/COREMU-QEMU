/*
 * COREMU Parallel Emulator Framework
 * The definition of interrupt related interface for i386
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
#include "cpu.h"

#include "coremu-intr.h"
#include "coremu-malloc.h"
#include "cm-intr.h"
#include "cm-i386-intr.h"

/* The initial function for interrupts */
CMIntr *cm_pic_intr_init(CMPicIntrInfo *pic_intr)
{
    CMIntr *intr = coremu_mallocz(sizeof(CMIntr));
    intr->source = PIC_INTR;
    intr->opaque = pic_intr;
    intr->handler = cm_pic_intr_handler;

    return intr;
}

CMIntr *cm_apicbus_intr_init(CMAPICBusIntrInfo *apicbus_intr)
{
    CMIntr *intr = coremu_mallocz(sizeof(CMIntr));
    intr->source = APICBUS_INTR;
    intr->opaque = apicbus_intr;
    intr->handler = cm_apicbus_intr_handler;

    return intr;
}

CMIntr *cm_ipi_intr_init(CMIPIIntrInfo *ipi_intr)
{
    CMIntr *intr = coremu_mallocz(sizeof(CMIntr));
    intr->source = IPI_INTR;
    intr->opaque = ipi_intr;
    intr->handler = cm_ipi_intr_handler;

    return intr;
}

void cm_send_pic_intr(int target, int level)
{
    CMIntr *intr;
    CMPicIntrInfo *picintr;

    /* malloc pic interrupt */
    picintr = coremu_mallocz(sizeof(CMPicIntrInfo));
    picintr->level = level;
    intr = cm_pic_intr_init(picintr);

    /* send the intr to the target core thread */
    coremu_send_intr(intr, target);
}

void cm_send_apicbus_intr(int target, int mask,
                                int vector_num, int trigger_mode)
{
    CMIntr *intr;
    CMAPICBusIntrInfo *apicintr;

    /* malloc apic bus interrupt */
    apicintr = coremu_mallocz(sizeof(CMAPICBusIntrInfo));
    apicintr->vector_num = vector_num;
    apicintr->trigger_mode = trigger_mode;
    apicintr->mask = mask;
    intr = cm_apicbus_intr_init(apicintr);

    /* send the intr to core thr */
    coremu_send_intr(intr, target);
}


void cm_send_ipi_intr(int target, int vector_num, int deliver_mode)
{
    CMIntr *intr;
    CMIPIIntrInfo *ipiintr;

    /* malloc ipi bus interrupt */
    ipiintr = coremu_mallocz(sizeof(CMIPIIntrInfo));
    ipiintr->vector_num = vector_num;
    ipiintr->deliver_mode = deliver_mode;
    intr = cm_ipi_intr_init(ipiintr);

    /* send the intr to core thr */
    coremu_send_intr(intr, target);
}


/* Handle the interrupt from the i8259 chip */
void cm_pic_intr_handler(void *opaque)
{
    CMPicIntrInfo *pic_intr = (CMPicIntrInfo *)opaque;

    CPUState *self = cpu_single_env;
    int level = pic_intr->level;

    if (self->apic_state) {
        if (apic_accept_pic_intr(self))
            apic_deliver_pic_intr(self, pic_intr->level);
    } else {
        if (level)
            cpu_interrupt(self, CPU_INTERRUPT_HARD);
        else
            cpu_reset_interrupt(self, CPU_INTERRUPT_HARD);
    }
    coremu_free(pic_intr);
}


/* Handle the interrupt from the apic bus.
   Because hardware connect to ioapic and inter-processor interrupt
   are all delivered through apic bus, so this kind of interrupt can
   be hw interrupt or IPI */
void cm_apicbus_intr_handler(void *opaque)
{
   CMAPICBusIntrInfo *apicbus_intr = (CMAPICBusIntrInfo *)opaque;

   CPUState *self = cpu_single_env;

   if (apicbus_intr->vector_num >= 0) {
        cm_apic_set_irq(self->apic_state,
                            apicbus_intr->vector_num, apicbus_intr->trigger_mode);
   } else {
   /* For NMI, SMI and INIT the vector information is ignored*/
        cpu_interrupt(self, apicbus_intr->mask);
   }
   coremu_free(apicbus_intr);
}


/* Handle the inter-processor interrupt (Only for INIT De-assert or SIPI) */
void cm_ipi_intr_handler(void *opaque)
{
    CMIPIIntrInfo *ipi_intr = (CMIPIIntrInfo *)opaque;

    CPUState *self = cpu_single_env;

    if (ipi_intr->deliver_mode) {
    /* SIPI */
        cm_apic_startup(self->apic_state, ipi_intr->vector_num);
    } else {
    /* the INIT level de-assert */
        cm_apic_setup_arbid(self->apic_state);
    }
    coremu_free(ipi_intr);
}

