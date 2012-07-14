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
#ifndef CONFIG_MEM_ORDER
    assert(0);
#endif
#ifdef DEBUG_MEM_ACCESS
    coremu_assert(cm_is_in_tc, "Must in TC execution");
#endif

    DATA_TYPE val;
    version_t version;
    memobj_t *mo = &memobj[objid];

    /* If lock already hold, just do the write. */
    if (mo->owner == cm_coreid) {
        /* Update data in the same cache line, should be better than updating
         * memop directly. */
        mo->read_cnt++;
        return *addr;
    }

#ifdef NO_LOCK
    version = mo->version;
    val = *addr;
#elif defined(USE_RWLOCK)
    tbb_start_read(&mo->rwlock);
    version = mo->version;
    val = *addr;
    tbb_end_read(&mo->rwlock);
#else
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
#endif

    last_memobj_t *last = &last_memobj[objid];
    if (last->version != version) {
        log_order(objid, version, last);
        last->version = version;
    }

    last->memop = memop;
#ifdef DEBUG_MEMCNT
    log_acc_version(version, objid);
    print_acc_info(version, objid, "read");
#endif
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "read");
#endif
    return val;
}

void glue(cm_crew_record_write, SUFFIX)(DATA_TYPE *addr, objid_t objid, DATA_TYPE val)
{
#ifndef CONFIG_MEM_ORDER
    assert(0);
#endif
#ifdef DEBUG_MEM_ACCESS
    coremu_assert(cm_is_in_tc, "Must in TC execution");
#endif

    version_t version;
    memobj_t *mo = &memobj[objid];

    /* If lock already hold, just do the write. */
    if (mo->owner == cm_coreid) {
        *addr = val;
        mo->write_cnt++;
        return;
    }

#ifdef NO_LOCK
#elif defined(USE_RWLOCK)
    tbb_start_write(&mo->rwlock);
#else
    // Release all acquired locks to avoid deadlock.
    if (coremu_spin_trylock(&mo->write_lock) == BUSY) {
        cm_release_acquired_locks();
        coremu_spin_lock(&mo->write_lock);
    }
    // Add the locked memobj to the array for release later
    assert(n_locked_memobj < MAX_LOCKED_MEMOBJ);
    locked_memobj[n_locked_memobj++] = mo;
    mo->owner = cm_coreid;
#endif

    version = mo->version;
    barrier();
    mo->version++;
    __sync_synchronize(); // XXX is this necessary?
    barrier();
    *addr = val;
    barrier();
    // Reader should not cut in.
    //mo->version++;

#ifdef NO_LOCK
#elif defined(USE_RWLOCK)
    //tbb_end_write(&mo->rwlock);
#else
    //__sync_synchronize();
    //coremu_spin_unlock(&mo->write_lock);
#endif

    last_memobj_t *last = &last_memobj[objid];
    if (last->version != version) {
        log_order(objid, version, last);
    }

    last->memop = memop;
    last->version = version + 2;
#ifdef DEBUG_MEMCNT
    log_acc_version(version, objid);
    print_acc_info(version, objid, "write");
#endif
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "write");
#endif
}

/* Replay read/write functino. */

DATA_TYPE glue(cm_crew_replay_read, SUFFIX)(const DATA_TYPE *addr, objid_t objid)
{
#ifdef DEBUG_MEM_ACCESS
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    coremu_assert(objid < n_memobj, "objid out of range");
#endif

    wait_object_version(objid);

    DATA_TYPE val = *addr;
#ifdef DEBUG_MEMCNT
    print_acc_info(obj_version[objid], objid, "read");
    check_acc_version(objid, "read");
#endif
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "read");
#endif
    return val;
}

void glue(cm_crew_replay_write, SUFFIX)(DATA_TYPE *addr, objid_t objid,
        DATA_TYPE val)
{
#ifdef DEBUG_MEM_ACCESS
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    coremu_assert(objid < n_memobj, "objid out of range");
#endif

    wait_object_version(objid);
    wait_memop(objid);

    *addr = val;
#ifdef DEBUG_MEMCNT
    print_acc_info(obj_version[objid], objid, "write");
    check_acc_version(objid, "write");
#endif
    obj_version[objid] += 2;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "write");
#endif
}

#undef DATA_BITS
#undef DATA_TYPE
#undef SUFFIX
