#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <pthread.h>
#include "cpu-all.h"
#include "rwlock.h"
#include "coremu-atomic.h"
#include "cm-crew.h"
#include "cm-replay.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

extern int smp_cpus;

/* Record memory operation count for each vCPU
 * Overflow should not cause problem if we do not sort the output log. */
volatile uint32_t *memop_cnt; /* XXX what's the problem with original version? */
__thread volatile uint32_t *memop;

__thread uint32_t crew_inc_cnt;

__thread int cm_is_in_tc;

static __thread uint32_t *incop;

static const uint16_t SHARED_READ = 0xffff;
static const uint16_t NONRIGHT = 0xfffe;

struct memobj_t {
    tbb_rwlock_t lock;
    volatile uint16_t owner;
};

/* Now we track memory as 4K shared object, each object will have a memobj_t
 * tracking its ownership */
#define MEMOBJ_SIZE 4096
static memobj_t *memobj;
static int n_memobj;

/* Initialize to be cm_log[cm_coreid][CREW_INC] */
static __thread FILE *crew_inc_log;

enum {
    LOGENT_MEMOP,
    LOGENT_CPUNO,
    LOGENT_WAITMEMOP,
    LOGENT_N
};
#define READLOG_N 3

/* Ugly. For debugging, store the target physical address trying to access. */
__thread ram_addr_t pa_access;

static inline int memobj_id(const void *addr)
{
    ram_addr_t r;
    /* XXX This is slow, but should be correct. It's possible to use hash to
     * improve speed here.
     * Note this is getting the RAM address, not guest physical address. But as
     * guest memory has RAM offset starting as 0, this should be the same assert
     * physcal memory. */
    assert(qemu_ram_addr_from_host((void *)addr, &r) != -1);
    pa_access = r;
    int id = r >> TARGET_PAGE_BITS;
    coremu_assert(id >= 0 && id <= n_memobj,
                  "addr: %p, id: %d, memop: %d, n_memobj: %d", addr, id,
                  memop_cnt[cm_coreid], n_memobj);
    return id;
}

#define CREW_LOG_FMT "%u %u %u %lu\n"

static inline void write_inc_log(uint16_t logcpu_no, uint32_t memop,
                                 uint32_t waitcpuno, uint32_t waitmemop)
{
    fprintf(cm_log[logcpu_no][CREW_INC], CREW_LOG_FMT, memop, waitcpuno,
            waitmemop, cm_tb_exec_cnt[logcpu_no]);
}

static __thread uint64_t recorded_tb_exec_cnt;

static inline int read_inc_log(void)
{
    crew_inc_cnt++;

    if (fscanf(crew_inc_log, CREW_LOG_FMT, &incop[LOGENT_MEMOP],
           &incop[LOGENT_CPUNO], &incop[LOGENT_WAITMEMOP],
           &recorded_tb_exec_cnt) == EOF) {
        coremu_debug("no more inc log");
        cm_print_replay_info();
        /* XXX when the inc log are consumed up, we should not pause the current
         * processor, so set incop[LOGENT_MEM] to a value that will not  */
        incop[LOGENT_MEMOP]--;
    }
    return 0;
}

/* TODO We'd better use a buffer */
static inline void record_read_crew_fault(uint16_t owner, int objid) {
    /* The owner's privilege is decreased. */

    /* increase log: memop, objid, R/W, cpuno, memop
     * The above is the original log format. objid and R/W are removed. But
     * these information may be needed if we want to apply analysis on the log. */
    /*coremu_debug("record read fault");*/
    int i;
    uint32_t owner_memop = memop_cnt[owner];
    for (i = 0; i < smp_cpus; i++) {
        if (i != owner) {
            /* XXX Other reader threads may be running, and we need to record
             * the immediate instruction after the owner's write instruction. */
            write_inc_log(i, memop_cnt[i] + 1, owner, owner_memop);
        }
    }
}

