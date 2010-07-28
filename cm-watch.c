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
#include <stdbool.h>
#include "cm-watch.h"
#define env cpu_single_env
#include "softmmu_exec.h"
#include "coremu-atomic.h"
#include "coremu-malloc.h"
#include "coremu-intr.h"
#include "coremu-hw.h"
#include "coremu-sched.h"
#include "cm-intr.h"
#include "queue.h"

static int cm_watch_index;
static CMWatchPage *cm_watch_p;
static long wramoffset;

#define inline __attribute__ (( always_inline )) __inline__
static inline queue_t* cm_get_watch_queue(ram_addr_t addr)
{
    return &cm_watch_p[addr >> TARGET_PAGE_BITS].cm_watch_q;
}

static inline bool cm_is_watch_hit(ram_addr_t raddr, int size, CMWatchEntry *entry)
{
    ram_addr_t wraddr;
    target_ulong wlen;
    wraddr = entry->cm_wrange.ram_addr_offset;
    wlen = entry->cm_wrange.len;
    return (((raddr >= wraddr) && (raddr < (wraddr + wlen))) 
            || ((wraddr >= raddr) && (wraddr < (raddr + size))));
}

static inline void cm_check_watch_paddr(target_phys_addr_t addr, int size, uint32_t value, int is_write)
{
    ram_addr_t ram_addr_offset = cpu_get_physical_page_desc(addr);
    QUEUE_FOREACH(cm_get_watch_queue(ram_addr_offset), entry_p, {
        CMWatchEntry *entry = *(CMWatchEntry **)entry_p;
        if (cm_is_watch_hit(ram_addr_offset, size, entry))
            entry->cm_wtrigger(NULL);
    });
}

static uint32_t cm_watch_mem_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = ldub_phys(addr);
    cm_check_watch_paddr(addr, 1, value, 0);
    return value;
}

static uint32_t cm_watch_mem_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = lduw_phys(addr);
    cm_check_watch_paddr(addr, 2, value, 0);
    return value;
}

static uint32_t cm_watch_mem_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = ldl_phys(addr);
    cm_check_watch_paddr(addr, 4, value, 0);
    return value;
}

static void cm_watch_mem_writeb(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    cm_check_watch_paddr(addr, 1, val, 1);
    stb_phys(addr, val);
}

static void cm_watch_mem_writew(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    cm_check_watch_paddr(addr, 2, val, 1);
    stw_phys(addr, val);
}

static void cm_watch_mem_writel(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    cm_check_watch_paddr(addr, 4, val, 1);
    stl_phys(addr, val);
}

static CPUReadMemoryFunc * const cm_watch_mem_read[3] = {
    cm_watch_mem_readb,
    cm_watch_mem_readw,
    cm_watch_mem_readl,
};

static CPUWriteMemoryFunc * const cm_watch_mem_write[3] = {
    cm_watch_mem_writeb,
    cm_watch_mem_writew,
    cm_watch_mem_writel,
};

static inline ram_addr_t cm_get_page_addr(CPUState *env1, target_ulong addr)
{
    int mmu_idx, page_index;
    void *p;

    page_index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    mmu_idx = cpu_mmu_index(env1);
    if (unlikely(env1->tlb_table[mmu_idx][page_index].addr_read !=
                 (addr & TARGET_PAGE_MASK))) {
        ldub(addr);
    }

    p = (void *)(unsigned long)addr
        + env1->tlb_table[mmu_idx][page_index].addend;
    return qemu_ram_addr_from_host(p);
}

static void cm_watch_page_init(CMWatchPage *wpage)
{
    wpage->cnt = 0;
    queue_t *q = new_queue();
    uint64_t res = atomic_compare_exchangeq((uint64_t *)(&wpage->cm_watch_q),
                                                (uint64_t)0, (uint64_t)q);
    if (res != (uint64_t)NULL)
        destroy_queue(q);
}

static CMWatchEntry *cm_insert_watch_entry(queue_t *q, CMTriggerID id,
        ram_addr_t ram_addr_offset, target_ulong start, target_ulong len)
{
    CMWatchEntry *new_wentry = coremu_mallocz(sizeof(CMWatchEntry));
    new_wentry->cm_wrange.ram_addr_offset = ram_addr_offset;
    new_wentry->cm_wrange.vaddr = len;
    new_wentry->cm_invalidate_flag = 0;
    // XXX: cm_wtrigger = trigger_func[id];
    new_wentry->cm_wtrigger = NULL;
    enqueue(q, (long)new_wentry);
    return new_wentry;
}

