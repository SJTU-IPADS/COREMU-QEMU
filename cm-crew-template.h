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

/* Record read/write functino. */

DATA_TYPE glue(cm_crew_record_read, SUFFIX)(const DATA_TYPE *addr, long objid)
{
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    memobj_t *mo = cm_read_lock(objid);
    DATA_TYPE val = *addr;
    memop++;
    cm_read_unlock(mo);
#ifdef DEBUG_MEM_ACCESS
    debug_read_access(val);
#endif
    return val;
}

void glue(cm_crew_record_write, SUFFIX)(DATA_TYPE *addr, long objid, DATA_TYPE val)
{
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    memobj_t *mo = cm_write_lock(objid);
    *addr = val;
    memop++;
    cm_write_unlock(mo);
#ifdef DEBUG_MEM_ACCESS
    debug_write_access(val);
#endif
}

/* Replay read/write functino. */

DATA_TYPE glue(cm_crew_replay_read, SUFFIX)(const DATA_TYPE *addr)
{
    cm_apply_replay_log();

    DATA_TYPE val = *addr;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_read_access(val);
#endif
    return val;
}

void glue(cm_crew_replay_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    cm_apply_replay_log();

    *addr = val;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_write_access(val);
#endif
}

#undef DATA_BITS
#undef DATA_TYPE
#undef SUFFIX
