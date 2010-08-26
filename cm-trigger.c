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
#include "cm-watch-util.h"
#include "coremu-atomic.h"
#include "coremu-logbuffer.h"

#if 0
static void test_trigger(void *opaque)
{
    CMWParams *wpara = (CMWParams *)opaque;
    print_wpara(wpara);
}
#endif

#include <dlfcn.h>
static const char *trigger_so_name = "./usertrigger.so";
void *cm_trigger_handle;

void cm_wtrigger_buf_init(void)
{   
    void (*log_buf_init_p) (void);

    if (!cm_trigger_handle)
        return;
    log_buf_init_p = dlsym(cm_trigger_handle, "log_buffer_init");
    log_buf_init_p();
}

void cm_wtrigger_buf_flush(void)
{
    void (*log_buf_flush_p) (void);

    if (!cm_trigger_handle)
        return;
    log_buf_flush_p = dlsym(cm_trigger_handle, "log_buffer_flush");
    log_buf_flush_p();
}

void cm_wtrigger_init(void)
{
    void (*trigger_init_p) (void);
    
    /* Open the trigger lib here */
    cm_trigger_handle = dlopen(trigger_so_name, RTLD_LAZY);
    if (!cm_trigger_handle) {
        fprintf(stderr, "%s\n", dlerror());
        printf("COREMU WARNING: No trigger dynamic link file is found.\n");
        return;
    }
    dlerror();

    trigger_init_p = dlsym(cm_trigger_handle, "trigger_init");
    trigger_init_p();
}

