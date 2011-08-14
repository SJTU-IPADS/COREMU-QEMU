/*
 * COREMU Parallel Emulator Framework
 *
 * Copyright (C) 2010 Parallel Processing Institute, Fudan Univ.
 *  <http://ppi.fudan.edu.cn/system_research_group>
 *
 * Authors:
 *  Yufei Chen      <chenyufei@fudan.edu.cn>
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
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include "cm-crew.h"
#include "cm-intr.h"
#include "cm-replay.h"
#include "cm-loop.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

extern int smp_cpus;

/* Whether the vm is being recorded or replayed. */
int cm_run_mode;

/* Use this to get cpu specific info. */
__thread uint16_t cm_coreid;

/* Array containing tb execution count for each cpu. */
uint64_t *cm_tb_exec_cnt;
/* How many times the interrupt handler is called. */
__thread volatile uint64_t cm_intr_handler_cnt;

/* Inject interrupt when cm_tb_exec_cnt reaches this value */
__thread uint64_t cm_inject_exec_cnt = -1;
__thread int cm_inject_intno;
__thread long cm_inject_eip;
__thread uint64_t cm_inject_intr_handler_cnt;

/* interrupt */

#define LOG_INTR_FMT "%x %lu %lu %p\n"

void cm_record_intr(int intno, long eip)
{
    fprintf(cm_log[cm_coreid][INTR], LOG_INTR_FMT, intno,
            cm_tb_exec_cnt[cm_coreid], cm_intr_handler_cnt, (void *)(long)eip);
}

static inline void cm_read_intr_log(void)
{
    if (fscanf(cm_log[cm_coreid][INTR], LOG_INTR_FMT, &cm_inject_intno,
               &cm_inject_exec_cnt, &cm_inject_intr_handler_cnt,
               (void **)&cm_inject_eip) == EOF) {
        cm_inject_exec_cnt = -1;
    }
}

static void cm_wait_disk_dma(void);

int cm_replay_intr(void)
{
    int intno;

    cm_wait_disk_dma();

    if (cm_tb_exec_cnt[cm_coreid] == cm_inject_exec_cnt) {
        /* Wait the interrupt handler to be called. */
        while (cm_intr_handler_cnt < cm_inject_intr_handler_cnt)
            cm_receive_intr();

        /*
         *coremu_debug("coreid %hu injecting interrupt %d at cm_tb_exec_cnt = %lu with "
         *             "cm_inject_intr_handler_cnt = %lu",
         *             cm_coreid, cm_inject_intno, cm_tb_exec_cnt[cm_coreid], cm_inject_intr_handler_cnt);
         */
        intno = cm_inject_intno;
        cm_read_intr_log(); /* Read next log entry. */
        return intno;
    }
    return -1;
}

#define GEN_RECORD_FUNC(name, type, log, fmt) \
void cm_record_##name(type arg) \
{ \
    fprintf(log, fmt, arg); \
}

#define GEN_REPLAY_FUNC(name, type, log, fmt) \
int cm_replay_##name(type *arg) \
{ \
    if (fscanf(log, fmt, arg) == EOF) { \
        coremu_debug("no more log"); \
        cm_print_replay_info(); \
        exit(0); \
    }\
    return 1; \
}

#define GEN_FUNC(name, type, log, fmt) \
    GEN_RECORD_FUNC(name, type, log, fmt) \
    GEN_REPLAY_FUNC(name, type, log, fmt)

/* input data */

#define IN_LOG_FMT "%x\n"
GEN_FUNC(in, uint32_t, cm_log[cm_coreid][IN], IN_LOG_FMT);
/*
 *#define IN_LOG_FMT "%x %x\n"
 *[> XXX Recording address is only for debugging. <]
 *void cm_record_in(uint32_t address, uint32_t value) {
 *    fprintf(cm_log[cm_coreid][IN], IN_LOG_FMT, address, value);
 *}
 *[> Returns 0 if ther's no more log entry. <]
 *int cm_replay_in(uint32_t *value) {
 *    uint32_t address;
 *    if (fscanf(cm_log[cm_coreid][IN], IN_LOG_FMT, &address, value) == EOF) {
 *        printf("no more in log\n");
 *        exit(0);
 *        return 0;
 *    }
 *    return 1;
 *}
 */

