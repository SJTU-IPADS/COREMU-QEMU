#include "dyngen-exec.h"

#include "cm-crew.h"

#define DEBUG_COREMU
#include "coremu-debug.h"

#if DATA_BITS == 64
#  define DATA_TYPE uint64_t
#  define SUFFIX q
#elif DATA_BITS == 32
#  define DATA_TYPE uint32_t
#  define SUFFIX l
#elif DATA_BITS == 16
#  define DATA_TYPE uint16_t
#  define SUFFIX w
#elif DATA_BITS == 8
#  define DATA_TYPE uint8_t
#  define SUFFIX b
#else
#error unsupported data size
#endif

/* These functions are called in ld/st raw, which are guaranteed not to access
 * span 2 page. So each call to the read/write function will only access one
 * object. */

/* For record */

static inline DATA_TYPE glue(record_crew_read, SUFFIX)(const DATA_TYPE *addr)
{
    DATA_TYPE val;
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

    (*memop)++;
    val = *addr;

    tbb_end_read(&mo->lock);

    return val;
}

static inline void glue(record_crew_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    int objid = memobj_id(addr);
    memobj_t *mo = &memobj[objid];

    tbb_start_write(&mo->lock);
    if (mo->owner != cm_coreid) {
        /* We increase own privilege here. */
        record_write_crew_fault(mo->owner, objid);
        mo->owner = cm_coreid;
    }
    *addr = val;
    (*memop_cnt)++;
    tbb_end_write(&mo->lock);
}

/* For replay */

static inline DATA_TYPE glue(replay_crew_read, SUFFIX)(const DATA_TYPE *addr)
{
    DATA_TYPE val;
    while (incop[LOGENT_MEMOP] == *memop + 1)
        apply_replay_inclog();

    val = *addr;
    (*memop)++;

    return val;
}

static inline void glue(replay_crew_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    while (incop[LOGENT_MEMOP] == *memop + 1)
        apply_replay_inclog();

    *addr = val;
    (*memop)++;
}

/* Call record or replay depending on the run mode. */

DATA_TYPE glue(cm_crew_read, SUFFIX)(const DATA_TYPE *addr)
{
    if (cm_run_mode == CM_RUNMODE_RECORD)
        return glue(record_crew_read, SUFFIX)(addr);
    else if (cm_run_mode == CM_RUNMODE_REPLAY)
        return glue(replay_crew_read, SUFFIX)(addr);
    else
        return *addr;
}

void glue(cm_crew_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    if (cm_run_mode == CM_RUNMODE_RECORD)
        glue(record_crew_write, SUFFIX)(addr, val);
    else if (cm_run_mode == CM_RUNMODE_REPLAY)
        glue(replay_crew_write, SUFFIX)(addr, val);
    else
        *addr = val;
}

#undef DATA_BITS
#undef DATA_TYPE
#undef SUFFIX
