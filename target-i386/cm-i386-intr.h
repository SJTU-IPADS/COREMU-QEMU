/*
 * COREMU Parallel Emulator Framework
 * Defines qemu related structure and interface for i386 architecture.
 *
 * Copyright (C) 2010 PPI, Fudan Univ. 
 *  <http://ppi.fudan.edu.cn/system_research_group>
 *
 * Authors:
 *  Zhaoguo Wang	<zgwang@fudan.edu.cn>
 *  Yufei Chen 		<chenyufei@fudan.edu.cn>
 *  Ran Liu 		<naruilone@gmail.com>
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

 /* Interrupt types for i386 architecture */
 enum cm_i386_intr_type {
    PIC_INTR,                 /* Interrupt from i8259 pic */
    APICBUS_INTR,               /* Interrupt from APIC BUS 
                                   can be issued by other core or ioapic */
    IPI_INTR,                   /* Interrupt from other core 
                                   Only for de-assert INIT and SIPI */
    DIRECT_INTR,                /* Direct interrupt (SMI) */
    SHUTDOWN_REQ,               /* Shut down request */
 };


/* Interrupt infomation for i8259 pic */
typedef struct CMPicIntrInfo {
    int level;                  /* the level of interrupt */
} CMPicIntrInfo;


/* Interrupt information for IOAPIC */
typedef struct CMAPICBusIntrInfo {
    int mask;                   /* Qemu will use this to check which
                                   kind of interrupt is issued */
    int vector_num;             /* The interrupt vector number
                                   If the vector number is -1, it indicates
                                   the vector information is ignored (SMI, NMI, INIT) */
    int trigger_mode;           /* The trigger mode of interrupt */
} CMAPICBusIntrInfo;


typedef struct CMIPIIntrInfo {
    int vector_num;             /* The interrupt vector number */
    int deliver_mode;           /* The deliver mode of interrupt 
                                   0: INIT Level De-assert 
                                   1: Start up IPI */
} CMIPIIntrInfo;


/* The declaration for apic wrapper function */
void cm_apic_set_irq(struct APICState *s, int vector_num, int trigger_mode);
void cm_apic_startup(struct APICState *s, int vector_num);
void cm_apic_setup_arbid(struct APICState * s);

