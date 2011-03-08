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
#include "coremu-core.h"
#include "cm-replay.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

#define MAXLOGLEN 256

extern int smp_cpus;

/* Whether the vm is being recorded or replayed. */
int cm_run_mode;

/* Use this to get cpu specific info. */
__thread int cm_coreid;

/* Array containing tb execution count for each cpu. */
uint64_t *cm_tb_exec_cnt;

/* Inject interrupt when cm_tb_exec_cnt reaches this value */
__thread uint64_t cm_inject_exec_cnt = -1;
static __thread int cm_inject_intno;
__thread long cm_inject_eip;

enum {
    INTR,
    PC,
    IN,
    RDTSC,
    MMIO,
    DISK_DMA,
    N_CM_LOG,
};

static const char *cm_log_name[] = {
    "intr", "pc", "in", "rdtsc", "mmio", "dma"
};

/* Array containing logs for each cpu. */
typedef FILE *log_t;
static log_t **cm_log;

#define LOGDIR "replay-log/"
static void open_log1(FILE **log, const char* logname, const char *mode)
{
    char logpath[MAXLOGLEN];
    snprintf(logpath, MAXLOGLEN, LOGDIR"%s-%d", logname, coremu_get_core_id());

    *log = fopen(logpath, mode);
    if (!(*log)) {
        printf("Can't open log file %s\n", logpath);
        exit(1);
    }
}

static void cm_open_log(const char *mode) {
    int i;
    for (i = 0; i < N_CM_LOG; i++)
        open_log1(&(cm_log[cm_coreid][i]), cm_log_name[i], mode);
}

void cm_replay_flush_log(void) {
    int i;
    for (i = 0; i < N_CM_LOG; i++)
        fflush(cm_log[cm_coreid][i]);
}

/* interrupt */

#define LOG_INTR_FMT "%d %lu %p\n"

void cm_record_intr(int intno, long eip) {
    fprintf(cm_log[cm_coreid][INTR], LOG_INTR_FMT, intno, cm_tb_exec_cnt[cm_coreid], (void *)(long)eip);
}

static inline void cm_read_intr_log(void)
{
    if (fscanf(cm_log[cm_coreid][INTR], LOG_INTR_FMT, &cm_inject_intno,
               &cm_inject_exec_cnt, (void **)&cm_inject_eip) == EOF) {
        cm_inject_exec_cnt = -1;
    }
}

static void cm_wait_disk_dma(void);

int cm_replay_intr(void) {
    int intno;

    cm_wait_disk_dma();

    if (cm_tb_exec_cnt[cm_coreid] == cm_inject_exec_cnt) {
        /*coremu_debug("injecting interrupt at %lu", cm_tb_exec_cnt);*/
        intno = cm_inject_intno;
        cm_read_intr_log(); /* Read next log entry. */
        return intno;
    }
    return -1;
}

#define GEN_RECORD_FUNC(name, type, log, fmt) \
void cm_record_##name(type arg) { \
    fprintf(log, fmt, arg); \
}

#define GEN_REPLAY_FUNC(name, type, log, fmt) \
int cm_replay_##name(type *arg) { \
    if (fscanf(log, fmt, arg) == EOF) \
        return 0; \
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

void cm_debug_mmio(void *f) {
    fprintf(cm_log[cm_coreid][MMIO], "%p\n", f);
}

/* rdtsc */
#define RDTSC_LOG_FMT "%lu\n"
GEN_FUNC(rdtsc, uint64_t, cm_log[cm_coreid][RDTSC], RDTSC_LOG_FMT);

/* dma */

/* Count how many disk DMA operations are done. */
uint64_t cm_dma_cnt;
static uint64_t cm_next_dma_cnt = 1;

__thread uint64_t cm_dma_done_exec_cnt;

#define DMA_LOG_FMT "%lu\n"
void cm_record_disk_dma(void) {
    int i;
    /* For each CPU, record when the DMA is done.
     * XXX can we improve this since only one CPU will record this, and other
     * CPU accessing the DMA memory can be recorded through memory ordering. */
    for (i = 0; i < smp_cpus; i++)
        fprintf(cm_log[i][DISK_DMA], DMA_LOG_FMT, cm_tb_exec_cnt[i]);
}