/* mmio */
#define MMIO_LOG_FMT "%u\n"
GEN_FUNC(mmio, uint32_t, cm_log[cm_coreid][MMIO], MMIO_LOG_FMT);

void cm_debug_mmio(void *f)
{
    fprintf(cm_log[cm_coreid][MMIO], "%p\n", f);
}

/* rdtsc */
#define RDTSC_LOG_FMT "%lu\n"
GEN_FUNC(rdtsc, uint64_t, cm_log[cm_coreid][RDTSC], RDTSC_LOG_FMT);

/* dma */

/* Count how many disk DMA operations are done. */
volatile uint64_t cm_dma_cnt;
static uint64_t cm_next_dma_cnt = 1;

__thread uint64_t cm_dma_done_exec_cnt;

#define DMA_LOG_FMT "%lu\n"
void cm_record_disk_dma(void)
{
    int i;
    /* For each CPU, record when the DMA is done.
     * XXX can we improve this since only one CPU will record this, and other
     * CPU accessing the DMA memory can be recorded through memory ordering. */
    for (i = 0; i < smp_cpus; i++)
        fprintf(cm_log[i][DISK_DMA], DMA_LOG_FMT, cm_tb_exec_cnt[i]);
}

static inline void cm_read_dma_log(void)
{
    if (fscanf(cm_log[cm_coreid][DISK_DMA], DMA_LOG_FMT, &cm_dma_done_exec_cnt) == EOF) {
        /* Set dma done cnt to max possible value so will not wait any more. */
        cm_dma_done_exec_cnt = (uint64_t)-1;
    }
}

static void cm_wait_disk_dma(void)
{
    /* We only need to wait for DMA operation to complete if current executed tb
     * is more then when DMA is done during recording. */
    if (cm_tb_exec_cnt[cm_coreid] < cm_dma_done_exec_cnt)
        return;

    /*
     *coremu_debug("CPU %d waiting DMA cnt to be %lu, cm_tb_exec_cnt = %lu "
     *             "cm_dma_done_exec_cnt = %lu", cm_coreid,
     *             cm_next_dma_cnt, cm_tb_exec_cnt[cm_coreid], cm_dma_done_exec_cnt);
     */
    while (cm_dma_cnt < cm_next_dma_cnt) {
        /* Waiting for DMA operation to complete. */
        pthread_yield();
    }
    /*coremu_debug("DMA done, cm_tb_exec_cnt = %lu", cm_tb_exec_cnt[cm_coreid]);*/
    cm_read_dma_log();
    cm_next_dma_cnt = cm_dma_cnt + 1;
}

/* init */

void cm_replay_init(void)
{
    /* Setup CPU local variable */
    cm_tb_exec_cnt = calloc(smp_cpus, sizeof(uint64_t));

    /* For hardware thread, set cm_coreid to -1. */
    cm_coreid = -1;

    cm_crew_init();
}

void cm_replay_core_init(void)
{
    if (cm_run_mode == CM_RUNMODE_NORMAL)
        return;

    if (cm_run_mode == CM_RUNMODE_REPLAY) {
        cm_read_intr_log();
        cm_read_dma_log();
    }

    cm_crew_core_init();
}

/* CPU initialization */

#define LOG_ALL_EXEC_CNT_FMT "%hu %lu\n"

void cm_record_all_exec_cnt(void)
{
    uint16_t i;

    for (i = 0; i < smp_cpus; i++) {
        if (i != cm_coreid) {
            fprintf(cm_log[cm_coreid][ALLPC], LOG_ALL_EXEC_CNT_FMT, i,
                    cm_tb_exec_cnt[i]);
        }
    }
}

void cm_replay_all_exec_cnt(void)
{
    uint16_t i, coreid;
    uint64_t wait_exec_cnt;

    for (i = 0; i < smp_cpus; i++) {
        if (i == cm_coreid)
            continue;
        if (fscanf(cm_log[cm_coreid][ALLPC], LOG_ALL_EXEC_CNT_FMT, &coreid,
                   &wait_exec_cnt) == EOF) {
            coremu_debug("No more all pc log.");
            return;
        } else {
            while (cm_tb_exec_cnt[coreid] < wait_exec_cnt)
                sched_yield();
            /*
             *coremu_debug("waited for %hu reach tb_exec_cnt %lu", coreid,
             *             wait_exec_cnt);
             */
        }
    }
}

/* debugging */

