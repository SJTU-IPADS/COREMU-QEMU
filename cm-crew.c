#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "cpu-all.h"

#include "rwlock.h"
#include "coremu-config.h"
#include "coremu-atomic.h"
#include "cm-crew.h"
#include "cm-replay.h"

/*#define VERBOSE_COREMU*/
/*#define DEBUG_COREMU*/
#include "coremu-debug.h"

extern int smp_cpus;

/* Record memory operation count for each vCPU
 * Overflow should not cause problem if we do not sort the output log. */
volatile uint32_t *memop_cnt; /* XXX what's the problem with original version? */
__thread volatile uint32_t *memop;

__thread uint32_t crew_inc_cnt;

__thread int cm_is_in_tc;

static __thread uint32_t *incop;

__thread uint32_t tlb_fill_cnt;

typedef uint16_t owner_t;

/* This is set to the number of CPUs. */
static owner_t SHARED_READ;

struct memobj_t {
    tbb_rwlock_t lock;
    volatile owner_t owner;

    volatile owner_t prev_read_owner; /* Records the owner value of previous read. */
    /* Keeps track last writer's info. */
    volatile owner_t last_writer;
    volatile uint32_t last_writer_memop;
};

/* Now we track memory as 4K shared object, each object will have a memobj_t
 * tracking its ownership */
#define MEMOBJ_SIZE 4096
static int n_memobj;
static memobj_t *memobj;
/* cache_info emulates the cache directory. last_read_owner keeps track of the cache's
 * owner field for the last read memory access. */
__thread owner_t *last_read_owner;

/* Initialize to be cm_log[cm_coreid][CREW_INC] */
static __thread FILE *crew_inc_log;

enum {
    LOGENT_MEMOP,
    LOGENT_CPUNO,
    LOGENT_WAITMEMOP,
    LOGENT_N
};
#define READLOG_N 3

static inline void *get_page_addr(const void *addr)
{
    return (void *)((unsigned long)addr & ~0xFFF);
}

static inline int memobj_id(const void *addr)
{
    ram_addr_t r;
    int id;
    /* XXX This is slow, but should be correct. It's possible to use hash to
     * improve speed here.
     * Note this is getting the RAM address, not guest physical address. But as
     * guest memory has RAM offset starting as 0, this should be the same assert
     * physcal memory. */
    static __thread void *last_addr = 0;
    static __thread int last_id = 0;

    void *page_addr = get_page_addr(addr);

    /* See if this is the same page, if yes, then return last id. */
    if (page_addr == last_addr) {
        id = last_id;
    } else {
        /*assert(qemu_ram_addr_from_host((void *)addr, &r) != -1);*/
        qemu_ram_addr_from_host((void *)addr, &r);
        id = r >> TARGET_PAGE_BITS;

        last_addr = page_addr;
        last_id = id;

        coremu_assert(id >= 0 && id <= n_memobj,
                      "addr: %p, id: %d, memop: %d, n_memobj: %d", addr, id,
                      memop_cnt[cm_coreid], n_memobj);
    }

    return id;
}

#define CREW_LOG_FMT "%u %hu %u\n"

typedef struct IncLog {
    uint32_t self_memop;
    owner_t owner;
    uint32_t owner_memop;
} IncLog;

__thread IncLog cm_inc_log;

static inline void write_inc_log(owner_t owner, uint32_t owner_memop)
{
#ifdef REPLAY_TXT_LOG
    fprintf(cm_log[cm_coreid][CREW_INC], CREW_LOG_FMT, memop_cnt[cm_coreid] + 1,
            owner, owner_memop);
#elif defined(REPLAY_LOGBUF)
    IncLog *log = coremu_logbuf_next_entry(&(cm_log_buf[cm_coreid][CREW_INC]), sizeof(*log));
    log->self_memop = memop_cnt[cm_coreid] + 1;
    log->owner = owner;
    log->owner_memop = owner_memop;
#else
    IncLog log;
    log.self_memop = memop_cnt[cm_coreid] + 1;
    log.owner = owner;
    log.owner_memop = owner_memop;
    if (fwrite(&log, sizeof(log), 1, cm_log[cm_coreid][CREW_INC]) != 1) {
        coremu_print("inc log write failed");
    }
#endif
}

static inline int read_inc_log(void)
{
    crew_inc_cnt++;

#ifdef REPLAY_TXT_LOG
    if (fscanf(crew_inc_log, CREW_LOG_FMT, &cm_inc_log.self_memop,
           &cm_inc_log.owner, &cm_inc_log.owner_memop) == EOF) {
        goto no_more_log;
    }
#else
    if (fread(&cm_inc_log, sizeof(cm_inc_log), 1, cm_log[cm_coreid][CREW_INC]) != 1)
        goto no_more_log;
#endif

    return 0;

    static int once = 0;
no_more_log:
    if (once) {
        coremu_debug("core %u exit", cm_coreid);
        pthread_exit(NULL);
    }
    once = 1;
    coremu_debug("no more inc log");
    cm_print_replay_info();
    /* XXX when the inc log are consumed up, we should not pause the current
     * processor, so set self_memop to a value that will not become.
     * Note, overflow will cause problem here. */
    cm_inc_log.self_memop--;

    return 1;
}

/* TODO We'd better use a buffer */
static inline void record_read_crew_fault(owner_t owner, uint32_t wait_memop) {
    /* The owner's privilege is decreased. */

    /* increase log: memop, objid, R/W, cpuno, memop
     * The above is the original log format. objid and R/W are removed. But
     * these information may be needed if we want to apply analysis on the log. */
    /*coremu_debug("record read fault");*/
    write_inc_log(owner, wait_memop);
}