static inline void cm_read_dma_log(void) {
    if (fscanf(cm_log[cm_coreid][DISK_DMA], DMA_LOG_FMT, &cm_dma_done_exec_cnt) == EOF) {
        /* Set dma done cnt to max possible value so will not wait any more. */
        cm_dma_done_exec_cnt = 0xffffffffffffffff;
    }
}

static void cm_wait_disk_dma(void) {
    /* We only need to wait for DMA operation to complete if current executed tb
     * is more then when DMA is done during recording. */
    if (cm_tb_exec_cnt[cm_coreid] < cm_dma_done_exec_cnt)
        return;

    int printed = 0;
    while (cm_dma_cnt < cm_next_dma_cnt) {
        /* Waiting for DMA operation to complete. */
        if (!printed) {
            coremu_debug("CPU %d waiting DMA cnt to be %lu, cm_tb_exec_cnt = %lu "
                         "cm_dma_done_exec_cnt = %lu", cm_coreid,
                         cm_next_dma_cnt, cm_tb_exec_cnt[cm_coreid], cm_dma_done_exec_cnt);
            printed = 1;
        }
        pthread_yield();
    }
    cm_read_dma_log();
    cm_next_dma_cnt = cm_dma_cnt + 1;
}

/* init */
static int cm_replay_inited = 0;

void cm_replay_init(void) {
    /* Setup CPU local variable */
    cm_tb_exec_cnt = calloc(smp_cpus, sizeof(uint64_t));

    cm_log = calloc(smp_cpus, sizeof(FILE **));
    assert(cm_log);
    int i;
    for (i = 0; i < smp_cpus; i++) {
        cm_log[i] = calloc(N_CM_LOG, sizeof(FILE *));
        assert(cm_log[i]);
    }

    /* For hardware thread, set cm_coreid to -1. */
    cm_coreid = -1;
}

void cm_replay_core_init(void)
{
    if (cm_run_mode == CM_RUNMODE_NORMAL || cm_replay_inited)
        return;

    /* Must initialize this before calling other functions. */
    cm_coreid = coremu_get_core_id();

    const char *mode = cm_run_mode == CM_RUNMODE_REPLAY ? "r" : "w";
    cm_open_log(mode);
    if (cm_run_mode == CM_RUNMODE_REPLAY) {
        cm_read_intr_log();
        cm_read_dma_log();
    }
    cm_replay_inited = 1;
}

/* debugging */

/* Check whether the next eip is the same as recorded. This is used for
 * debugging. */

extern int cm_ioport_read_cnt;

#include "cpu.h"
#ifdef TARGET_X86_64
#define PC_LOG_FMT "%08lx\n"
#else
#define PC_LOG_FMT "%08x\n"
#endif

void helper_cm_replay_assert_pc(void);
void helper_cm_replay_assert_pc(void) {
    target_ulong next_eip;

    /*
     *if (cm_tb_exec_cnt[cm_coreid] % 10240 != 0)
     *    return;
     */

    switch (cm_run_mode) {
    case CM_RUNMODE_REPLAY:
        if (fscanf(cm_log[cm_coreid][PC], PC_LOG_FMT, &next_eip) == EOF) {
            printf("no more pc log\n");
            exit(1);
        }
        coremu_assert(cpu_single_env->eip == next_eip,
                      "eip = %p, recorded eip = %p, cm_tb_exec_cnt = %lu, cm_ioport_read_cnt = %d",
                      (void *)(long)cpu_single_env->eip, (void *)(long)next_eip,
                      cm_tb_exec_cnt[cm_coreid], cm_ioport_read_cnt);
        break;
    case CM_RUNMODE_RECORD:
        fprintf(cm_log[cm_coreid][PC], PC_LOG_FMT, cpu_single_env->eip);
        break;
    }
}
