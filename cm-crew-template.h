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
    memobj_t *mo = cm_read_lock(addr);
    DATA_TYPE val = *addr;
    if (cm_is_in_tc)
        (*memop)++;
    cm_read_unlock(mo);

    return val;
}

static inline void glue(record_crew_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    memobj_t *mo = cm_write_lock(addr);
    *addr = val;
    if (cm_is_in_tc)
        (*memop)++;
    cm_write_unlock(mo);
}

/* For replay */

static inline DATA_TYPE glue(replay_crew_read, SUFFIX)(const DATA_TYPE *addr)
{
    cm_apply_replay_log();

    DATA_TYPE val = *addr;
    if (cm_is_in_tc)
        (*memop)++;

    return val;
}

static inline void glue(replay_crew_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    cm_apply_replay_log();

    *addr = val;
    if (cm_is_in_tc)
        (*memop)++;
}

/* Call record or replay depending on the run mode. */

DATA_TYPE glue(cm_crew_read, SUFFIX)(const DATA_TYPE *addr)
{
    if (!cm_is_in_tc) {
        return *addr;
    }

    //debug_mem_access(addr, 'r', cm_is_in_tc);
    if (cm_run_mode == CM_RUNMODE_RECORD)
        return glue(record_crew_read, SUFFIX)(addr);
    else if (cm_run_mode == CM_RUNMODE_REPLAY)
        return glue(replay_crew_read, SUFFIX)(addr);
    else
        return *addr;
}

void glue(cm_crew_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    if (!cm_is_in_tc) {
        *addr = val;
        return;
    }

    //debug_mem_access(addr, 'w', cm_is_in_tc);
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
