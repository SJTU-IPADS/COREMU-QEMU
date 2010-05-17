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
#include "cpu.h"

#include "cm-i386-intr.h"

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
}


/* Handle the interrupt from the apic bus.
   Because hardware connect to ioapic and inter-processor interrupt 
   are all delivered through apic bus, so this kind of interrupt can
   be hw interrupt or IPI */
void cm_apicbus_intr_handler(void *opaque)
{
   CMAPICBusIntrInfo *apicbus_intr = (CMAPICBusIntrInfo *)opaque;

   CPUState *self = cpu_single_env;
   
   if(apicbus_intr->vector_num >= 0) {
        cm_apic_set_irq(self->apic_state, 
                            apicbus_intr->vector_num, apicbus_intr->trigger_mode);
   } else {
   /* for NMI, SMI and INIT the vector information is ignored*/
        cpu_interrupt(self, apicbus_intr->mask);
   }
}


/* Handle the inter-processor interrupt (Only for INIT De-assert or SIPI) */
void cm_ipi_intr_handler(void *opaque)
{
    CMIPIIntrInfo *ipi_intr = (CMIPIIntrInfo *)opaque;

    CPUState *self = cpu_single_env;

    if(ipi_intr->deliver_mode) {
    /* SIPI */
        cm_apic_startup(self->apic_state, ipi_intr->vector_num);
    } else {
    /* the INIT level de-assert */
        cm_apic_setup_arbid(self->apic_state);
    }
    
}
