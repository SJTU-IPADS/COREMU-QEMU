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

#include <assert.h>
#include <math.h>
#include <sys/mman.h>

#include "sysemu.h"
#include "exec-all.h"

#include "sglib.h"
#include "coremu-malloc.h"
#include "coremu-intr.h"
#include "coremu-sched.h"
#include "coremu-atomic.h"
#include "cm-profile.h"
#include "cm-instrument.h"

int cm_profile_state;

void cm_profile_init(void)
{
    cm_profile_state = CM_PROFILE_STOP;
}

#define DEBUG_COREMU
#include "coremu-debug.h"

/* nb_tbs and tbs are only declared non static if COREMU_PROFILE_MODE is on. */
extern COREMU_THREAD int nb_tbs;

/*****************************************************************************
 * Hot PC/TB collection/identification
 *****************************************************************************/

#define PROFILE_KERNEL  1
#define PROFILE_USER    0
#define PROFILE_KERNEL_BASE 0xffffffff00000000

#define CM_HASH_PRIME   1021
#define CM_HASH_SIZE    1021
#define CM_HASH(value)  (value%CM_HASH_PRIME)

#define CM_HOT_SIGMA    32

static unsigned int cm_profile_ack;

/* A basic block maybe translated more than once to different TBs,
 * however, we need to count the execution time of the basic block not the TB.
 * profile_pc_cnt is used to record the execution count of a basic block. */
typedef struct profile_pc_cnt {
    uint64_t exec_count;    /* times the pc has been executed. */
    uint64_t to_cold_count; /* times the pc corresponding TB jumps to cold TB.
                               Is this useful? */
    uint64_t pc;

    struct profile_pc_cnt *next; /* Node to the next element. */
} profile_pc_cnt;

/* Generate list and hash table functions. */
#define PC_CNT_CMP(e1, e2) (e1->pc - e2->pc)
SGLIB_DEFINE_LIST_PROTOTYPES(profile_pc_cnt, PC_CNT_CMP, next);
#define pc_cnt_hash(e) ((e)->pc)
SGLIB_DEFINE_HASHED_CONTAINER_PROTOTYPES(profile_pc_cnt, CM_HASH_SIZE, pc_cnt_hash);

SGLIB_DEFINE_LIST_FUNCTIONS(profile_pc_cnt, PC_CNT_CMP, next);
SGLIB_DEFINE_HASHED_CONTAINER_FUNCTIONS(profile_pc_cnt, CM_HASH_SIZE, pc_cnt_hash);

static profile_pc_cnt *profile_pc_cnt_new(uint64_t pc)
{
    profile_pc_cnt *pc_cnt = coremu_mallocz(sizeof(*pc_cnt));

    pc_cnt->exec_count = 0;
    pc_cnt->to_cold_count = 0;
    pc_cnt->next = NULL;
    pc_cnt->pc = pc;

    return pc_cnt;
}

/*
 *static void profile_pc_cnt_delete(profile_pc_cnt *list)
 *{
 *    profile_pc_cnt *next;
 *    while (list != NULL) {
 *        next = list->next;
 *        coremu_free(list);
 *        list = next;
 *    }
 *}
 */

static void cm_flush_profile_info(void)
{
    TranslationBlock *tb;
    for (tb = tbs; tb < tbs + nb_tbs; tb++) {
        tb->cm_profile_counter = 0;
    }
    /* XXX if we want to clear the hot pc table, we need to unlink all the tb. */
    /*profile_pc_cnt_clear_hash(hot_pc_htab);*/
}

static __thread profile_pc_cnt *hot_pc_htab[CM_HASH_SIZE];
double hot_tb_threshold;

