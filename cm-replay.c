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
#include "exec-all.h"

#include "coremu-config.h"
#include "coremu-core.h"

#include "cm-crew.h"
#include "cm-intr.h"
#include "cm-replay.h"
#include "cm-loop.h"
#include "cm-defs.h"

#define VERBOSE_COERMU
#define DEBUG_COREMU
#include "coremu-debug.h"

#ifdef DEBUG_REPLAY
int going_to_fail;
#endif

extern int smp_cpus;

/* Whether the vm is being recorded or replayed. */
int cm_run_mode;

/* Use this to get cpu specific info. */
__thread int16_t cm_coreid;

/* Array containing tb execution count for each cpu. */
uint64_t *cm_tb_exec_cnt;
/* How many times the ipi interrupt handler is called. */
__thread volatile int cm_ipi_intr_handler_cnt;

/* Replaying interrupt. */

/* Inject interrupt when cm_tb_exec_cnt reaches exec_cnt */
__thread IntrLog cm_inject_intr;

#define LOG_INTR_FMT "%x %lu %p\n"

/* XXX hope disk DMA intterrupt number could be somewhat fixed. */
#define IS_DMA_INT(intno) \
    ((intno == 0x3e))

void cm_record_intr(int intno, long eip)
{
    /* For DMA, we should wait until it finishes.
     * Linux uses 0xfd as DMA interrupt. */
    /*coremu_debug("recording dma interrupt");*/
    if (IS_DMA_INT(intno)) {
        cm_record_disk_dma();
    }
#ifdef DEBUG_REPLAY
    fprintf(cm_log[INTR], LOG_INTR_FMT, intno,
            cm_tb_exec_cnt[cm_coreid], (void *)(long)eip);
#else
    IntrLog log;
    log.intno = intno;
    log.exec_cnt = cm_tb_exec_cnt[cm_coreid];
    if (fwrite(&log, sizeof(log), 1, cm_log[INTR]) != 1) {
        coremu_print("intr log record error");
    }
#endif
}

static inline void cm_read_intr_log(void)
{
#ifdef DEBUG_REPLAY
    if (fscanf(cm_log[INTR], LOG_INTR_FMT, &cm_inject_intr.intno,
               &cm_inject_intr.exec_cnt,
               (void **)&cm_inject_intr.eip) == EOF)
        cm_inject_intr.exec_cnt = -1;
#else
    if (fread(&cm_inject_intr, sizeof(cm_inject_intr), 1, cm_log[INTR]) != 1)
        cm_inject_intr.exec_cnt = -1;
#endif
}

#ifdef TARGET_I386
static void cm_wait_disk_dma(void);
#endif

int cm_replay_intr(void)
{
    int intno;

    /* We should only wait when it needs to inject an interrupt. */
    if (cm_tb_exec_cnt[cm_coreid] == cm_inject_intr.exec_cnt) {
#ifdef TARGET_I386
        /* Wait the init and sipi interrupt handler to be called. */
        if (cm_inject_intr.intno == CM_CPU_INIT || cm_inject_intr.intno == CM_CPU_SIPI) {
            int recorded_ipi_cnt = 0;
            cm_replay_ipi_handler_cnt(&recorded_ipi_cnt);
            while (cm_ipi_intr_handler_cnt < recorded_ipi_cnt)
                cm_receive_intr();
        }

        if (IS_DMA_INT(cm_inject_intr.intno))
            cm_wait_disk_dma();
#endif

        /*
         *coremu_debug("coreid %hu injecting interrupt %d at cm_tb_exec_cnt = %lu with "
         *             "cm_inject_intr_handler_cnt = %lu",
         *             cm_coreid, cm_inject_intno, cm_tb_exec_cnt[cm_coreid], cm_inject_intr_handler_cnt);
         */
        intno = cm_inject_intr.intno;
        cm_read_intr_log(); /* Read next log entry. */
        return intno;
    }
    return -1;
}

#ifdef DEBUG_REPLAY

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
    } \
    return 1; \
}

