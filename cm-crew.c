#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "coremu-config.h"
#include "cm-defs.h"
#include "cm-crew.h"
#include "cm-replay.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

__thread int cm_is_in_tc;

/* TODO Consider what will happen if there's overflow. */
__thread memop_t memop;
memop_t **memop_cnt;

static int n_memobj;
memobj_t *memobj; /* Used during recording. */
__thread last_memobj_t *last_memobj;
version_t *obj_version; /* Used during replay. */

#ifdef LAZY_LOCK_RELEASE
__thread struct crew_state_t crew;
struct crew_gstate_t crew_g;
#endif

__thread MappedLog memop_log;
__thread MappedLog version_log;

#ifdef DEBUG_MEMCNT
__thread MappedLog acc_version_log;
#endif

/* For replay */
__thread wait_version_t wait_version;
wait_memop_log_t *wait_memop_log;
__thread int *wait_memop_idx;

/* Initialization */

static inline void *calloc_check(size_t nmemb, size_t size, const char *err_msg) {
    void *p = calloc(nmemb, size);
    if (!p) {
        printf("memory allocation failed: %s\n", err_msg);
        exit(1);
    }
    return p;
}

static void load_wait_memop_log(void) {
    MappedLog memop_log, index_log;
    int no_memop_log = false;
    if (open_mapped_log_path(LOGDIR"memop", &memop_log) != 0) {
        perror("opening memop log\n");
        no_memop_log = true;
    }
    if (open_mapped_log_path(LOGDIR"memop-index", &index_log) != 0) {
        perror("opening memop index log\n");
        no_memop_log = true;
    }

    wait_memop_log = calloc_check(n_memobj, sizeof(*wait_memop_log), "Can't allocate wait_memop");

    if (no_memop_log) {
        // For single core record, it's possible that there's no memop log
        int i;
        for (i = 0; i < n_memobj; i++) {
            wait_memop_log[i].size = -1;
            wait_memop_log[i].log = NULL;
            wait_memop_log[i].n = 0;
        }
    } else {
        /* Each entry in index log contains start and log entry count, both are int. */
        int *index = (int *)index_log.buf;
        wait_memop_t *log_start = (wait_memop_t *)memop_log.buf;
        int i = 0;
        for (; i < n_memobj; i++) {
            if (*index == -1) {
                wait_memop_log[i].log = NULL;
                wait_memop_log[i].n = 0;
                wait_memop_log[i].size = -1;
                index += 2;
                continue;
            }
            wait_memop_log[i].log = &log_start[*index++];
            wait_memop_log[i].n = 0;
            wait_memop_log[i].size = *index++;
        }
        unmap_log(&index_log);
    }
}

#ifdef DMA_DETECTOR
extern void (*dma_lock_range)(const char *start, int len);
extern void (*dma_unlock_range)(const char *start, int len);
#endif

void cm_crew_init(void)
{
    n_memobj = OBJID_CNT;

#ifdef DMA_DETECTOR
    // hack to avoid linking cm-crew.o with block-obj
    // qemu-system links with block-obj, but block-obj does not link with
    // qemu-systme-x86
    dma_lock_range = cm_acquire_write_lock_range;
    dma_unlock_range = cm_acquire_write_unlock_range;
#endif

    if (cm_run_mode == CM_RUNMODE_RECORD) {
        memobj = calloc_check(n_memobj, sizeof(*memobj), "Can't allocate memobj");
#if defined(LAZY_LOCK_RELEASE) || defined(DMA_DETECTOR)
        int i;
        for (i = 0; i < n_memobj; ++i) {
            memobj[i].owner = -1;
        }
#endif
#ifdef LAZY_LOCK_RELEASE
        crew_g.contending.memobj = calloc_check(cm_ncpus, sizeof(*crew_g.contending.memobj),
                "Can't allocated contend_memobj");
        crew_g.contending.core_idx = calloc_check(cm_ncpus, sizeof(*crew_g.contending.core_idx),
                "Can't allocated contend_idx_arr");
        crew_g.contending.core = calloc_check(cm_ncpus, sizeof(*crew_g.contending.core),
                "Can't allocate contend_core_arr");
#endif
    } else {
        memop_cnt = calloc_check(cm_ncpus, sizeof(*memop_cnt), "Can't allocate memop count\n");
        obj_version = calloc_check(n_memobj, sizeof(*obj_version), "Can't allocate obj_version\n");
        load_wait_memop_log();
    }
}