static void gather_hot_pc(void)
{
    /* We can't clear the hot pc table here. Otherwise collection function will
     * not find the corresponding item given a hot TB and its PC. */
    TranslationBlock *tb;
    uint64_t cnt;
    profile_pc_cnt *item;
    profile_pc_cnt target_item;

    /* First count how many times does a pc corresponding basic block executes. */
    for (tb = tbs; tb < tbs + nb_tbs; tb++) {
        cnt = tb->cm_profile_counter;
        /* Reset counter, since we will accumulate later in backtrace collection
         * function. */
        tb->cm_profile_counter = 0;

#if (!PROFILE_KERNEL)
        if (tb->pc >= (uint64_t)PROFILE_KERNEL_BASE)
            continue;
#endif

#if (!PROFILE_USER)
        if (tb->pc < (uint64_t)PROFILE_KERNEL_BASE)
            continue;
#endif
        target_item.pc = tb->pc;
        item = sglib_hashed_profile_pc_cnt_find_member(hot_pc_htab, &target_item);
        /* If no such item exists, insert a new one. */
        if (!item) {
            item = profile_pc_cnt_new(tb->pc);
            sglib_hashed_profile_pc_cnt_add(hot_pc_htab, item);
        }
        item->exec_count += cnt;
    }

    /* walk through the hash table */
    uint64_t pc_cnt = 0; /* how many pc */
    double sum = 0, avg = 0, sqr_sum = 0, sqr_avg = 0;

    SGLIB_HASHED_CONTAINER_MAP_ON_ELEMENT(profile_pc_cnt, item, hot_pc_htab, {
        pc_cnt++;
        cnt = item->exec_count;
        sum += cnt;
        sqr_sum += cnt * cnt;
    });

    avg = sum / pc_cnt;
    sqr_avg = sqr_sum / pc_cnt;

    /* So we assume the pc counts obey normal distribution. We calculate the
     * deviation, and use the average to get the threshold. */
    double sigma = sqrt(sqr_avg - avg * avg);
    hot_tb_threshold = avg + CM_HOT_SIGMA * sigma;

    coremu_debug("PC avg exec count %lf", avg);
    coremu_debug("PC sqr_avg %lf", sqr_avg);
    coremu_debug("Hot TB Threshold %lu", (uint64_t)hot_tb_threshold);

    /* Delete items which are not hot, what left in the hash table is hot pc */
    SGLIB_HASHED_CONTAINER_MAP_ON_ELEMENT(profile_pc_cnt, item, hot_pc_htab, {
        if (item->exec_count <= hot_tb_threshold) {
            sglib_hashed_profile_pc_cnt_delete(hot_pc_htab, item);
            coremu_free(item);
        }
    });
}

/* return true if a specific pc is hot */
bool is_hot_pc(target_ulong pc)
{
    profile_pc_cnt target_item = { .pc = pc };
    profile_pc_cnt *item;

    item = sglib_hashed_profile_pc_cnt_find_member(hot_pc_htab, &target_item);
    return item != NULL;
}

/* Maps translated code ptr to tb. To optimize jmp target TB look up. */
typedef struct tc2tb {
    unsigned long tc;
    void *tb;
    struct tc2tb *next;
} tc2tb;

static tc2tb *tc2tb_new(unsigned long tc, void *tb)
{
    tc2tb *p = coremu_mallocz(sizeof(*p));
    p->tc = tc;
    p->tb = tb;
    return p;
}

#define TCPTR_CMP(e1, e2) (e1->tc - e2->tc)
SGLIB_DEFINE_LIST_PROTOTYPES(tc2tb, TCPTR_CMP, next);
#define tc_hash(e) ((e)->tc % CM_HASH_PRIME)
SGLIB_DEFINE_HASHED_CONTAINER_PROTOTYPES(tc2tb, CM_HASH_SIZE, tc_hash);

SGLIB_DEFINE_LIST_FUNCTIONS(tc2tb, TCPTR_CMP, next);
SGLIB_DEFINE_HASHED_CONTAINER_FUNCTIONS(tc2tb, CM_HASH_SIZE, tc_hash);

#define CLEAR_HASH_TABLE_FUNCTION(type) \
static inline void type##_clear_hash(type **htab) {\
    SGLIB_HASHED_CONTAINER_MAP_ON_ELEMENT(type, item, htab, {\
        coremu_free(item);\
    }\
    sglib_hashed_##type##_init(htab);\
})
CLEAR_HASH_TABLE_FUNCTION(tc2tb);

static __thread tc2tb *tc2tb_htab[CM_HASH_SIZE];

