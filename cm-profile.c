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

#include "sysemu.h"
#include "exec-all.h"

#include "sglib.h"
#include "coremu-malloc.h"
#include "coremu-intr.h"
#include "coremu-sched.h"
#include "coremu-atomic.h"
#include "cm-profile.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

extern COREMU_THREAD TranslationBlock *tbs;
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

int cm_profile_state;
static unsigned int cm_profile_ack;

/* A basic block maybe translated more than once to different TBs,
 * however, we need to count the execution time of the basic block not the TB.
 * profile_pc_cnt is used to record the execution count of a basic block. */
typedef struct profile_pc_cnt {
    uint64_t exec_count;    /* times the pc has been executed. */
    uint64_t to_cold_count; /* times the pc corresponding TB jumps to cold TB. */
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

static __thread profile_pc_cnt *hot_pc_htab[CM_HASH_SIZE];

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
    double threshold = avg + CM_HOT_SIGMA * sigma;

    coremu_debug("PC avg exec count %lf", avg);
    coremu_debug("PC sqr_avg %lf", sqr_avg);
    coremu_debug("Hot TB Threshold %lu", (uint64_t)threshold);

    /* Delete items which are not hot, what left in the hash table is hot pc */
    SGLIB_HASHED_CONTAINER_MAP_ON_ELEMENT(profile_pc_cnt, item, hot_pc_htab, {
        if (item->exec_count <= threshold) {
            sglib_hashed_profile_pc_cnt_delete(hot_pc_htab, item);
            coremu_free(item);
        }
    });
}

/* return true if a specific pc is hot */
bool is_hot_pc(target_ulong pc)
{
    profile_pc_cnt target_item;
    profile_pc_cnt *item;

    target_item.pc = pc;
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

static void identify_hot_trace(void)
{
    gather_hot_pc();
    tag_hot_tb();
}

static void cm_flush_profile_info(void)
{
    TranslationBlock *tb;
    for (tb = tbs; tb < tbs + nb_tbs; tb++) {
        tb->cm_profile_counter = 0;
        tb->profile_next_tb = NULL;
        tb->collect_count = 0;
    }
    /* XXX if we want to clear the hot pc table, we need to unlink all the tb. */
    /*profile_pc_cnt_clear_hash(hot_pc_htab);*/
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
    default:
        printf("error hypercall command : %ld", req);
    }
}