#else /* !DEBUG_REPLAY */

#define GEN_RECORD_FUNC(name, type, log, fmt) \
void cm_record_##name(type arg) \
{ \
    if (fwrite(&arg, sizeof(type), 1, log) != 1) { \
        fprintf(stderr, "write log error"); \
    } \
}

#define GEN_REPLAY_FUNC(name, type, log, fmt) \
int cm_replay_##name(type *arg) \
{ \
    if (fread(arg, sizeof(type), 1, log) != 1) { \
        coremu_debug("no more log"); \
        cm_print_replay_info(); \
        exit(0); \
    }\
    return 1; \
}
#endif

#define GEN_FUNC(name, type, log, fmt) \
    GEN_RECORD_FUNC(name, type, log, fmt) \
    GEN_REPLAY_FUNC(name, type, log, fmt)

/* IPI hander cnt */
#define IPI_LOG_FMT "%x\n"
GEN_FUNC(ipi_handler_cnt, int, cm_log[IPI], IPI_LOG_FMT);

/* input data */

#define IN_LOG_FMT "%x\n"
GEN_FUNC(in, uint32_t, cm_log[IN], IN_LOG_FMT);
/*
 *#define IN_LOG_FMT "%x %x\n"
 *[> XXX Recording address is only for debugging. <]
 *void cm_record_in(uint32_t address, uint32_t value) {
 *    fprintf(cm_log[IN], IN_LOG_FMT, address, value);
 *}
 *[> Returns 0 if ther's no more log entry. <]
 *int cm_replay_in(uint32_t *value) {
 *    uint32_t address;
 *    if (fscanf(cm_log[IN], IN_LOG_FMT, &address, value) == EOF) {
 *        printf("no more in log\n");
 *        exit(0);
 *        return 0;
 *    }
 *    return 1;
 *}
 */

/* mmio */
#define MMIO_LOG_FMT "%u\n"
GEN_FUNC(mmio, uint32_t, cm_log[MMIO], MMIO_LOG_FMT);

void cm_debug_mmio(void *f)
{
    fprintf(cm_log[MMIO], "%p\n", f);
}
/* rdtsc */
#define RDTSC_LOG_FMT "%lu\n"
GEN_FUNC(rdtsc, uint64_t, cm_log[RDTSC], RDTSC_LOG_FMT);

/* dma */

/* Count how many disk DMA operations are done. */
volatile uint64_t cm_dma_cnt;
static uint64_t cm_next_dma_cnt = 1;

__thread uint64_t cm_dma_done_exec_cnt;

#define DMA_LOG_FMT "%lu\n"
void cm_record_disk_dma(void)
{
    /* For each CPU, record when the DMA is done.
     * XXX can we improve this by letting only one CPU record this, and other
     * CPU accessing the DMA memory can be recorded through memory ordering. */
    /*
     *int i;
     *for (i = 0; i < smp_cpus; i++)
     *    fprintf(cm_log[i][DISK_DMA], DMA_LOG_FMT, cm_tb_exec_cnt[i]);
     */
#ifdef DEBUG_REPLAY
    fprintf(cm_log[DISK_DMA], DMA_LOG_FMT, cm_dma_cnt);
#else
    uint64_t cnt = cm_dma_cnt;
    if (fwrite(&cnt, sizeof(cnt), 1, cm_log[DISK_DMA]) == -1) {
        fprintf(stderr, "writing disk dma log error\n");
    }
#endif
}

static inline void cm_read_dma_log(void)
{
    /*
     *if (fscanf(cm_log[DISK_DMA], DMA_LOG_FMT, &cm_dma_done_exec_cnt) == EOF) {
     *    [> Set dma done cnt to max possible value so will not wait any more. <]
     *    cm_dma_done_exec_cnt = (uint64_t)-1;
     *}
     */
#ifdef DEBUG_REPLAY
     if (fscanf(cm_log[DISK_DMA], DMA_LOG_FMT, &cm_next_dma_cnt) == EOF)
        printf("no more dma log\n");
#else
     if (fread(&cm_next_dma_cnt, sizeof(cm_dma_cnt), 1, cm_log[DISK_DMA]) != 1)
        printf("no more dma log\n");
#endif
}