static void tag_hot_tb(void)
{
    TranslationBlock *tb;
    tc2tb_clear_hash(tc2tb_htab);

    for (tb = tbs; tb < tbs + nb_tbs; tb++) {
        if (is_hot_pc(tb->pc)) {
            coremu_debug("find hot TB: %ld, PC: %p, cpu id: %d", tb - tbs,
                    (void*)tb->pc, cpu_single_env->cpuid_apic_id);
            tb->cm_hot_tb = 1;

            /* Add target addr and TB association. */
            tc2tb *p = tc2tb_new((unsigned long)tb->cm_profile_cnt_tc_ptr, tb);
            sglib_hashed_tc2tb_add(tc2tb_htab, p);
            /* XXX Do we need to add tb->tc_ptr in the hash table?
             * If in profiling mode, add_jump will only set jmp to
             * cm_profile_cnt_t, so no need to add tc_ptr. */
            /*
             **p = new_tc2tb((unsigned long)tb->tc_ptr, tb);
             *sglib_hashed_tc2tb_add(tc2tb_htab, p);
             */
        } else {
            tb->cm_hot_tb = 0;
        }
    }
}

static void patch_hot_tb(void);

static void identify_hot_trace(void)
{
    gather_hot_pc();
    tag_hot_tb();
    patch_hot_tb();
    /* Reset execution count so we will only collect trace when the application
     * runs for the second time. */
    cm_flush_profile_info();
}

/*****************************************************************************
 * TB patching
 *****************************************************************************/

typedef struct trace_prologue_page {
    struct trace_prologue_page *next;
    uint8_t *page;
} trace_prologue_page;
#define PAGE_CMP(e1, e2) (e1->page - e2->page)

/* Allocate trace code buffer one page at a time.
 * code ptr points to the start of the buffer which can be used to put code. */
int trace_page_size; /* size of a trace prologue page */
COREMU_THREAD uint8_t *trace_code_ptr;
COREMU_THREAD unsigned int trace_buffer_left; /* Records how many space is left in the buffer. */

/* This piece code contains calls the helper function to collect trace info. */
static uint8_t trace_code_template[] = {
    0x57, /* push %rdi, save this register for argument passing */
    0xbf, 0x00, 0x00, 0x00, 0x00, /* mov $0x0, %rdi, patch here for arg*/
    0xe8, 0x00, 0x00, 0x00, 0x00, /* callq <displacement to func>, patch here for func */
    0x5f, /* pop %rdi */
    0xe9, 0x00, 0x00, 0x00, 0x00, /* jmp <displacement of next tb>, patch for tb. */
};
#define ARG1_ADDR_OFFSET 2
#define CALL_ADDR_OFFSET 7

int trace_prologue_size = sizeof(trace_code_template);

/* Records pages allocated for trace prologue */
static __thread trace_prologue_page *trace_pages;

/* This is the same with tb_set_jmp_target1 */
static inline void cm_patch_trace_call_addr(unsigned long ptr, unsigned long addr)
{
    /* patch call addr. relative address to the jmp address. */
    *(uint32_t *)(ptr + CALL_ADDR_OFFSET) = addr - (ptr + CALL_ADDR_OFFSET + 4);
}
static inline void cm_patch_trace_argument(unsigned long ptr, unsigned long arg)
{
    *(uint32_t *)(ptr + ARG1_ADDR_OFFSET) = arg;
}

static void cm_collect_trace_profile(long tbid)
{
    /* Jumping to cold TB means that this vcpu has got the competing resource.
     * Only when the the jumping happens after the hot TB executes a huge number
     * of times means that there are contention on the resource. */
    TranslationBlock *tb = &tbs[tbid];

    if (tb->cm_profile_counter > 100) {
        coremu_debug("collecting hot trace");
        cm_dump_stack(cpu_single_env->dumpstack_buf->file, 10);
    }
    tb->cm_profile_counter = 0;
}