void cm_crew_core_init(void)
{
    if (cm_run_mode == CM_RUNMODE_RECORD) {
        new_mapped_log("memop", cm_coreid, &memop_log);
        new_mapped_log("version", cm_coreid, &version_log);
#ifdef DEBUG_MEMCNT
        new_mapped_log("accversion", cm_coreid, &acc_version_log);
#endif

        last_memobj = calloc(n_memobj, sizeof(*last_memobj));
        int i = 0;
        for (; i < n_memobj; ++i) {
            // memop -1 means there's no previous memop
            last_memobj[i].memop = -1;
        }
#ifdef LAZY_LOCK_RELEASE
        memset(crew.contending.core, -1, sizeof(crew.contending.core));
        crew.contending.memobj = calloc_check(cm_ncpus,
                sizeof(*crew.contending.memobj), "Can't allocate contending.memobj");

        crew.contending.memop = calloc_check(n_memobj,
                sizeof(*crew.contending.memop), "Can't allocate contending.memop");
        for (i = 0; i < n_memobj; ++i) {
            crew.contending.memop[i] = -LAZY_RELEASE_MEMOP_DIFF;
        }

        crew_g.contending.memobj[cm_coreid] = crew.contending.memobj;
        crew_g.contending.core_idx[cm_coreid] = &crew.contending.core_idx;
        crew_g.contending.core[cm_coreid] = crew.contending.core;
#endif
    } else {
        if (open_mapped_log("version", cm_coreid, &version_log) != 0) {
            printf("core %d opening version log failed\n", cm_coreid);
            exit(1);
        }
#ifdef DEBUG_MEMCNT
        if (open_mapped_log("accversion", cm_coreid, &acc_version_log) != 0) {
            printf("core %d opening memcnt log failed\n", cm_coreid);
            exit(1);
        }
#endif
        wait_memop_idx = calloc_check(n_memobj, sizeof(*wait_memop_idx),
                "Can't allocate wait_memop_idx");
        /* Register TLS variable address to a global array to allow cross thread
         * access to TLS variable. */
        memop_cnt[cm_coreid] = &memop;

        read_next_version_log();
    }
}

void cm_crew_core_finish(void)
{
    int i;

    for (i = 0; i < n_memobj; i++) {
        if (last_memobj[i].version != memobj[i].version) {
            log_other_wait_memop(i, &last_memobj[i]);
        }
    }

    /* Use -1 to mark the end of the log. */
    wait_version_t *l = next_version_log();
    l->memop = -1;

    rec_wait_memop_t *t = next_memop_log();
    t->objid = -1;
}

#ifdef LAZY_LOCK_RELEASE
/* Call this when exiting TC or failed to acquire lock. */
void cm_release_all_memobj(void)
{
    int i = 0;
    for (i = 0; i < MAX_LOCKED_MEMOBJ; i++) {
        if (crew.locked_memobj[i]) {
            cm_release_memobj(crew.locked_memobj[i]);
            crew.locked_memobj[i] = NULL;
        }
    }
}

