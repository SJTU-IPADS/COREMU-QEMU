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

DATA_TYPE glue(cm_crew_record_read, SUFFIX)(const DATA_TYPE *addr)
{
    objid_t objid = memobj_id(addr);
    DATA_TYPE val;
    version_t version;
    memobj_t *mo = &memobj[objid];

#ifdef LAZY_LOCK_RELEASE
    // XXX Release contending memobj at basic block boundary to avoid fast path
    // overhead. But this may make high contention application perform worse.
    //cm_release_contending_memobj();
    /* If lock already hold, just do the read. */
    if (mo->owner == cm_coreid) {
        assert(mo->write_lock);
        /* Version will not change here. */
        last_memobj[objid].memop = memop;
        memop++;
        return *addr;
    }
#endif

#ifdef NO_LOCK
    version = mo->version;
    val = *addr;
#elif defined(USE_RWLOCK)
    tbb_start_read(&mo->rwlock);
    version = mo->version;
    val = *addr;
    tbb_end_read(&mo->rwlock);
#else
    do {
        version = mo->version;
        while (unlikely(version & 1)) {
#  ifdef LAZY_LOCK_RELEASE
            // lazy lock release will detector DMA error in cm_add_contending_memobj.
            cm_handle_contention(mo, objid);
#  elif defined(DMA_DETECTOR)
            if (mo->owner == cm_ncpus) {
                printf("Guest OS error: core %d accessing DMA memobj %d\n",
                        cm_coreid, objid);
                exit(1);
            }
#  endif
            cpu_relax();
            version = mo->version;
        }
        barrier();

        val = *addr;
        barrier();
    } while (version != mo->version);
#endif // NO_LOCK

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
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    debug_mem_access(val, objid, "read");
#endif
    return val;
}

void glue(cm_crew_record_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    objid_t objid = memobj_id(addr);
    memobj_t *mo = &memobj[objid];

#ifdef LAZY_LOCK_RELEASE
    /* If lock already hold, just do the write. */
    if (mo->owner == cm_coreid) {
        assert(mo->write_lock);
        mo->version += 2;
        last_memobj[objid].memop = ~memop; // Mark last access as write by flip operation
        memop++;
        *addr = val;
        return;
    }
#endif

    version_t version;
    cm_crew_record_start_write(mo, objid, version);

    barrier();
    *addr = val;
    __sync_synchronize();
    barrier();

    cm_crew_record_end_write(mo, objid, version);
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "write");
#endif
}

#ifdef LAZY_LOCK_RELEASE

// This next 2 function is called directly from tcg. When called, the memobj is
// not owned by self.

DATA_TYPE glue(cm_crew_record_lazy_read, SUFFIX)(const DATA_TYPE *addr, objid_t objid,
        memobj_t *mo)
{
    DATA_TYPE val;
    version_t version;

    do {
        version = mo->version;
        while (unlikely(version & 1)) {
            cm_handle_contention(mo, objid);
            cpu_relax();
            version = mo->version;
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
    return val;
}

#endif // LAZY_LOCK_RELEASE
/* Replay read/write functino. */

DATA_TYPE glue(cm_crew_replay_read, SUFFIX)(const DATA_TYPE *addr)
{
    objid_t objid = memobj_id(addr);
    wait_object_version(objid);

    DATA_TYPE val = *addr;
#ifdef DEBUG_MEMCNT
    print_acc_info(obj_version[objid], objid, "read");
    check_acc_version(objid, "read");
#endif
    memop++;
#ifdef DEBUG_MEM_ACCESS
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    coremu_assert(objid < n_memobj, "objid out of range");
    debug_mem_access(val, objid, "read");
#endif
    return val;
}

void glue(cm_crew_replay_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    objid_t objid = memobj_id(addr);
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
    coremu_assert(cm_is_in_tc, "Must in TC execution");
    coremu_assert(objid < n_memobj, "objid out of range");
    debug_mem_access(val, objid, "write");
#endif
}

#undef DATA_BITS
#undef DATA_TYPE
#undef SUFFIX
