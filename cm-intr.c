/*
 * COREMU Parallel Emulator Framework
 * The common interface for hardware interrupt sending and handling
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

#include "cm-intr.h"

/* The common interface to handle the interrupt, this function should to 
   be registered to coremu */
void cm_common_intr_handler(void *opaque)
{
    CMIntr *intr = (CMIntr *)opaque;
    intr->handler(intr->opaque);
}



 

