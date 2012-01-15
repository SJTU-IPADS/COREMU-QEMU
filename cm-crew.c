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

/* This is not good practice, but typedef causes warning in printf */
#define memop_t uint32_t
/* Record memory operation count for each vCPU
 * Overflow should not cause problem if we do not sort the output log. */
volatile memop_t *memop_cnt;
__thread volatile memop_t *memop;

__thread int cm_is_in_tc;

__thread uint32_t tlb_fill_cnt;

#define SHARED_READ -1

struct memobj_t {
    volatile cpuid_t owner;
    volatile uint16_t version;
    /* Keeps track last writer's info. */
    volatile cpuid_t last_writer;
    volatile memop_t last_writer_memop;

    tbb_rwlock_t lock;
};

/* Now we track memory as 4K shared object, each object will have a memobj_t
 * tracking its ownership */
#define MEMOBJ_SIZE 4096
static int n_memobj;
static memobj_t *memobj;
/* cache_info emulates the cache directory. last_read_version keeps track of the
 * version of the last read. Updated on CREW fault. */
__thread cpuid_t *last_read_version;

/* Initialize to be cm_log[cm_coreid][CREW_INC] */
static __thread FILE *crew_inc_log;

enum {
    LOGENT_MEMOP,
    LOGENT_CPUNO,
    LOGENT_WAITMEMOP,
    LOGENT_N
};
#define READLOG_N 3

/* Using hash table or even just using a 2 entry cache here will actually
 * make performance worse. */
__thread unsigned long last_addr = 0;
__thread long last_id = 0;

long __memobj_id(unsigned long addr)
{
    ram_addr_t r;
    long id;

    /*assert(qemu_ram_addr_from_host((void *)addr, &r) != -1);*/
    qemu_ram_addr_from_host((void *)addr, &r);
    id = r >> TARGET_PAGE_BITS;

    coremu_assert(id >= 0 && id <= n_memobj,
                  "addr: %p, id: %ld, memop: %d, n_memobj: %d", (void *)addr, id,
                  memop_cnt[cm_coreid], n_memobj);

    return id;
}

#define CREW_LOG_FMT "%u %hu %u\n"

typedef struct IncLog {
    memop_t self_memop;
    memop_t owner_memop;
    cpuid_t owner;
} __attribute__ ((packed)) IncLog;

__thread IncLog cm_inc_log;

static inline void write_inc_log(cpuid_t owner, memop_t owner_memop)
{
#ifdef DEBUG_REPLAY
    coremu_assert(cm_coreid != owner, "no need to wait self");
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
#ifdef DEBUG_REPLAY
    if (fscanf(crew_inc_log, CREW_LOG_FMT, &cm_inc_log.self_memop,
           (uint16_t *)&cm_inc_log.owner, &cm_inc_log.owner_memop) == EOF) {
        goto no_more_log;
    }
#else
    if (fread(&cm_inc_log, sizeof(cm_inc_log), 1, cm_log[cm_coreid][CREW_INC]) != 1)
        goto no_more_log;
#endif

    return 0;

no_more_log:
    coremu_debug("no more inc log");
    cm_print_replay_info();
    /* XXX when the inc log are consumed up, we should not pause the current
     * processor, so set self_memop to a value that will not become.
     * Note, overflow will cause problem here. */
    cm_inc_log.self_memop--;

    return 1;
}

/* TODO We'd better use a buffer */
static inline void record_read_crew_fault(cpuid_t owner, memop_t wait_memop) {
    /* The owner's privilege is decreased. */

    /* increase log: memop, objid, R/W, cpuno, memop
     * The above is the original log format. objid and R/W are removed. But
     * these information may be needed if we want to apply analysis on the log. */
    /*coremu_debug("record read fault");*/
    write_inc_log(owner, wait_memop);
}

static inline void record_write_crew_fault(cpuid_t owner) {
    /*coremu_debug("record write fault");*/
    if (owner != SHARED_READ) {
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

memobj_t *cm_read_lock(long objid)
{
    memobj_t *mo = &memobj[objid];

    tbb_start_read(&mo->lock);
    if ((last_read_version[objid] != mo->version)) {
        /* If version mismatch, it means there's write before this read. */
        if (mo->owner != SHARED_READ)
            mo->owner = SHARED_READ;
        last_read_version[objid] = mo->version;
        if (cm_coreid != mo->last_writer) {
            record_read_crew_fault(mo->last_writer, mo->last_writer_memop);
        }
    }
    return mo;
}

void cm_read_unlock(memobj_t *mo)
{
    tbb_end_read(&mo->lock);
}

memobj_t *cm_write_lock(long objid)
{
    memobj_t *mo = &memobj[objid];

    tbb_start_write(&mo->lock);
    mo->last_writer_memop = *memop + 1;
    if (mo->owner != cm_coreid) {
        cpuid_t owner = mo->owner;
        mo->version++;
        /* XXX Avoid version checking in following read. */
        last_read_version[objid] = mo->version;
        mo->owner = cm_coreid; /* Increase own privilege here. */
        mo->last_writer = cm_coreid;

        record_write_crew_fault(owner);
    }
    return mo;
}

void cm_write_unlock(memobj_t *mo)
{
    tbb_end_write(&mo->lock);
}

static inline int apply_replay_inclog(void)
{
    /* Wait for the target CPU's memop to reach the recorded value. */
    while (memop_cnt[cm_inc_log.owner] < cm_inc_log.owner_memop) {
        asm volatile ("pause" : : : "memory");
    }
    return read_inc_log();
}

void cm_apply_replay_log(void)
{
    while (cm_inc_log.self_memop == *memop + 1)
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
    for (i = 0; i < n_memobj; i++) {
        memobj[i].owner = SHARED_READ;
        memobj[i].version = 0;
        memobj[i].last_writer = SHARED_READ;
        memobj[i].last_writer_memop = 0;
    }
}

void cm_crew_core_init(void)
{
    crew_inc_log = cm_log[cm_coreid][CREW_INC];
    memop = &memop_cnt[cm_coreid];

    last_read_version = calloc(n_memobj, sizeof(*last_read_version));

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
        uint32_t tlb_cnt;
        memop_t rec_memop;
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

void *cm_crew_record_read_func[4] = {
    cm_crew_record_readb,
    cm_crew_record_readw,
    cm_crew_record_readl,
    cm_crew_record_readq,
};

void *cm_crew_record_write_func[4] = {
    cm_crew_record_writeb,
    cm_crew_record_writew,
    cm_crew_record_writel,
    cm_crew_record_writeq,
};

void *cm_crew_replay_read_func[4] = {
    cm_crew_replay_readb,
    cm_crew_replay_readw,
    cm_crew_replay_readl,
    cm_crew_replay_readq,
};

void *cm_crew_replay_write_func[4] = {
    cm_crew_replay_writeb,
    cm_crew_replay_writew,
    cm_crew_replay_writel,
    cm_crew_replay_writeq,
};