void __cm_release_contending_memobj(void)
{
    uint8_t idx = crew.contending.core_start_idx;
    cpuid_t coreid;
    memobj_t *mo;

    /*coremu_debug("core %d releasing memobj starts at idx %d", cm_coreid, idx);*/
    while ((coreid = crew.contending.core[idx]) != -1) {
        mo = crew.contending.memobj[coreid];
        /*
         *coremu_debug("core %d releasing contending memobj %ld %p for core %d idx %d",
         *        cm_coreid, mo - memobj, mo, coreid, idx);
         */
        coremu_assert(mo, "core %d memobj shouldn't be null", cm_coreid);
        cm_release_memobj(mo);

        // obj owner update contending memop to avoid holding the lock
        // immediately after releasing
        crew.contending.memop[mo - memobj] = memop;

        crew.contending.core[idx] = -1;
        /* Clear the slot after it's actually released. */
        crew.contending.memobj[coreid] = NULL;

        idx = (idx + 1) & CONTENDING_CORE_IDX_MASK;
    }
    /*coremu_debug("core %d released memobj end at idx %d", cm_coreid, idx);*/
    crew.contending.core_start_idx = idx;
}

void *cm_crew_read_lazy_func[4] = {
    cm_crew_record_lazy_readb,
    cm_crew_record_lazy_readw,
    cm_crew_record_lazy_readl,
    cm_crew_record_lazy_readq,
};

#endif // LAZY_LOCK_RELEASE

#ifdef DMA_DETECTOR

void cm_acquire_write_lock_range(const char *start, int len)
{
    /*coremu_debug("dma start %p len %d", start, len);*/
    if (cm_run_mode != CM_RUNMODE_RECORD)
        return;
    const char *p = start;
    objid_t objid = memobj_id(start);
    memobj_t *mo = &memobj[objid];

    cm_coreid = cm_ncpus; // This is executed in AIO thread.
    for (; p < start + len; p += MEMOBJ_SIZE, mo++) {
#ifdef LAZY_LOCK_RELEASE
        int printed = 0;
        while (coremu_spin_trylock(&mo->write_lock) == BUSY) {
            if (!printed) {
                coremu_debug("DMA waiting core %d release obj %ld", mo->owner, mo - memobj);
                printed = 1;
            }
            /* The memory maybe hold by other core because of lazy lock
             * releasing. */
            cm_add_contending_memobj(mo);
        }
#else
        if (mo->owner != -1 || mo->write_lock == BUSY) {
            printf("DMA accessing memory owned by other core %d\n", mo->owner);
            exit(1);
        }
        coremu_spin_lock(&mo->write_lock);
#endif
        mo->version++;
        mo->owner = cm_ncpus;
    }
}

void cm_acquire_write_unlock_range(const char *start, int len)
{
    if (cm_run_mode != CM_RUNMODE_RECORD)
        return;
    const char *p = start;
    objid_t objid = memobj_id(start);
    memobj_t *mo = &memobj[objid];

    for (; p < start + len; p += MEMOBJ_SIZE, mo++) {
        mo->owner = -1; // First set it not owned, then allow reader to cut in
        mo->version--;
        coremu_spin_unlock(&mo->write_lock);
    }
    /*coremu_debug("dma done %p len %d", start, len);*/
}
#endif // DMA_DETECTOR

#ifdef ASSERT_REPLAY_TLBFILL
__thread uint32_t tlb_fill_cnt;
#endif

#ifdef DEBUG_MEM_ACCESS
#include <assert.h>
#include "cpu.h"

__thread uint32_t memacc_cnt;
__thread int error_print_cnt = 0;
#define PRINT_ERROR_TIMES 10

/*#define READ_LOG_FMT "%lx %lx %u\n"*/
struct memacc_log_t {
    uint64_t pc;
    uint64_t val;
    /*uint32_t tlb_fill_cnt;*/
    objid_t objid;
} __attribute__((packed));

