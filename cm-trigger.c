/*
 * COREMU Parallel Emulator Framework
 *
 * Copyright (C) 2010 Parallel Processing Institute, Fudan Univ.
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
#include "cm-watch.h"

static void print_wpara(CMWParams *wpara)
{
    printf("Watch para : \n");
    if(wpara->is_write)
        printf("Write to ");
    else
        printf("Read from ");
    printf("vaddr[0x%lx] , paddr[0x%x] with value %ld len %ld\n", 
             wpara->vaddr, wpara->paddr, wpara->value, wpara->len);
}

static void test_trigger(void *opaque)
{
    CMWParams *wpara = (CMWParams *)opaque;
    print_wpara(wpara);
}

void cm_wtriger_init(void)
{
    cm_register_wtrigger_func(10, test_trigger);
}
