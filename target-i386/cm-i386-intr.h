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
    I8259_INTR,                 /* Interrupt from i8259 pic */
    IOAPIC_INTR,                /* Interrupt from IOAPIC */
    IPI_INTR,                   /* Interrupt from other core */
    DIRECT_INTR,                /* Direct interrupt (SMI) */
    SHUTDOWN_REQ,               /* Shut down request */
 };

/* Interrupt infomation for i8259 pic */
typedef struct CMPicIntrInfo {
    int level;                  /* the level of interrupt */
} CMPicIntrInfo;

/* Interrupt information for IOAPIC */
typedef struct CMIOAPICIntrInfo {
    
} CMIOAPICIntrInfo;

