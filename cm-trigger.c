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

typedef struct pipe_bug_info {
    target_ulong vnum;
    target_ulong eip;
    target_ulong stack;
    target_ulong vaddr;
    int write;
    int cpu;
} pipe_bug_info;

static __thread CMLogbuf *pipe_bug_buf;
static void pipe_bug_record_log(FILE * log_file, void *opaque)
{
    pipe_bug_info *info = (pipe_bug_info *)opaque;
    fprintf(log_file, "[%016ld] WRITE[%d] VADDR[0x%lx] EIP[0x%lx] Process[0x%lx] CPU[%d]\n",
            info->vnum, info->write , info->vaddr, info->eip, info->stack, info->cpu);
}

static void pipe_bug_trigger(void *opaque)
{
    static target_ulong inc = 0;
    target_ulong version_num = 1;
    atomic_xaddq((uint64_t *)&version_num, (uint64_t *)&inc);
    CMWParams *wpara = (CMWParams *)opaque;
    if (wpara->value == 0) {
        COREMU_LOGBUF_LOG(pipe_bug_buf, record, {
            pipe_bug_info * info = (pipe_bug_info *)record;
            info->vnum = version_num;
            info->eip = cm_get_cpu_eip();
            info->cpu = cm_get_cpu_idx();
            info->stack = cm_get_stack_page_addr();
            info->vaddr = wpara->vaddr;
            info->write = wpara->is_write;
        });
    }
}

static void pipe_bug_trigger_buf_init(void)
{
    char pathname[100];
    sprintf(pathname, "pipe_bug_log%d", cpu_single_env->cpu_index);
    FILE *file = fopen(pathname,"w");
    pipe_bug_buf = coremu_logbuf_new(100, sizeof(pipe_bug_info), 
                                        pipe_bug_record_log, file);
}

static void pipe_bug_trigger_buf_flush(void)
{
    printf("cpu[%d] flush buffer\n", cpu_single_env->cpu_index);
    COREMU_LOGBUF_LOG(pipe_bug_buf, record, {
            pipe_bug_info * info = (pipe_bug_info *)record;
            info->vnum = -1;
            info->eip = 0;
            info->cpu = cpu_single_env->cpu_index;
            info->stack = 0;
            info->vaddr = 0;
            info->write = 0;
        });
    coremu_logbuf_flush(pipe_bug_buf);
    coremu_logbuf_wait_flush(pipe_bug_buf);
    fflush(pipe_bug_buf->file);
    printf("cpu[%d] finish flush buffer\n", cpu_single_env->cpu_index);
}

typedef struct pbzip2_bug_info {
    target_ulong vnum;
    target_ulong eip;
    target_ulong stack;
    target_ulong vaddr;
    int write;
    int cpu;
} pbzip2_bug_info;

static __thread CMLogbuf *pbzip2_bug_buf;
static void pbzip2_bug_record_log(FILE * log_file, void *opaque)
{
    pbzip2_bug_info *info = (pbzip2_bug_info *)opaque;
    fprintf(log_file, "[%016ld] WRITE[%d] VADDR[0x%lx] EIP[0x%lx] Process[0x%lx] CPU[%d]\n",
            info->vnum, info->write , info->vaddr, info->eip, info->stack, info->cpu);
}

static void pbzip2_bug_trigger(void *opaque)
{
    static target_ulong inc = 0;
    target_ulong version_num = 1;
    atomic_xaddq((uint64_t *)&version_num, (uint64_t *)&inc);
    CMWParams *wpara = (CMWParams *)opaque;
    if (wpara->value == 0) {
        COREMU_LOGBUF_LOG(pbzip2_bug_buf, record, {
            pbzip2_bug_info * info = (pbzip2_bug_info *)record;
            info->vnum = version_num;
            info->eip = cm_get_cpu_eip();
            info->cpu = cm_get_cpu_idx();
            info->stack = cm_get_stack_page_addr();
            info->vaddr = wpara->vaddr;
            info->write = wpara->is_write;
        });
    }
}

static void pbzip2_bug_trigger_buf_init(void)
{
    char pathname[100];
    sprintf(pathname, "pbzip2_bug_log%d", cpu_single_env->cpu_index);
    FILE *file = fopen(pathname,"w");
    pbzip2_bug_buf = coremu_logbuf_new(100, sizeof(pbzip2_bug_info), 
                                        pbzip2_bug_record_log, file);
}

static void pbzip2_bug_trigger_buf_flush(void)
{
    printf("cpu[%d] flush buffer\n", cpu_single_env->cpu_index);
    COREMU_LOGBUF_LOG(pbzip2_bug_buf, record, {
            pbzip2_bug_info * info = (pbzip2_bug_info *)record;
            info->vnum = -1;
            info->eip = 0;
            info->cpu = cpu_single_env->cpu_index;
            info->stack = 0;
            info->vaddr = 0;
            info->write = 0;
        });
    coremu_logbuf_flush(pbzip2_bug_buf);
    coremu_logbuf_wait_flush(pbzip2_bug_buf);
    fflush(pbzip2_bug_buf->file);
    printf("cpu[%d] finish flush buffer\n", cpu_single_env->cpu_index);
}

void cm_wtrigger_buf_init(void)
{
    //pipe_bug_trigger_buf_init();
    pbzip2_bug_trigger_buf_init();
}

void cm_wtrigger_buf_flush(void)
{
    //pipe_bug_trigger_buf_flush();
    pbzip2_bug_trigger_buf_flush();
}

void cm_wtrigger_init(void)
{
    //cm_register_wtrigger_func(0, pipe_bug_trigger);
    cm_register_wtrigger_func(1, pbzip2_bug_trigger);
}