#ifdef TARGET_I386
static void cm_wait_disk_dma(void)
{
    /* We only need to wait for DMA operation to complete if current executed tb
     * is more then when DMA is done during recording. */
    /*
     *if (cm_tb_exec_cnt[cm_coreid] < cm_dma_done_exec_cnt)
     *    return;
     */

    /*
     *coremu_debug("CPU %d waiting DMA cnt to be %lu, cm_tb_exec_cnt = %lu "
     *             "cm_dma_done_exec_cnt = %lu", cm_coreid,
     *             cm_next_dma_cnt, cm_tb_exec_cnt[cm_coreid], cm_dma_done_exec_cnt);
     */
    cm_read_dma_log();
    /*
     *coremu_debug("core %u get next_dma_cnt %lu at eip %p",
     *             cm_coreid, cm_dma_cnt, (void *)cpu_single_env->eip);
     */
    while (cm_dma_cnt < cm_next_dma_cnt) {
        /* Waiting for DMA operation to complete. */
        pthread_yield();
    }
    /*coremu_debug("DMA done, cm_tb_exec_cnt = %lu", cm_tb_exec_cnt[cm_coreid]);*/
    /*cm_next_dma_cnt = cm_dma_cnt + 1;*/
}
#endif

/* init */

void cm_replay_init(void)
{
    cm_log_init();
    /* Setup CPU local variable */
    cm_tb_exec_cnt = calloc(smp_cpus, sizeof(uint64_t));

    /* For hardware thread, set cm_coreid to -1. */
    cm_coreid = -1;

    cm_crew_init();
}

void cm_replay_core_init(void)
{
    cm_log_init_core();
    if (cm_run_mode == CM_RUNMODE_NORMAL)
        return;

    if (cm_run_mode == CM_RUNMODE_REPLAY) {
        cm_read_intr_log();
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
            fprintf(cm_log_allpc[i], LOG_ALL_EXEC_CNT_FMT, i,
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
        if (fscanf(cm_log_allpc[i], LOG_ALL_EXEC_CNT_FMT, &coreid,
                   &wait_exec_cnt) == EOF) {
            coremu_print("No more all pc log.");
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

/*
 *#include "cpu-all.h"
 *int logset = 0;
 *extern int loglevel;
 */

#include "cpu.h"

#ifdef ASSERT_REPLAY_PC

#include "config-target.h"
#ifdef TARGET_X86_64
#define PC_LOG_FMT "%016lx %u\n"
#else
#define PC_LOG_FMT "%08lx %u\n"
#endif

void cm_replay_assert_pc(uint64_t eip)
{
    uint64_t next_eip;
    uint32_t recorded_memop;

    assert(cm_is_in_tc);

    /* Update cpu eip so we get correct eip in debug code. This is necessary to
     * make the asserting code work correctly when there's page fault inside an
     * execution of a TB chain. */
    cpu_single_env->ENVPC = eip;

    int error = 0;
    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[PC], PC_LOG_FMT, &next_eip, &recorded_memop) == EOF) {
            coremu_debug("no more pc log, cm_coreid = %u, cm_tb_exec_cnt = %lu", cm_coreid,
                   cm_tb_exec_cnt[cm_coreid]);
            cm_print_replay_info();
            pthread_exit(NULL);
        }
        if (eip != next_eip) {
            coremu_debug("core %d ERROR in execution path!", cm_coreid);
            error = 1;
        }
        if (*memop != recorded_memop) {
            coremu_debug("core %d ERROR in memop cnt", cm_coreid);
            error = 1;
        }
        if (error) {
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
                    cm_inject_intr.exec_cnt,
                    cm_ioport_read_cnt,
                    cm_mmio_read_cnt);
            pthread_exit(NULL);
        }
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log[PC], PC_LOG_FMT, eip, *memop);
        break;
    }
}
#endif

