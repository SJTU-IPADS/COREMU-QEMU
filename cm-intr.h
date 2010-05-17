/*
 * COREMU Parallel Emulator Framework
 * Defines qemu related structure and interface for hardware interrupt
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
#ifndef CM_INTR_H
#define CM_INTR_H

/* This is the call back function used to handle different type interrupts */
typedef void (*CMIntr_handler)(void *opaque);

/* This structure is used to wrap the interrupt request between different core 
   thread and hardware thread. Opaque's type is depend on different sources and 
   target architectures. */
typedef struct CMIntr {
    int source;
    void *opaque;
    CMIntr_handler handler;
} CMIntr;



#endif
