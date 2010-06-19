/*
 * COREMU Parallel Emulator Framework
 *
 * Copyright (C) 2010 PPI, Fudan Univ. <http://ppi.fudan.edu.cn/system_research_group>
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
#include <assert.h>
#include "coremu-malloc.h"
#include "coremu-atomic.h"
#include "coremu-hw.h"

static uint16_t *cm_phys_tb_cnt;

void cm_init_tb_cnt(ram_addr_t ram_offset, ram_addr_t size)
{
    coremu_assert_hw_thr("cm_init_bt_cnt should only called by hw thr");

    cm_phys_tb_cnt = coremu_realloc(cm_phys_tb_cnt,
            sizeof(uint16_t) * ((ram_offset + size) >> TARGET_PAGE_BITS));
    memset(cm_phys_tb_cnt + (ram_offset >> TARGET_PAGE_BITS) * sizeof(uint16_t),
           0x0,  sizeof(uint16_t) * (size >> TARGET_PAGE_BITS));
}

void cm_phys_add_tb(ram_addr_t addr)
{
    atomic_incw(&cm_phys_tb_cnt[addr >> TARGET_PAGE_BITS]);
}

void cm_phys_del_tb(ram_addr_t addr)
{
    assert(cm_phys_tb_cnt[addr >> TARGET_PAGE_BITS]);
    atomic_decw(&cm_phys_tb_cnt[addr >> TARGET_PAGE_BITS]);
}

uint16_t cm_phys_page_tb_p(ram_addr_t addr)
{
    return cm_phys_tb_cnt[addr >> TARGET_PAGE_BITS];
}

void cm_invalidate_bitmap(CMPageDesc *p)
{
    /* Get the bitmap lock */
    coremu_spin_lock(&p->bitmap_lock);
    
    if (p->code_bitmap) {
        coremu_free(p->code_bitmap);
        p->code_bitmap = NULL;
    }

    /* Unlock the bitmap lock */
    coremu_spin_unlock(&p->bitmap_lock);

}

void cm_invalidate_tb(target_phys_addr_t start, int len)
{
    int count = tb_phys_invalidate_count;
    if (! coremu_hw_thr_p()) {
        tb_invalidate_phys_page_fast(start, len);
        count = tb_phys_invalidate_count - count;
    }

    if ((! cm_phys_page_tb_p(start))||(cm_phys_page_tb_p(start) == count))
        goto done;

    /* XXX: not finish need Lazy invalidate here! */
done:
    return;
}

void cm_tlb_reset_dirty_range(CPUTLBEntry * tlb_entry, 
                        unsigned long start, unsigned long length)
{
    unsigned long addr, old, addend;
    old = tlb_entry->addr_write;
    addend = tlb_entry->addend;

    if ((old & ~TARGET_PAGE_MASK) == IO_MEM_RAM) {
        addr = (old & TARGET_PAGE_MASK) + addend;
        if ((addr - start) < length) {
            uint64_t newv = (tlb_entry->addr_write & TARGET_PAGE_MASK) |
                TLB_NOTDIRTY;
            atomic_compare_exchangeq(&tlb_entry->addr_write, old, newv);
        }
    }
}

