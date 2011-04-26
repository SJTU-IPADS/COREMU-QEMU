#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu-all.h"
#include "rwlock.h"
#include "coremu-atomic.h"
#include "cm-crew.h"
#include "cm-replay.h"

extern int smp_cpus;

/* Record memory operation count for each vCPU
 * Overflow should not cause problem if we do not sort the output log. */
uint32_t *memop_cnt;

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
memobj_t *memobj;

__thread FILE *crew_inc_log;

void cm_crew_init(void)
{
    memop_cnt = calloc(smp_cpus, sizeof(uint64_t));
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

unsigned long cm_ram_addr;
static inline int memobj_id(void *addr)
{
    return ((long)(addr - cm_ram_addr) & TARGET_PAGE_MASK) >> TARGET_PAGE_BITS;
}

enum {
    LOGENT_MEMOP,
    LOGENT_CPUNO,
    LOGENT_WAITMEMOP,
    LOGENT_N
};
#define READLOG_N 3

static inline void write_inc_log(int logcpuno, uint32_t memop, int objid,
        uint32_t type, uint32_t waitcpuno, uint32_t waitmemop) {
    /* TODO Change this to make it more space efficient. */
    uint32_t logbuf[LOGENT_N];

    logbuf[LOGENT_MEMOP] = memop;
    logbuf[LOGENT_CPUNO] = waitcpuno;
    logbuf[LOGENT_WAITMEMOP] = waitmemop;

    fwrite(logbuf, sizeof(logbuf), 1, crew_inc_log);
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
            write_inc_log(i, memop_cnt[i] + 1, objid, SHARED_READ,
                    owner, memop_cnt[owner]);
        }
    }
}

static inline void record_write_crew_fault(uint16_t owner, int objid) {
    if (owner != SHARED_READ) {
        write_inc_log(cm_coreid, memop_cnt[cm_coreid] + 1, objid, cm_coreid,
                owner, memop_cnt[owner]);
    } else {
        int i;
        for (i = 0; i < smp_cpus; i++) {
            if (i != cm_coreid) {
                write_inc_log(cm_coreid, memop_cnt[cm_coreid] + 1, objid,
                        cm_coreid, i, memop_cnt[i]);
            }
        }
    }
}

int64_t cm_crew_read(void *addr, int size)
{
    uint64_t val[READLOG_N];
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

    val[LOGENT_MEMOP] = ++memop_cnt[cm_coreid];
    switch (size) {
    case 0:
        val[READLOG_N - 1] = *(uint8_t *)addr;
        break;
    case 1:
        val[READLOG_N - 1] = *(uint16_t *)addr;
        break;
    case 2:
        val[READLOG_N - 1] = *(uint32_t *)addr;
        break;
    case 3:
        val[READLOG_N - 1] = *(uint64_t *)addr;
        break;
    }
    // fwrite(val, sizeof(val), 1, reclog[cm_coreid]);

    tbb_end_read(&mo->lock);

    return val[READLOG_N - 1];
}

void cm_crew_write(void *addr, int64_t value, int size)
{
    int objid = memobj_id(addr);
    memobj_t *mo = &memobj[objid];

    tbb_start_write(&mo->lock);
    if (mo->owner != cm_coreid) {
        /* We increase own privilege here. */
        record_write_crew_fault(mo->owner, objid);
        mo->owner = cm_coreid;
    }
    switch (size) {
    case 0:
        *(uint8_t *)addr = (uint8_t) value;
        break;
    case 1:
        *(uint16_t *)addr = (uint16_t) value;
        break;
    case 2:
        *(uint32_t *)addr = (uint32_t) value;
        break;
    case 3:
        *(uint64_t *)addr = (uint64_t) value;
        break;
    }
    memop_cnt[cm_coreid]++;
    tbb_end_write(&mo->lock);
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
