#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "cpu-all.h"

#include "rwlock.h"
#include "coremu-config.h"
#include "cm-defs.h"
#include "cm-crew.h"
#include "cm-replay.h"

extern int smp_cpus;

/* TODO Consider what will happen if there's overflow. */
__thread memop_t memop;
memop_t **memop_cnt;

__thread int cm_is_in_tc;

static int n_memobj;
memobj_t *memobj; /* Used during recording. */
__thread last_memobj_t *last_memobj;
version_t *obj_version; /* Used during replay. */

__thread MappedLog memop_log;
__thread MappedLog version_log;

#ifdef DEBUG_MEMCNT
__thread MappedLog acc_version_log;
#endif

/* For replay */
__thread wait_version_t wait_version;
wait_memop_log_t *wait_memop_log;
__thread int *wait_memop_idx;

/* Using hash table or even just using a 2 entry cache here will actually
 * make performance worse. */
__thread unsigned long last_addr = 0;
__thread objid_t last_id = 0;

objid_t __memobj_id(unsigned long addr)
{
    ram_addr_t r;
    objid_t id;

    /*assert(qemu_ram_addr_from_host((void *)addr, &r) != -1);*/
    qemu_ram_addr_from_host((void *)addr, &r);
    id = r >> MEMOBJ_SHIFT;

    return id;
}

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
    if (open_mapped_log_path(LOGDIR"memop", &memop_log) != 0) {
        printf("Error opening memop log\n");
        exit(1);
    }
    if (open_mapped_log_path(LOGDIR"memop-index", &index_log) != 0) {
        printf("Error opening memop index log\n");
        exit(1);
    }

    wait_memop_log = calloc_check(n_memobj, sizeof(*wait_memop_log), "Can't allocate wait_memop");

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

void cm_crew_init(void)
{
    /* 65536 is for cirrus_vga.rom, added in hw/pci.c:pci_add_option_rom */
    /*n_memobj = (ram_size+PC_ROM_SIZE+VGA_RAM_SIZE+65536+MEMOBJ_SIZE-1) / MEMOBJ_SIZE;*/
    n_memobj = (ram_size+MEMOBJ_SIZE-1) / MEMOBJ_SIZE;
    /* XXX I don't know exactly how many is needed, providing more is safe */
    n_memobj += (n_memobj / 100);

    if (cm_run_mode == CM_RUNMODE_RECORD) {
        memobj = calloc_check(n_memobj, sizeof(*memobj), "Can't allocate memobj");
    } else {
        memop_cnt = calloc_check(smp_cpus, sizeof(*memop_cnt), "Can't allocate memop count\n");
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

#include <assert.h>
#include "cpu.h"

__thread uint32_t tlb_fill_cnt;

#ifdef DEBUG_MEM_ACCESS
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
#endif

void cm_assert_not_in_tc(void)
{
    if (cm_is_in_tc) {
        coremu_debug(
             "cm_coreid = %u, eip = %0lx, "
             "cm_tb_exec_cnt = %lu, "
             "memop = %u",
             cm_coreid,
             (long)cpu_single_env->ENVPC,
             cm_tb_exec_cnt[cm_coreid],
             (int)memop);
        pthread_exit(NULL);
    }
}

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