/* Check whether the next eip is the same as recorded. This is used for
 * debugging. */

extern uint64_t cm_ioport_read_cnt;
extern uint64_t cm_mmio_read_cnt;

#include "config-target.h"
#ifdef TARGET_X86_64
#define PC_LOG_FMT "%016lx %u\n"
#else
#define PC_LOG_FMT "%08lx %u\n"
#endif

/*
 *#include "cpu-all.h"
 *int logset = 0;
 *extern int loglevel;
 */

#include "cpu.h"

void cm_replay_assert_pc(uint64_t eip)
{
    uint64_t next_eip;
    uint32_t recorded_memop;

    assert(cm_is_in_tc);

    /* Update cpu eip so we get correct eip in debug code. */
    cpu_single_env->eip = eip;

    /*
     *if (cm_tb_exec_cnt[cm_coreid] % 10240 != 0)
     *    return;
     */

    /*
     *if (!logset && cm_tb_exec_cnt[cm_coreid] > 45000000) {
     *    coremu_debug("Enabling log");
     *    loglevel |= CPU_LOG_EXEC | CPU_LOG_TB_IN_ASM | CPU_LOG_TB_CPU;
     *    logset = 1;
     *}
     */

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[cm_coreid][PC], PC_LOG_FMT, &next_eip,
                   &recorded_memop) == EOF) {
            coremu_debug("no more pc log, cm_coreid = %u, cm_tb_exec_cnt = %lu", cm_coreid,
                   cm_tb_exec_cnt[cm_coreid]);
            cm_print_replay_info();
            pthread_exit(NULL);
        }
        if ((eip != next_eip)
                || (recorded_memop != *memop)) {
            if (eip != next_eip)
                coremu_debug("ERROR in execution path!");
            else if (*memop != recorded_memop)
                coremu_debug("ERROR in memop cnt");
            coremu_debug(
                      "cm_coreid = %u, eip = %016lx, recorded eip = %016lx, "
                      "memop_cnt = %u, recorded_memop = %u, "
                      "cm_tb_exec_cnt = %lu, cm_inject_exec_cnt = %lu, "
                      "cm_ioport_read_cnt = %lu, "
                      "cm_mmio_read_cnt = %lu",
                      cm_coreid,
                      (long)eip,
                      (long)next_eip,
                      *memop, recorded_memop,
                      cm_tb_exec_cnt[cm_coreid],
                      cm_inject_exec_cnt,
                      cm_ioport_read_cnt,
                      cm_mmio_read_cnt);
            pthread_exit(NULL);
        }
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log[cm_coreid][PC], PC_LOG_FMT, eip, *memop);
        break;
    }
}

#define GEN_ASSERT(name, type, log_item, fmt) \
void cm_replay_assert_##name(type cur) \
{ \
    type recorded; \
    switch (cm_run_mode) { \
    case CM_RUNMODE_REPLAY: \
        if (fscanf(cm_log[cm_coreid][log_item], fmt, &recorded) == EOF) { \
            printf("no more " #name " log\n"); \
        } \
        if (cur != recorded) { \
            coremu_debug("diff in "#name); \
            coremu_debug( \
                      #name" = %lx, recorded "#name" = %lx, " \
                      "cm_tb_exec_cnt = %lu, cm_inject_exec_cnt = %lu, " \
                      "cm_ioport_read_cnt = %lu, " \
                      "cm_mmio_read_cnt = %lu", \
                      (long)cur, \
                      (long)recorded, \
                      cm_tb_exec_cnt[cm_coreid], \
                      cm_inject_exec_cnt, \
                      cm_ioport_read_cnt, \
                      cm_mmio_read_cnt); \
            coremu_debug("ERROR "#name" differs!"); \
        } \
        break; \
    case CM_RUNMODE_RECORD: \
        fprintf(cm_log[cm_coreid][log_item], fmt, cur); \
        break; \
    } \
}

GEN_ASSERT(tbflush, uint64_t, TBFLUSH, "%ld\n");

#define TLBFLUSH_LOG_FMT "%ld %lx\n"
void cm_replay_assert_tlbflush(uint64_t exec_cnt, uint64_t eip, int coreid)
{
    uint64_t recorded_exec_cnt, recorded_eip;

    /*coremu_debug("coreid %d calling tlb flush", cm_coreid);*/

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[coreid][TLBFLUSH], TLBFLUSH_LOG_FMT, &recorded_exec_cnt,
                   &recorded_eip) == EOF) {
            coremu_debug("no more tlbflush log, coreid = %u, cm_tb_exec_cnt = %lu", coreid,
                   exec_cnt);
            cm_print_replay_info();
            pthread_exit(NULL);
        }
        /*
         *if ((recorded_exec_cnt != cm_tb_exec_cnt[coreid])
         *        || (recorded_eip != eip)) {
         */
        if (recorded_exec_cnt != cm_tb_exec_cnt[coreid]) {
            coremu_debug("diff in tlbflush");
            coremu_debug(
                      "coreid = %u, eip = %0lx, recorded eip = %0lx, "
                      "cm_tb_exec_cnt = %lu, recorded_exec_cnt = %lu",
                      coreid,
                      (long)eip,
                      (long)recorded_eip,
                      cm_tb_exec_cnt[coreid],
                      recorded_exec_cnt);
            pthread_exit(NULL);
        }
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log[coreid][TLBFLUSH], TLBFLUSH_LOG_FMT,
                cm_tb_exec_cnt[coreid], eip);
        break;
    }
}

