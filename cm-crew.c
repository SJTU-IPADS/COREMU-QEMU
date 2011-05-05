#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "cpu-all.h"
#include "rwlock.h"
#include "coremu-atomic.h"
#include "cm-crew.h"
#include "cm-replay.h"

extern int smp_cpus;

/* Record memory operation count for each vCPU
 * Overflow should not cause problem if we do not sort the output log. */
static uint32_t *memop_cnt;
static __thread uint32_t *memop;

static __thread uint32_t *incop;

static const uint16_t SHARED_READ = 0xffff;
static const uint16_t NONRIGHT = 0xfffe;

struct memobj_t {
    tbb_rwlock_t lock;
    volatile int16_t owner;
};
typedef struct memobj_t memobj_t;

/* Now we track memory as 4K shared object, each object will have a memobj_t
 * tracking its ownership */
#define MEMOBJ_SIZE 4096
static memobj_t *memobj;

/* Initialize to be cm_log[cm_coreid][CREW_INC] */
static __thread FILE *crew_inc_log;

enum {
    LOGENT_MEMOP,
    LOGENT_CPUNO,
    LOGENT_WAITMEMOP,
    LOGENT_N
};
#define READLOG_N 3

/* Set to be the start address of the allocated memory emulating guest memory. */
unsigned long cm_ram_addr;
static inline int memobj_id(const void *addr)
{
    return (long)(addr - cm_ram_addr) >> TARGET_PAGE_BITS;
}

static inline void write_inc_log(uint32_t memop, uint32_t waitcpuno,
                                 uint32_t waitmemop)
{
    fprintf(crew_inc_log, "%u %u %u\n", memop, waitcpuno, waitmemop);
}

static inline int read_inc_log(void)
{
    return fscanf(crew_inc_log, "%u %u %u\n", &incop[LOGENT_MEMOP],
           &incop[LOGENT_CPUNO], &incop[LOGENT_WAITMEMOP]);
}

/* TODO We'd better use a buffer */
static inline void record_read_crew_fault(uint16_t owner, int objid) {
    /* The owner's privilege is decreased. */

    /* increase log: memop, objid, R/W, cpuno, memop
     * The above is the original log format. objid and R/W are removed. But
     * these information may be needed if we want to apply analysis on the log. */
    int i;
    for (i = 0; i < smp_cpus; i++) {
        if (i != owner) {
            /* XXX Other reader threads may be running, and we need to record
             * the immediate instruction after the owner's write instruction. */
            write_inc_log(memop_cnt[i] + 1, owner, memop_cnt[owner]);
        }
    }
}

static inline void record_write_crew_fault(uint16_t owner, int objid) {
    if (owner == SHARED_READ) {
        int i;
        for (i = 0; i < smp_cpus; i++) {
            if (i != cm_coreid) {
                write_inc_log(*memop + 1, i, memop_cnt[i]);
            }
        }
    } else {
        write_inc_log(*memop + 1, owner, memop_cnt[owner]);
    }
}

static inline int apply_replay_inclog(void)
{
    /* Wait for the target CPU's memop to reach the recorded value. */
    while (memop_cnt[incop[LOGENT_CPUNO]] < incop[LOGENT_WAITMEMOP]);
    return read_inc_log();
}

void cm_crew_init(void)
{
    memop_cnt = calloc(smp_cpus, sizeof(*memop_cnt));
    if (!memop_cnt) {
        printf("Can't allocate memop count\n");
        exit(1);
    }

    int n = (ram_size+MEMOBJ_SIZE-1) / MEMOBJ_SIZE;
    memobj = calloc(n, sizeof(memobj_t));
    if (!memobj) {
        printf("Can't allocate mem info\n");
        exit(1);
    }
    /* Initialize all memobj_t to shared read */
    int i;
    for (i = 0; i < n; i++)
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