void debug_mem_access(uint64_t val, objid_t objid, const char *acc_type)
{
    if (cm_run_mode == CM_RUNMODE_NORMAL)
        return;
    assert(cm_is_in_tc);
    memacc_cnt++;
    if (memacc_cnt != memop) {
        coremu_debug("core %d %s error memacc_cnt = %u", cm_coreid, acc_type, memacc_cnt);
        cm_print_replay_info();
        exit(1);
    }

    struct memacc_log_t l;
    if (cm_run_mode == CM_RUNMODE_RECORD) {
        /*
         *fprintf(cm_log[READ_ACC], READ_LOG_FMT,
         *        (uint64_t)cpu_single_env->ENVPC, val, tlb_fill_cnt);
         */
        l.pc = (uint64_t)cpu_single_env->ENVPC;
        l.val = val;
        /*l.tlb_fill_cnt = tlb_fill_cnt;*/
        l.objid = objid;
        fwrite(&l, sizeof(l), 1, cm_log[MEMACC]);
        return;
    }
    // For replay
    int error = 0;
    /*
     *if (fscanf(cm_log[MEMACC], READ_LOG_FMT,
     *            &rec_eip, &rec_val, &tlb_cnt) == EOF)
     *    return;
     */
    if (fread(&l, sizeof(l), 1, cm_log[MEMACC]) != 1) {
        return; // No more log.
    }
    if (l.pc != cpu_single_env->ENVPC && error_print_cnt < PRINT_ERROR_TIMES) {
        coremu_debug("%s ERROR in eip: core = %d, eip = %lx, recorded_eip = %lx",
                acc_type, cm_coreid, (uint64_t)cpu_single_env->ENVPC, l.pc);
        error = 1;
    }
    if (val != l.val && error_print_cnt < PRINT_ERROR_TIMES) {
        coremu_debug("%s ERROR in val: core = %d, val = %lx, recorded_val = %lx",
                acc_type, cm_coreid, val, l.val);
        error = 1;
    }
    if (objid != l.objid && error_print_cnt < PRINT_ERROR_TIMES) {
        coremu_debug("%s ERROR in objid: core = %d, objid = %i, recorded_objid = %i",
                acc_type, cm_coreid, objid, l.objid);
        error = 1;
    }
    /*
     *if (tlb_fill_cnt != tlb_cnt) {
     *    coremu_debug("read ERROR in tlb fill cnt: coreid = %d, tlb_cnt = %u, recorded_cnt = %u",
     *                 cm_coreid, tlb_fill_cnt, tlb_cnt);
     *    error = 1;
     *}
     */
    if (error && error_print_cnt < PRINT_ERROR_TIMES) {
        cm_print_replay_info();
        error_print_cnt++;
        /*pthread_exit(NULL);*/
    }
}
#endif // DEBUG_MEM_ACCESS

#define DATA_BITS 8
#include "cm-crew-template.h"

#define DATA_BITS 16
#include "cm-crew-template.h"

#define DATA_BITS 32
#include "cm-crew-template.h"

#define DATA_BITS 64
#include "cm-crew-template.h"

void *cm_crew_read_func[3][4] = {
    [CM_RUNMODE_NORMAL] = { },
    [CM_RUNMODE_RECORD] = {
        cm_crew_record_readb,
        cm_crew_record_readw,
        cm_crew_record_readl,
        cm_crew_record_readq,
    },
    [CM_RUNMODE_REPLAY] = {
        cm_crew_replay_readb,
        cm_crew_replay_readw,
        cm_crew_replay_readl,
        cm_crew_replay_readq,
    },
};

void *cm_crew_write_func[3][4] = {
    [CM_RUNMODE_NORMAL] = { },
    [CM_RUNMODE_RECORD] = {
        cm_crew_record_writeb,
        cm_crew_record_writew,
        cm_crew_record_writel,
        cm_crew_record_writeq,
    },
    [CM_RUNMODE_REPLAY] = {
        cm_crew_replay_writeb,
        cm_crew_replay_writew,
        cm_crew_replay_writel,
        cm_crew_replay_writeq,
    },
};
