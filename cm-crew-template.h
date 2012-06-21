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

DATA_TYPE glue(cm_crew_record_read, SUFFIX)(const DATA_TYPE *addr, objid_t objid)
{
    coremu_assert(cm_is_in_tc, "Must in TC execution");

    DATA_TYPE val;
    version_t version;
    memobj_t *mo = &memobj[objid];

    __sync_synchronize();
    do {
repeat:
        version = mo->version;
        if (unlikely(version & 1)) {
            cpu_relax();
            goto repeat;
        }
        barrier();

        val = *addr;
        barrier();
    } while (version != mo->version);

    last_memobj_t *last = &last_memobj[objid];
    if (last->version != version) {
        log_order(objid, version, last);
        last->version = version;
    }

    last->memop = memop;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_read_access(val);
#endif
    return val;
}

void glue(cm_crew_record_write, SUFFIX)(DATA_TYPE *addr, objid_t objid, DATA_TYPE val)
{
    coremu_assert(cm_is_in_tc, "Must in TC execution");

    version_t version;
    memobj_t *mo = &memobj[objid];

    coremu_spin_lock(&mo->write_lock);

    version = mo->version;
    barrier();
    mo->version++;
    barrier();
    *addr = val;
    barrier();
    mo->version++;

    coremu_spin_unlock(&mo->write_lock);

    last_memobj_t *last = &last_memobj[objid];
    if (last->version != version) {
        log_order(objid, version, last);
    }

    last->memop = memop;
    last->version = version + 2;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_write_access(val);
#endif
}

/* Replay read/write functino. */

DATA_TYPE glue(cm_crew_replay_read, SUFFIX)(const DATA_TYPE *addr, objid_t objid)
{
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    coremu_assert(objid < n_memobj, "objid out of range");

    wait_object_version(objid);

    DATA_TYPE val = *addr;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_read_access(val);
#endif
    return val;
}

void glue(cm_crew_replay_write, SUFFIX)(DATA_TYPE *addr, objid_t objid,
        DATA_TYPE val)
{
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    coremu_assert(objid < n_memobj, "objid out of range");

    wait_object_version(objid);
    wait_memop(objid);

    *addr = val;
    obj_version[objid] += 2;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_write_access(val);
#endif
}

#undef DATA_BITS
#undef DATA_TYPE
#undef SUFFIX