static inline void record_write_crew_fault(uint16_t owner, int objid) {
    /*coremu_debug("record write fault");*/
    if (owner == SHARED_READ) {
        int i;
        for (i = 0; i < smp_cpus; i++) {
            if (i != cm_coreid) {
                write_inc_log(cm_coreid, *memop + 1, i, memop_cnt[i]);
            }
        }
    } else {
        write_inc_log(cm_coreid, *memop + 1, owner, memop_cnt[owner]);
    }
}

memobj_t *cm_read_lock(const void *addr)
{
    uint16_t owner;
    int objid = memobj_id(addr);
    memobj_t *mo = &memobj[objid];

    tbb_start_read(&mo->lock);
    owner = mo->owner;
    if ((owner != SHARED_READ) && (owner != cm_coreid)) {
        // We need to increase privilege for all cpu except the owner.
        // We use cmpxchg to avoid other readers make duplicate record.
        if (owner != NONRIGHT && atomic_compare_exchangew((uint16_t *)&mo->owner, owner,
                    NONRIGHT) == owner) {
            record_read_crew_fault(owner, objid);
            mo->owner = SHARED_READ;
        } else {
            // XXX Pause if other threads are taking log.
            while (mo->owner != SHARED_READ);
        }
    }
    return mo;
}

void cm_read_unlock(memobj_t *mo)
{
    tbb_end_read(&mo->lock);
    /*
     *if ((mo->lock.counter >> 2) >= 2) {
     *    coremu_debug("Error in rwlock, pc %p, lock->counter 0x%x, lock->owner 0x%x, objid %ld\n",
     *           (void *)cpu_single_env->eip, mo->lock.counter, mo->owner, mo - memobj);
     *    while (1);
     *}
     */
}

memobj_t *cm_write_lock(const void* addr)
{
    int objid = memobj_id(addr);
    memobj_t *mo = &memobj[objid];

    /*
     *if ((mo->lock.counter >> 2) >= 2) {
     *    coremu_debug("Error in rwlock, pc %p, lock->counter 0x%x, lock->owner 0x%x, objid %d\n",
     *           (void *)cpu_single_env->eip, mo->lock.counter, mo->owner, objid);
     *    while (1);
     *}
     */
    tbb_start_write(&mo->lock);
    if (mo->owner != cm_coreid) {
        /* We increase own privilege here. */
        record_write_crew_fault(mo->owner, objid);
        mo->owner = (uint16_t)cm_coreid;
    }
    return mo;
}

void cm_write_unlock(memobj_t *mo)
{
    tbb_end_write(&mo->lock);
    /*assert((mo->lock.counter >> 2) < 3);*/
}

static inline int apply_replay_inclog(void)
{
    /*
     *if (recorded_tb_exec_cnt > cm_tb_exec_cnt[cm_coreid]) {
     *    coremu_debug("Error in crew inc and tb_exec_cnt correspondence!\n"
     *                 "cm_coreid = %u, "
     *                 "cm_tb_exec_cnt = %lu, "
     *                 "recorded_tb_exec_cnt = %lu, "
     *                 "memop = %u, "
     *                 "crew_inc_cnt = %u, ",
     *                 cm_coreid,
     *                 cm_tb_exec_cnt[cm_coreid],
     *                 recorded_tb_exec_cnt,
     *                 *memop,
     *                 crew_inc_cnt);
     *    assert(0);
     *}
     */
    /* Wait for the target CPU's memop to reach the recorded value. */
    while (memop_cnt[incop[LOGENT_CPUNO]] < incop[LOGENT_WAITMEMOP]);
    return read_inc_log();
}

void cm_apply_replay_log(void)
{
    while (incop[LOGENT_MEMOP] == *memop + 1)
        apply_replay_inclog();
}