static inline void cm_tlb_set_watch(unsigned long vaddr, ram_addr_t ram_addr_offset)
{
    CPUState *self;
    int mmu_idx, idx;
    self = cpu_single_env;
    idx = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        if(((vaddr & TARGET_PAGE_MASK) == (env->tlb_table[mmu_idx][idx].addr_read &
                                          (TARGET_PAGE_MASK | TLB_INVALID_MASK))) ||
           ((vaddr & TARGET_PAGE_MASK) == (env->tlb_table[mmu_idx][idx].addr_write &
                                          (TARGET_PAGE_MASK | TLB_INVALID_MASK)))) {
            env->tlb_table[mmu_idx][idx].addr_read = -1;
            env->tlb_table[mmu_idx][idx].addr_write = -1;
        }
    }

}

static inline void cm_tlb_set_unwatch(unsigned long vaddr, ram_addr_t ram_addr_offset)
{
    //not complete
}

static void cm_insert_watch_req_handler(void *opaque)
{
    CMWatchReq *wreq = (CMWatchReq *)opaque;

    if(wreq->flag) {
        cm_tlb_set_watch(wreq->cm_wrange.vaddr, wreq->cm_wrange.ram_addr_offset);
        tb_flush(cpu_single_env);
    } else {
        cm_tlb_set_unwatch(wreq->cm_wrange.vaddr, wreq->cm_wrange.ram_addr_offset);
    }
}


static CMIntr *cm_watch_req_init(CMWatchAddrRange *range, int flag)
{
    CMWatchReq *req = coremu_mallocz(sizeof(*req));
    ((CMIntr *)req)->handler = cm_insert_watch_req_handler;

    req->cm_wrange = *range;
    req->flag = flag;

    return (CMIntr *)req;
}

static void cm_send_watch_req(int target, CMWatchAddrRange *range, int flag)
{
    coremu_send_intr(cm_watch_req_init(range, flag), target);
}

void cm_watch_init(ram_addr_t ram_offset, ram_addr_t size)
{
    coremu_assert_hw_thr("cm_watch_init should only called by hw thr");

    cm_watch_p = coremu_realloc(cm_watch_p,
                                    ((ram_offset +
                                      size) >> TARGET_PAGE_BITS) *
                                    sizeof(CMWatchPage));
    memset(cm_watch_p + (ram_offset >> TARGET_PAGE_BITS), 0x0,
           (size >> TARGET_PAGE_BITS) * sizeof(CMWatchPage));

    cm_watch_index = cpu_register_io_memory(cm_watch_mem_read,
                                            cm_watch_mem_write, NULL);
}

void cm_insert_watch_point(CMTriggerID id, target_ulong start, target_ulong len)
{
    CPUState *self;
    ram_addr_t ram_start;
    CMWatchPage *wpage;
    target_ulong cnt;
    int cpu_idx;

    self = cpu_single_env;
    ram_start = cm_get_page_addr(self, start);
    wramoffset = ram_start;
    wpage = &cm_watch_p[ram_start >> TARGET_PAGE_BITS];

    printf("%s : id[%ld] start[0x%lx] len[%ld] phys-start[0x%lx]\n",
                        __FUNCTION__, id, start, len, ram_start);

    if (!wpage->cm_watch_q)
        cm_watch_page_init(wpage);
    CMWatchEntry *wentry = cm_insert_watch_entry(wpage->cm_watch_q,
                                                id, ram_start, start, len);
    cnt = 1;
    atomic_xaddq(&cnt, &wpage->cnt);
    if (cnt == 0) {
    printf("reset tlb\n");
        cm_tlb_set_watch(start, ram_start);
        tb_flush(cpu_single_env);
        for(cpu_idx = 0; cpu_idx < coremu_get_targetcpu(); cpu_idx++) {
            if(cpu_idx == cpu_single_env->cpu_index)
                continue;
        printf("broad cast to cpu[%d] reset tlb\n", cpu_idx);
            cm_send_watch_req(cpu_idx, &wentry->cm_wrange, 1);
        }
    }

}

void cm_remove_watch_point(CMTriggerID id, target_ulong start, target_ulong len)
{

}


bool cm_is_watch_addr_p(ram_addr_t addr)
{
    return cm_watch_p[addr >> TARGET_PAGE_BITS].cnt;
}

int cm_get_watch_index(void)
{
    return cm_watch_index;
}

void helper_watch_server(void);
void helper_watch_server(void)
{
    CPUState *self;
    CMTriggerID id;
    target_ulong cmd, start, len;
    self = cpu_single_env;
    cmd = self->regs[R_EAX];
    id = self->regs[R_EDI];
    start = self->regs[R_ESI];
    len = self->regs[R_EDX];
    printf("watch point[%ld] : cmd[%ld] start[0x%lx] len[%ld]\n", id, cmd, start, len);
    switch(cmd) {
        case WATCH_START:
            //not implement
            break;
        case WATCH_STOP:
            //not implement
            break;
        case WATCH_INSERT:
            cm_insert_watch_point(id, start, len);
            break;
        case WATCH_REMOVE:
            cm_remove_watch_point(id, start, len);
            break;
        default:
            printf("Invalidate watch cmd [%ld]\n", cmd);
    }
}