#define GEN_ASSERT(name, type, log_item, fmt) \
void cm_replay_assert_##name(type cur) \
{ \
    type recorded; \
    switch (cm_run_mode) { \
    case CM_RUNMODE_REPLAY: \
        if (fscanf(cm_log[log_item], fmt, &recorded) == EOF) { \
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
                      cm_inject_intr.exec_cnt, \
                      cm_ioport_read_cnt, \
                      cm_mmio_read_cnt); \
            coremu_debug("ERROR "#name" differs!"); \
        } \
        break; \
    case CM_RUNMODE_RECORD: \
        fprintf(cm_log[log_item], fmt, cur); \
        break; \
    } \
}

#ifdef ASSERT_REPLAY_TBFLUSH
GEN_ASSERT(tbflush, uint64_t, TBFLUSH, "%ld\n");
#endif

#ifdef ASSERT_TLBFLUSH
#define TLBFLUSH_LOG_FMT "%ld %lx %u\n"
void cm_replay_assert_tlbflush(uint64_t exec_cnt, uint64_t eip, int coreid)
{
    uint64_t recorded_exec_cnt, recorded_eip;
    uint32_t recorded_tlb_fill_cnt;

    /*coremu_debug("coreid %d calling tlb flush", cm_coreid);*/

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[TLBFLUSH], TLBFLUSH_LOG_FMT, &recorded_exec_cnt,
                   &recorded_eip, &recorded_tlb_fill_cnt) == EOF) {
            coremu_debug("no more tlbflush log, coreid = %u, cm_tb_exec_cnt = %lu", coreid,
                   exec_cnt);
            cm_print_replay_info();
            pthread_exit(NULL);
        }
        /*
         *if ((recorded_exec_cnt != cm_tb_exec_cnt[coreid])
         *        || (recorded_eip != eip)) {
         */
        int error = 0;
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
            error = 1;
        }
        if (recorded_tlb_fill_cnt != tlb_fill_cnt) {
            coremu_debug(
                      "coreid = %u, eip = %0lx, cm_tb_exec_cnt = %lu, "
                      "tlb_fill_cnt = %u, recorded_tlb_fill_cnt = %u",
                      coreid, (long)eip, cm_tb_exec_cnt[coreid],
                      tlb_fill_cnt, recorded_tlb_fill_cnt);
        }
        if (error)
            pthread_exit(NULL);
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log[TLBFLUSH], TLBFLUSH_LOG_FMT,
                cm_tb_exec_cnt[coreid], eip, tlb_fill_cnt);
        break;
    }
}
#endif

#ifdef ASSERT_REPLAY_GENCODE
#define GENCODE_LOG_FMT "%ld %lx\n"
void cm_replay_assert_gencode(uint64_t eip)
{
    uint64_t recorded_exec_cnt, recorded_eip;

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[GENCODE], GENCODE_LOG_FMT, &recorded_exec_cnt,
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
        fprintf(cm_log[GENCODE], GENCODE_LOG_FMT,
                cm_tb_exec_cnt[cm_coreid], eip);
        break;
    }
}
#endif

#ifdef ASSERT_TLB_TLBFILL
#define TLBFILL_LOG_FMT "%lu %lu %u\n"
void cm_replay_assert_tlbfill(uint64_t addr)
{
    uint64_t recorded_exec_cnt, recorded_addr;
    uint32_t recorded_memop;

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[TLBFILL], TLBFILL_LOG_FMT, &recorded_exec_cnt,
                   &recorded_addr, &recorded_memop) == EOF) {
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
        fprintf(cm_log[TLBFILL], TLBFILL_LOG_FMT,
                cm_tb_exec_cnt[cm_coreid], addr, *memop);
        break;
    }
}
#endif

void cm_print_replay_info(void)
{
    coremu_debug("core_id = %u, eip = %lx, cm_tb_exec_cnt = %lu, memop = %u",
                 cm_coreid,
                 (uint64_t)cpu_single_env->ENVPC,
                 cm_tb_exec_cnt[cm_coreid],
                 *memop);
}