void cm_crew_init(void)
{
    memop_cnt = calloc(smp_cpus, sizeof(*memop_cnt));
    if (!memop_cnt) {
        printf("Can't allocate memop count\n");
        exit(1);
    }

    /* 65536 is for cirrus_vga.rom, added in hw/pci.c:pci_add_option_rom */
    /*n_memobj = (ram_size+PC_ROM_SIZE+VGA_RAM_SIZE+65536+MEMOBJ_SIZE-1) / MEMOBJ_SIZE;*/
    n_memobj = (ram_size+MEMOBJ_SIZE-1) / MEMOBJ_SIZE;
    /* XXX I don't know exactly how many is needed, providing more is safe */
    n_memobj *= 3;
    memobj = calloc(n_memobj, sizeof(memobj_t));
    if (!memobj) {
        printf("Can't allocate mem info\n");
        exit(1);
    }
    /* Initialize all memobj_t to shared read */
    int i;
    for (i = 0; i < n_memobj; i++)
        memobj[i].owner = SHARED_READ;
}

void cm_crew_core_init(void)
{
    crew_inc_log = cm_log[cm_coreid][CREW_INC];
    memop = &memop_cnt[cm_coreid];

    incop = calloc(LOGENT_N, sizeof(*incop));
    if (!incop) {
        printf("Can't allocate incop count\n");
        exit(1);
    }

    if (cm_run_mode == CM_RUNMODE_REPLAY)
        read_inc_log();
}

#include <assert.h>
#include "cpu.h"

__thread uint32_t memacc_cnt;

#define READ_LOG_FMT "%lx %lx %lx\n"
void debug_read_access(uint64_t val)
{
    if (cm_run_mode == CM_RUNMODE_NORMAL)
        return;
    if (cm_is_in_tc)
        memacc_cnt++;
    if (memacc_cnt != *memop) {
        coremu_debug("read error memacc_cnt = %u", memacc_cnt);
        cm_print_replay_info();
        exit(1);
    }
    if (cm_run_mode == CM_RUNMODE_RECORD)
        fprintf(cm_log[cm_coreid][READ], READ_LOG_FMT,
                cpu_single_env->eip, pa_access, val);
    /*else if (*memop > 1000000) {*/
    else {
        uint64_t rec_eip, rec_val;
        ram_addr_t rec_addr;
        int error = 0;
        fscanf(cm_log[cm_coreid][READ], READ_LOG_FMT,
               &rec_eip, &rec_addr, &rec_val);
        if (rec_eip != cpu_single_env->eip) {
            coremu_debug("read error in eip: coreid = %d, eip = %lx, recorded_eip = %lx",
                         cm_coreid, cpu_single_env->eip, rec_eip);
            error = 1;
        }
        if (pa_access != rec_addr) {
            coremu_debug("read error in adr: coreid = %d, addr = %lx, recorded_addr = %lx",
                         cm_coreid, pa_access, rec_addr);
            error = 1;
        }
        if (val != rec_val) {
            coremu_debug("read error in val: coreid = %d, val = %lx, recorded_val = %lx",
                         cm_coreid, val, rec_val);
            error = 1;
        }
        if (error) {
            cm_print_replay_info();
            pthread_exit(NULL);
        }
    }
}

#define WRITE_LOG_FMT "%lx\n"
void debug_write_access(void)
{
    if (cm_run_mode == CM_RUNMODE_NORMAL)
        return;
    if (cm_is_in_tc)
        memacc_cnt++;
    if (memacc_cnt != *memop) {
        coremu_debug("write error memacc_cnt = %u", memacc_cnt);
        cm_print_replay_info();
        exit(1);
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

void *cm_crew_read_func[4] = {
    cm_crew_readb,
    cm_crew_readw,
    cm_crew_readl,
    cm_crew_readq,
};

void *cm_crew_write_func[4] = {
    cm_crew_writeb,
    cm_crew_writew,
    cm_crew_writel,
    cm_crew_writeq,
};