/* Generate a new code segment. Return the start address. */
uint8_t *cm_gen_trace_prologue(int tbid)
{
    coremu_debug("called");
    if (trace_buffer_left < trace_prologue_size) {
        coremu_debug("allocating a new page for trace code prologue");
        uint8_t *newpage;
        trace_page_size = getpagesize();
        /* NOTE we don't unmap now since the memory overhead should be too small. */
        newpage = mmap(NULL, trace_page_size,
                PROT_WRITE | PROT_READ | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (newpage == MAP_FAILED) {
            perror("[COREMU] mmap could not allocate memory for trace prologue\n");
            abort();
        }
        trace_code_ptr = newpage;
        trace_buffer_left = trace_page_size;
        trace_prologue_page *p = coremu_mallocz(sizeof (*p));
        p->page = newpage;
        SGLIB_LIST_ADD(trace_prologue_page, trace_pages, p, next);
    }

    memcpy(trace_code_ptr, trace_code_template, trace_prologue_size);
    trace_buffer_left -= trace_prologue_size;

    cm_patch_trace_call_addr((unsigned long)trace_code_ptr,
            (unsigned long) cm_collect_trace_profile);
    cm_patch_trace_argument((unsigned long)trace_code_ptr, tbid);

    uint8_t *save_ptr = trace_code_ptr;
    trace_code_ptr += trace_prologue_size;
    return save_ptr;
}

void cm_flush_trace_prologue(void)
{
    SGLIB_LIST_MAP_ON_ELEMENTS(trace_prologue_page, trace_pages, item, next, {
        if (munmap(item->page, trace_page_size) != 0)
            perror("munmap failed");
        coremu_free(item);
    });
    trace_pages = NULL;
}

/* Check if the target address is in trace prologue */
static inline int is_addr_in_trace_prologue(uint32_t addr)
{
    SGLIB_LIST_MAP_ON_ELEMENTS(trace_prologue_page, trace_pages, item, next, {
        coremu_debug("page: %p", item->page);
        if ((unsigned long)item->page <= addr &&
                addr < (unsigned long)item->page + trace_page_size) {
            coremu_debug("jmp to trace prologue");
            return true;
        }
    });
    return false;
}

/* Note relative address may be negative! Can't use unsigned. */
static inline int32_t get_jmp_rel_addr(TranslationBlock *tb, int n)
{
    return *(int32_t *)(tb->tc_ptr + tb->tb_jmp_offset[n]);
}

/* Return TB jmp instruction's absolute target address. */
static inline unsigned long get_jmp_abs_addr(TranslationBlock *tb, int n)
{
    uint16_t offset = tb->tb_jmp_offset[n];
    /*assert(offset != 0xffff);*/

    /* Address of the next instruction to jmp. */
    unsigned long jmp_next_ins_addr = (unsigned long)(tb->tc_ptr + offset + 4);
    return jmp_next_ins_addr + get_jmp_rel_addr(tb, n);
}

static int tb_need_patch(TranslationBlock *tb, int n)
{
    assert(tb->cm_hot_tb);

    /* All possible kinds of jmp target.
     * 1. Not set
     * 2. Jmp to the next instruction
     * 3. Jmp to trace prologue
     * 4. Jmp to TB (possible to itself) */

    uint16_t offset = tb->tb_jmp_offset[n];
    /* When we don't know the jmp target, don't patch the TB. */
    if (offset == 0xffff || offset == 0x0) {
        /*coremu_debug("jmp address not set");*/
        return true;
    }

    unsigned long jmp_abs_addr = get_jmp_abs_addr(tb, n);
    int32_t jmp_rel_addr = get_jmp_rel_addr(tb, n);
    /* Check for jmp to next instruction. */
    if (0 == jmp_rel_addr) {
        /*coremu_debug("jmp addr points to the next instruction");*/
        return true;
    }

    /* If TB already jump to prologue, no need to patch. */
    if (is_addr_in_trace_prologue(jmp_abs_addr))
        return true;

    tc2tb target;
    target.tc = jmp_abs_addr;
    tc2tb *item = sglib_hashed_tc2tb_find_member(tc2tb_htab, &target);
    return item != NULL;
}

static void trace_profile_patch_chain(TranslationBlock *tb, int n)
{
    if (tb->cm_trace_prologue_ptr[n] == NULL)
        tb->cm_trace_prologue_ptr[n] = cm_gen_trace_prologue(tb - tbs);

    /* NOTE order is important. */
    /* 1. Patch the prologue to jump to the original TB. */
    cm_patch_trace_jmp_addr((unsigned long)tb->cm_trace_prologue_ptr[n],
            get_jmp_abs_addr(tb, n));
    /* 2. Patch TB to jump to the trace prologue. */
    tb_set_jmp_target(tb, n, (unsigned long)tb->cm_trace_prologue_ptr[n]);
}

static void patch_hot_tb(void)
{
    coremu_debug("called");
    TranslationBlock *it;

    for (it = tbs; it < tbs + nb_tbs; it++) {
        if (!it->cm_hot_tb)
            continue;
        if (!tb_need_patch(it, 0))
            trace_profile_patch_chain(it, 0);
        if (!tb_need_patch(it, 1))
            trace_profile_patch_chain(it, 1);
    }
}

/*****************************************************************************
 * Profile interrupt and command handling
 *****************************************************************************/

/* Profile Interrupt */
typedef struct {
    CMIntr *base;
    int event;
} CMProfileIntr;

static void cm_profile_intr_handler(void *opaque)
{
    CMProfileIntr *intr = (CMProfileIntr *)opaque;
    int event = intr->event;

    atomic_incl(&cm_profile_ack);
    coremu_debug("profile_ack %d", cm_profile_ack);

    switch (event) {
    case CM_PROFILE_START:
        coremu_debug("start profile");
        cm_flush_profile_info();
        break;
    case CM_PROFILE_PREPARE:
        coremu_debug("prepare profile");
        cm_cpu_unlink_all_tb();
        break;
    case CM_PROFILE_START_TRACE:
        coremu_debug("start trace profile");
        identify_hot_trace();
        break;
    default:
        printf("bad profile event\n");
    };
}

static void cm_send_profile_intr(int target, int event)
{
    CMProfileIntr *intr = coremu_mallocz(sizeof(*intr));
    ((CMIntr *)intr)->handler = cm_profile_intr_handler;
    intr->event = event;

    coremu_send_intr(intr, target);
}

/* Wait for all the cpus to receive the interrupt. */
static void cm_wait_other_cpu(void)
{
    struct timespec interval;
    interval.tv_sec = 0;
    interval.tv_nsec = 10000000; /* 10ms */
    while (cm_profile_ack < smp_cpus)
        nanosleep(&interval, NULL);
}

#define CM_SEND_INTR_OTHER(cpu_idx, command) \
    do { \
        cm_profile_ack = 1; \
        int ncpus = coremu_get_targetcpu(); \
        int cpu_idx; \
        for (cpu_idx = 0; cpu_idx < ncpus; cpu_idx++) { \
            if (cpu_idx == cpu_single_env->cpu_index) \
                continue; \
            { command; }; \
        } \
        cm_wait_other_cpu(); \
    } while (0)

static void cm_start_profile(void)
{
    coremu_debug("called");
    cm_profile_state = CM_PROFILE_START;
    /* This function is called by the vcpu making hypercall. We need to wait
     * until all other vcpus has handled the interrupt, so we have
     * to call the actual handling function here. */
    cm_flush_profile_info();

    CM_SEND_INTR_OTHER(id, {
        cm_send_profile_intr(id, CM_PROFILE_START);
    });
}

static void cm_prepare_profile(void)
{
    coremu_debug("called");
    cm_profile_state = CM_PROFILE_PREPARE;
    /* unlink all local tbs */
    cm_cpu_unlink_all_tb();

    CM_SEND_INTR_OTHER(id, {
        cm_send_profile_intr(id, CM_PROFILE_PREPARE);
    });
}

static void cm_start_trace_profile(void)
{
    coremu_debug("called");
    cm_profile_state = CM_PROFILE_START_TRACE;

    identify_hot_trace();

    CM_SEND_INTR_OTHER(id, {
        cm_send_profile_intr(id, CM_PROFILE_START_TRACE);
    });
}

void helper_profile_hypercall(void);
void helper_profile_hypercall(void)
{
    target_ulong req = cpu_single_env->regs[R_EAX];
    coremu_debug("profile request %ld", req);
    switch (req) {
    case CM_PROFILE_START:
        cm_start_profile();
        break;
    case CM_PROFILE_PREPARE:
        cm_prepare_profile();
        break;
    case CM_PROFILE_START_TRACE:
        cm_start_trace_profile();
        break;
    case CM_PROFILE_CACHE_START:
		memtrace_start();
		break;
	case CM_PROFILE_CACHE_STOP:
		memtrace_stop();
		break;
    default:
        printf("error hypercall command : %ld", req);
    }
}