#define GENCODE_LOG_FMT "%ld %lx\n"
void cm_replay_assert_gencode(uint64_t eip)
{
    uint64_t recorded_exec_cnt, recorded_eip;

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[cm_coreid][GENCODE], GENCODE_LOG_FMT, &recorded_exec_cnt,
                   &recorded_eip) == EOF) {
            coremu_debug("no more gencode log, cm_coreid = %u, cm_tb_exec_cnt = %lu", cm_coreid,
                   cm_tb_exec_cnt[cm_coreid]);
            cm_print_replay_info();
            pthread_exit(NULL);
        }
        /*
         *if ((recorded_exec_cnt != cm_tb_exec_cnt[cm_coreid])
         *        || (recorded_eip != eip)) {
         */
        if (recorded_exec_cnt != cm_tb_exec_cnt[cm_coreid]) {
            coremu_debug("diff in gencode");
            coremu_debug(
                      "cm_coreid = %u, eip = %0lx, recorded eip = %0lx, "
                      "cm_tb_exec_cnt = %lu, recorded_exec_cnt = %lu, "
                      "memop_cnt = %u",
                      cm_coreid,
                      (long)eip,
                      (long)recorded_eip,
                      cm_tb_exec_cnt[cm_coreid],
                      recorded_exec_cnt,
                      *memop_cnt);
            /*pthread_exit(NULL);*/
        }
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log[cm_coreid][GENCODE], GENCODE_LOG_FMT,
                cm_tb_exec_cnt[cm_coreid], eip);
        break;
    }
}

#define TLBFILL_LOG_FMT "%lu %u\n"
void cm_replay_assert_tlbfill(void)
{
    uint64_t recorded_exec_cnt;
    uint32_t recorded_memop;

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[cm_coreid][TLBFILL], TLBFILL_LOG_FMT, &recorded_exec_cnt,
                   &recorded_memop) == EOF) {
            coremu_debug("no more tlbfill log, cm_coreid = %u, cm_tb_exec_cnt = %lu", cm_coreid,
                   cm_tb_exec_cnt[cm_coreid]);
            cm_print_replay_info();
            pthread_exit(NULL);
        }
        if ((recorded_exec_cnt != cm_tb_exec_cnt[cm_coreid])
                || (recorded_memop != *memop)) {
            coremu_debug("diff in tlbfill");
            coremu_debug(
                      "cm_coreid = %u, "
                      "cm_tb_exec_cnt = %lu, recorded_exec_cnt = %lu, "
                      "memop = %u, recorded_memop = %u",
                      cm_coreid,
                      cm_tb_exec_cnt[cm_coreid],
                      recorded_exec_cnt,
                      *memop_cnt,
                      recorded_memop);
            /*pthread_exit(NULL);*/
        }
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log[cm_coreid][TLBFILL], TLBFILL_LOG_FMT,
                cm_tb_exec_cnt[cm_coreid], *memop);
        break;
    }
}
void cm_print_replay_info(void)
{
    coremu_debug("core_id = %u, eip = %lx, cm_tb_exec_cnt = %lu, memop = %u",
                 cm_coreid,
                 cpu_single_env->eip,
                 cm_tb_exec_cnt[cm_coreid],
                 *memop);
}