static inline void record_write_crew_fault(owner_t owner, int objid) {
    /*coremu_debug("record write fault");*/
    if (owner < SHARED_READ) {
        write_inc_log(owner, memop_cnt[owner]);
    } else {
        int i;
        for (i = 0; i < smp_cpus; i++) {
            if (i != cm_coreid) {
                write_inc_log(i, memop_cnt[i]);
            }
        }
    }
}

memobj_t *cm_read_lock(const void *addr)
{
    int objid = memobj_id(addr);
    memobj_t *mo = &memobj[objid];

    tbb_start_read(&mo->lock);
    owner_t owner = mo->owner;
    if ((owner != cm_coreid) && last_read_owner[objid] != owner) {
        /* If the cache line is in shared read state, but since last_read_owner is
         * different with cache_info's owner, it means there're write op before
         * last read. So we need to wait last writer. */
        if (owner < SHARED_READ) {
            owner_t read_owner = mo->prev_read_owner + 1;
            if (read_owner == 0)
                read_owner = SHARED_READ;
            mo->owner = read_owner;
        }

        last_read_owner[objid] = mo->owner;
        record_read_crew_fault(mo->last_writer, mo->last_writer_memop);
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
        if (mo->owner >= SHARED_READ)
            mo->prev_read_owner = mo->owner;
        mo->last_writer = mo->owner = cm_coreid;
    }
    mo->last_writer_memop = *memop + 1;
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
    SHARED_READ = smp_cpus;

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
    for (i = 0; i < n_memobj; i++) {
        memobj[i].owner = SHARED_READ;
        memobj[i].prev_read_owner = SHARED_READ;
        memobj[i].last_writer = SHARED_READ;
        memobj[i].last_writer_memop = 0;
    }
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

    int i;
    last_read_owner = calloc(n_memobj, sizeof(*last_read_owner));
    for (i = 0; i < n_memobj; i++) {
        last_read_owner[i] = SHARED_READ;
    }

    if (cm_run_mode == CM_RUNMODE_REPLAY)
        read_inc_log();
}

#include <assert.h>
#include "cpu.h"

__thread uint32_t memacc_cnt;

#define READ_LOG_FMT "%lx %lx %u %u\n"
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
    if (cm_run_mode == CM_RUNMODE_RECORD) {
        fprintf(cm_log[cm_coreid][READ], READ_LOG_FMT,
                cpu_single_env->eip, val, tlb_fill_cnt, *memop);
    }
    /*else if (*memop > 1000000) {*/
    else {
        uint64_t rec_eip, rec_val;
        uint32_t tlb_cnt, rec_memop;
        int error = 0;
        if (fscanf(cm_log[cm_coreid][READ], READ_LOG_FMT,
               &rec_eip, &rec_val, &tlb_cnt, &rec_memop) == EOF)
            return;
        if (rec_eip != cpu_single_env->eip) {
            coremu_debug("read ERROR in eip: coreid = %d, eip = %lx, recorded_eip = %lx",
                         cm_coreid, cpu_single_env->eip, rec_eip);
            error = 1;
        }
        if (val != rec_val) {
            coremu_debug("read ERROR in val: coreid = %d, val = %lx, recorded_val = %lx",
                         cm_coreid, val, rec_val);
            error = 1;
        }
        /*
         *if (tlb_fill_cnt != tlb_cnt) {
         *    coremu_debug("read ERROR in tlb fill cnt: coreid = %d, tlb_cnt = %u, recorded_cnt = %u",
         *                 cm_coreid, tlb_fill_cnt, tlb_cnt);
         *    error = 1;
         *}
         */
        if (error) {
            cm_print_replay_info();
            pthread_exit(NULL);
        }
    }
}

#define WRITE_LOG_FMT "%lx %lx %u\n"
void debug_write_access(uint64_t val)
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
    if (cm_run_mode == CM_RUNMODE_RECORD)
        fprintf(cm_log[cm_coreid][WRITE], WRITE_LOG_FMT,
                cpu_single_env->eip, val, tlb_fill_cnt);
    /*else if (*memop > 1000000) {*/
    else {
        uint64_t rec_eip, rec_val;
        uint32_t cnt;
        int error = 0;
        if (fscanf(cm_log[cm_coreid][WRITE], WRITE_LOG_FMT,
               &rec_eip, &rec_val, &cnt) == EOF)
            return;
        if (rec_eip != cpu_single_env->eip) {
            coremu_debug("write ERROR in eip: coreid = %d, eip = %lx, recorded_eip = %lx",
                         cm_coreid, cpu_single_env->eip, rec_eip);
            error = 1;
        }
        if (val != rec_val) {
            coremu_debug("write ERROR in val: coreid = %d, val = %lx, recorded_val = %lx",
                         cm_coreid, val, rec_val);
            error = 1;
        }
        /*
         *if (tlb_fill_cnt != cnt) {
         *    coremu_debug("read ERROR in tlb fill cnt: coreid = %d, cnt = %u, recorded_cnt = %u",
         *                 cm_coreid, tlb_fill_cnt, cnt);
         *    error = 1;
         *}
         */
        if (error) {
            cm_print_replay_info();
            pthread_exit(NULL);
        }
    }
}

void cm_assert_not_in_tc(void)
{
    if (cm_is_in_tc) {
        coremu_debug(
             "cm_coreid = %u, eip = %0lx, "
             "cm_tb_exec_cnt = %lu, "
             "memop_cnt = %u",
             cm_coreid,
             (long)cpu_single_env->eip,
             cm_tb_exec_cnt[cm_coreid],
             *memop_cnt);
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

