#ifndef _CM_CREW_H
#define _CM_CREW_H

#include "coremu-spinlock.h"
#include "coremu-atomic.h" /* For barrier and cpu_relax */
#include "cm-replay.h"
#include "cm-mapped-log.h"

#include "rwlock.h"

#include <pthread.h>
#include <stdbool.h>

#define __inline__ inline __attribute__((always_inline))

//#define DEBUG_MEMCNT
#define WRITE_RECORDING_AS_FUNC
#define LAZY_LOCK_RELEASE

#ifdef DEBUG_MEMCNT
extern __thread MappedLog acc_version_log;
#endif

// 2G memory total
#define MEMORY_TOTAL_BITS 31
#define MEMORY_TOTAL (1 << MEMORY_TOTAL_BITS)

/* Now we track memory as MEMOBJ_SIZE shared object, each object will have a
 * memobj_t tracking its ownership and seqlock. Note it's possible one objid
 * corresponds to multiple shared object. */
#define MEMOBJ_SHIFT 10
#define MEMOBJ_SIZE (1 << MEMOBJ_SHIFT)

// XXX should I increase the object mask bits? thus having more different memobj
#define OBJID_BITS (MEMORY_TOTAL_BITS - MEMOBJ_SHIFT)
//#define OBJID_BITS 21
#define OBJID_CNT (1 << OBJID_BITS)
#define OBJID_MASK (OBJID_CNT - 1)

typedef long memop_t;
typedef long version_t;
typedef int objid_t;

typedef struct {
    version_t version;
#ifdef USE_RWLOCK
    tbb_rwlock_t rwlock;
#else
    CMSpinLock write_lock;
#endif
#ifdef LAZY_LOCK_RELEASE
    cpuid_t owner;
    /* Avoiding adding the same obj to contending array too much times. There
     * may still have duplicates, but it does not harm correctness. */
    /* TODO do this optimization later */
    //bool contending_added;
#endif
} memobj_t;

#define MEMOBJ_STRUCT_BITS 4
extern int memobj_t_wrong_size[sizeof(memobj_t) == (1 << MEMOBJ_STRUCT_BITS) ? 1 : -1];

typedef struct {
    /* field order is important */
    version_t version;
    memop_t memop;
} last_memobj_t;
extern int last_memobj_t_wrong_size[sizeof(last_memobj_t) ==
    (1 << MEMOBJ_STRUCT_BITS) ? 1 : -1];

extern __thread int cm_is_in_tc;

/* Used in record mode where we only need memop of the current CPU. */
extern __thread memop_t memop;
/* Used in replay mode. */
extern memop_t **memop_cnt;

extern memobj_t *memobj; /* Used during recording. */
extern __thread last_memobj_t *last_memobj;

extern version_t *obj_version; /* Used during replay. */

void cm_crew_init(void);
void cm_crew_core_init(void);
void cm_crew_core_finish(void);

static __inline__ objid_t memobj_id(const void *addr)
{
    return ((long)addr >> MEMOBJ_SHIFT) & OBJID_MASK;
}

/* Log */

extern __thread MappedLog memop_log;
extern __thread MappedLog version_log;

typedef struct {
    memop_t memop;
    version_t version;
} wait_version_t;

extern int wait_version_t_wrong_size[sizeof(wait_version_t) == (sizeof(memop_t) +
        sizeof(version_t)) ? 1 : -1];

/**********************************************************************
 * Recording
 **********************************************************************/

static __inline__ wait_version_t *next_version_log(void)
{
    return (wait_version_t *)next_log_entry(&version_log, sizeof(wait_version_t));
}

static __inline__ void log_wait_version(version_t ver)
{
    wait_version_t *wv = next_version_log();
    wv->memop = memop;
    wv->version = ver;
}

/* This struct is only used during recording. */
typedef struct {
    objid_t objid;
    version_t version;
    memop_t memop;
} rec_wait_memop_t;

static __inline__ rec_wait_memop_t *next_memop_log(void)
{
    return (rec_wait_memop_t *)next_log_entry(&memop_log, sizeof(rec_wait_memop_t));
}

static __inline__ void log_other_wait_memop(objid_t objid, last_memobj_t *last)
{
    rec_wait_memop_t *wm = next_memop_log();

    wm->objid = objid;
    wm->version = last->version;
    wm->memop = last->memop;
}

static __inline__ void log_order(objid_t objid, version_t ver,
        last_memobj_t *last)
{
    log_wait_version(ver);
    log_other_wait_memop(objid, last);
}

#ifdef DEBUG_MEMCNT
static __inline__ void log_acc_version(version_t ver, objid_t objid)
{
    version_t *pv = (version_t *)next_log_entry(&acc_version_log, sizeof(version_t));
    *pv = ver;
}

static __inline__ version_t read_acc_version(void)
{
    version_t *pv = (version_t *)acc_version_log.buf;
    version_t ver = *pv;
    acc_version_log.buf = (char *)(pv + 1);
    return ver;
}

static __inline__ void print_acc_info(version_t version, objid_t objid, const char *acc)
{
    if (48 <= memop && memop <= 50) {
        printf("core %d %s memop %ld obj %d @%ld\n", (int)cm_coreid, acc,
                memop, objid, version);
        //coremu_backtrace();
    }
}

static __inline__ void check_acc_version(objid_t objid, const char *acc)
{
    version_t ver = read_acc_version();
    if (ver != obj_version[objid]) {
        printf("core %d %s memop %ld obj %d recorded version = %ld, actual = %ld\n",
                cm_coreid, acc, memop, objid, ver, obj_version[objid]);
        pthread_exit(NULL);
    }
}
#endif

uint8_t  cm_crew_record_readb(const  uint8_t *addr);
uint16_t cm_crew_record_readw(const uint16_t *addr);
uint32_t cm_crew_record_readl(const uint32_t *addr);
uint64_t cm_crew_record_readq(const uint64_t *addr);

void cm_crew_record_writeb(uint8_t  *addr,  uint8_t val);
void cm_crew_record_writew(uint16_t *addr, uint16_t val);
void cm_crew_record_writel(uint32_t *addr, uint32_t val);
void cm_crew_record_writeq(uint64_t *addr, uint64_t val);

extern void *cm_crew_read_func[3][4];
extern void *cm_crew_write_func[3][4];

#ifdef LAZY_LOCK_RELEASE

uint8_t  cm_crew_record_lazy_readb(const  uint8_t *addr, objid_t, memobj_t *);
uint16_t cm_crew_record_lazy_readw(const uint16_t *addr, objid_t, memobj_t *);
uint32_t cm_crew_record_lazy_readl(const uint32_t *addr, objid_t, memobj_t *);
uint64_t cm_crew_record_lazy_readq(const uint64_t *addr, objid_t, memobj_t *);

extern void *cm_crew_read_lazy_func[4];

/* Using size of 2's power, we can get the index by simply & to get the lowest
 * bits. */
/* XXX size of maximum holding lock may impact performance. Holding too much
 * no longer unused memobj's locks will decrease performance of other vCPUs. */
#define MAX_LOCKED_MEMOBJ 32
#define LOCKED_MEMOBJ_IDX_MASK (MAX_LOCKED_MEMOBJ - 1)

#define MAX_CONTENDING_CORE 128
#define CONTENDING_CORE_IDX_MASK (MAX_CONTENDING_CORE - 1)

struct crew_state_t {
    /* Circular array holding locked memory object */
    memobj_t *locked_memobj[MAX_LOCKED_MEMOBJ];
    uint8_t locked_memobj_idx;

    /* Array holding the core id which is contending memobj.
     *
     * Multiple producer, one consumer. So producer should use xadd to get the
     * avaiable slot.
     *
     * Producer will wait until the item in the array is processed (as it's a
     * lock), this means each producer will have at most 1 item in the array. So
     * there will be at most #cpus items in the array. This property makes parallel
     * access to the array easier to implement:
     *
     * 1. As item number is bounded, so producer don't need to check if the array is full
     * */
    struct {
        cpuid_t core[MAX_CONTENDING_CORE];
        /* The start index of contending_core for releasing contending memobj the next
         * time. */
        uint8_t core_start_idx;
        uint8_t core_idx; /* Next available contending core idx. */

        /* Array with length #core, each core stores the its contending memobj in it's
         * own slot. */
        memobj_t **memobj;

        // last contending memop for each object
        memop_t *memop;
    } contending;
};
extern __thread struct crew_state_t crew;

/* global state */
struct crew_gstate_t {
    struct {
        cpuid_t **core;
        /* Array contain the contending_core_idx's address */
        uint8_t **core_idx;
        memobj_t ***memobj;
    } contending;
};
extern struct crew_gstate_t crew_g;

static __inline__ void cm_add_contending_memobj(memobj_t *mo)
{
    cpuid_t owner = mo->owner;
    /* It's possible the memobj is released. */
    if (owner == -1) {
        return;
    }

    /* First check if the previous added memobj is handled. */
    if (crew_g.contending.memobj[owner][cm_coreid]) {
        /* It's possible the previous memobj is released but the slot is not
         * cleared, so mo is different with previous mo.
         * In that case, we simply return, the contending memobj will be added
         * when the slot is cleared.  */
        return;
    }

    /* Producer first add memobj, then add core id to array.
     * Consumer first read core id stored in the array, then find memobj and
     * release it.
     * This ensures
     * 1. when the consumer finds a contending core, the contending memobj has
     *    been written.
     * 2. when the producer sees the contending memobj for its slot is NULL, it
     *    can be sure that the previous contending memobj has been released at
     *    least once.
     */
    /* Add memobj to the slot in the owner's contending_memobj array. */
    crew_g.contending.memobj[owner][cm_coreid] = mo;
    /* Add self to the owner's contending core array. */
    uint8_t idx = __sync_fetch_and_add(crew_g.contending.core_idx[owner], 1) &
        CONTENDING_CORE_IDX_MASK;
    crew_g.contending.core[owner][idx] = cm_coreid;

    /*
     *printf("core %d add contending memobj %ld %p owned by %d with idx %d\n", cm_coreid,
     *        mo -  memobj, mo, owner, idx);
     */
}

static __inline__ void cm_release_memobj(memobj_t *mo)
{
    if (mo->owner != cm_coreid)
        return;

    /* Allow reader to continue. */
    version_t version = ++mo->version;
    assert((mo->version & 1) == 0);
    /* Allow writer to continue. */
    mo->owner = -1;
    coremu_spin_unlock(&mo->write_lock);

    objid_t objid = mo - memobj;
    last_memobj[objid].version = version;
}

void cm_release_all_memobj(void);

static __inline__ bool cm_has_contending_memobj(void)
{
    return crew.contending.core_start_idx != crew.contending.core_idx;
}

void __cm_release_contending_memobj(void);
static __inline__ void cm_release_contending_memobj(void)
{
    if (cm_has_contending_memobj()) {
        __cm_release_contending_memobj();
    }
}

static __inline__ void cm_add_locked_memobj(memobj_t *mo)
{
    /* Add the locked memobj to the array for release later */
    crew.locked_memobj_idx = (crew.locked_memobj_idx + 1) & LOCKED_MEMOBJ_IDX_MASK;
    uint8_t idx = crew.locked_memobj_idx;
    if (crew.locked_memobj[idx]) {
        cm_release_memobj(crew.locked_memobj[idx]);
    }
    crew.locked_memobj[idx] = mo;
    mo->owner = cm_coreid;
}

static __inline__ void cm_handle_contention(memobj_t *mo, objid_t objid)
{
    crew.contending.memop[objid] = memop;
    cm_release_contending_memobj();
    /* It's possible the owner changes between successive calls,
     * cm_add_contending_memobj will add the memobj if it's not added. */
    cm_add_contending_memobj(mo);
}

#define LAZY_RELEASE_MEMOP_DIFF 5
static __inline__ int should_lazy_release(objid_t objid)
{
    return (memop - crew.contending.memop[objid]) >= LAZY_RELEASE_MEMOP_DIFF;
}
#endif // LAZY_LOCK_RELEASE

/* Inline function seems slower than directly putting the code in. */
#ifdef WRITE_RECORDING_AS_FUNC
static __inline__ version_t __cm_crew_record_start_write(memobj_t *mo, objid_t objid)
{
#ifdef NO_LOCK
#elif defined(USE_RWLOCK)
    tbb_start_write(&mo->rwlock);
#else
#  ifdef LAZY_LOCK_RELEASE
    while (coremu_spin_trylock(&mo->write_lock) == BUSY) {
        /* When failed to get the spinlock, the owner may have changed, so we
         * need to add the memobj to the owner's contending array again. */
        cm_handle_contention(mo, objid);
    }
#  else // LAZY_LOCK_RELEASE
    coremu_spin_lock(&mo->write_lock);
#  endif // LAZY_LOCK_RELEASE
#endif

    version_t version = mo->version;
    barrier();
    mo->version++;
    return version;
}
#define cm_crew_record_start_write(mo, objid, version) \
    version = __cm_crew_record_start_write(mo, objid)

static __inline__ void cm_crew_record_end_write(memobj_t *mo, objid_t objid,
        version_t version)
{
#ifdef NO_LOCK
    // do nothing
#elif defined(USE_RWLOCK)
    /* rwlock uses atoimc instructions, so no extra barrier needed. */
    tbb_end_write(&mo->rwlock);
#else
#  ifdef LAZY_LOCK_RELEASE
    if (should_lazy_release(objid)) {
        cm_release_contending_memobj();
        cm_add_locked_memobj(mo);
    } else {
        // release lock as usual
        mo->version++;
        coremu_spin_unlock(&mo->write_lock);
    }
#  else
    mo->version++;
    coremu_spin_unlock(&mo->write_lock);
#  endif // LAZY_LOCK_RELEASE
#endif

    last_memobj_t *last = &last_memobj[objid];
    if (last->version != version) {
        log_order(objid, version, last);
    }

    last->memop = memop;
    last->version = version + 2;
    memop++;
#ifdef DEBUG_MEMCNT
    log_acc_version(version, objid);
    print_acc_info(version, objid, "write");
#endif
}

#else /* WRITE_RECORDING_AS_FUNC */

#  ifdef LAZY_LOCK_RELEASE

#define cm_crew_record_start_write(mo, objid, version) \
    while (coremu_spin_trylock(&mo->write_lock) == BUSY) { \
        cm_handle_contention(mo, objid); \
    } \
    version = mo->version; \
    barrier(); \
    mo->version++;

#define cm_crew_record_end_write(mo, objid, version) \
    if (should_lazy_release(objid)) { \
        cm_release_contending_memobj(); \
        cm_add_locked_memobj(mo); \
    } else { \
        mo->version++; \
        coremu_spin_unlock(&mo->write_lock); \
    } \
    if (last->version != version) { \
        log_order(objid, version, last); \
    } \
    last_memobj_t *last = &last_memobj[objid]; \
    last->memop = memop; \
    last->version = version + 2; \
    memop++;

#  else // LAZY_LOCK_RELEASE

#define cm_crew_record_start_write(mo, version) \
    coremu_spin_lock(&mo->write_lock); \
    version = mo->version; \
    barrier(); \
    mo->version++;

#define cm_crew_record_end_write(mo, objid, version) \
    mo->version++; \
    coremu_spin_unlock(&mo->write_lock); \
    last_memobj_t *last = &last_memobj[objid]; \
    if (last->version != version) { \
        log_order(objid, version, last); \
    } \
    last->memop = memop; \
    last->version = version + 2; \
    memop++;

#  endif // LAZY_LOCK_RELEASE

#endif // WRITE_RECORDING_AS_FUNC

/**********************************************************************
 * Replay
 **********************************************************************/

extern __thread wait_version_t wait_version;

static __inline__ void read_next_version_log(void)
{
    wait_version_t *wv = (wait_version_t *)version_log.buf;
    if (wv->memop == -1) {
        /* Assuming no overflow occurs for memop */
        wait_version.memop = -1;
        return;
    }

    wait_version = *wv;
    version_log.buf = (char *)(wv + 1);
}

static __inline__ void wait_object_version(objid_t objid)
{
    if (memop == wait_version.memop) {
        while (obj_version[objid] < wait_version.version) {
            cpu_relax();
        }

        if (obj_version[objid] != wait_version.version) {
            printf("core %d memop %ld obj %d wait @%ld actual @%ld\n", (int)cm_coreid,
                    memop, objid, wait_version.version, obj_version[objid]);
            assert(0);
        }
        //assert(obj_version[objid] == wait_version.version);
        read_next_version_log();
    }
}

typedef struct {
    // Order of field must match with binary log
    version_t version;
    memop_t memop;
    cpuid_t coreid;
} wait_memop_t;

typedef struct {
    wait_memop_t *log;
    int n;
    int size;
} wait_memop_log_t;

extern wait_memop_log_t *wait_memop_log;
extern __thread int *wait_memop_idx;

static __inline__ wait_memop_t *next_wait_memop(objid_t objid) {
    int i;
    wait_memop_t *log = wait_memop_log[objid].log;
    version_t version = obj_version[objid];
    for (i = wait_memop_idx[objid];
         i < wait_memop_log[objid].size && (version > log[i].version || log[i].coreid == cm_coreid);
         ++i);

    if (i < wait_memop_log[objid].size && version == log[i].version) {
        wait_memop_idx[objid] = i + 1;
        return &log[i];
    }
    wait_memop_idx[objid] = i;
    return NULL;
}

static __inline__ void wait_memop(objid_t objid)
{
    wait_memop_t *log;
    while ((log = next_wait_memop(objid)) != NULL) {
        while (*memop_cnt[log->coreid] <= log->memop) {
            cpu_relax();
        }
    }
}

uint8_t  cm_crew_replay_readb(const  uint8_t *addr);
uint16_t cm_crew_replay_readw(const uint16_t *addr);
uint32_t cm_crew_replay_readl(const uint32_t *addr);
uint64_t cm_crew_replay_readq(const uint64_t *addr);

void cm_crew_replay_writeb(uint8_t *addr,  uint8_t val);
void cm_crew_replay_writew(uint16_t *addr, uint16_t val);
void cm_crew_replay_writel(uint32_t *addr, uint32_t val);
void cm_crew_replay_writeq(uint64_t *addr, uint64_t val);

extern void *cm_crew_replay_read_func[4];
extern void *cm_crew_replay_write_func[4];

extern __thread uint32_t tlb_fill_cnt;

void debug_mem_access(uint64_t val, objid_t objid, const char *acc_type);

void cm_assert_not_in_tc(void);

/**********************************************************************
 * atomic instruction
 **********************************************************************/

#define CM_START_ATOMIC_INSN(addr) \
    objid_t __objid = memobj_id((void *)addr); \
    memobj_t *__mo = &memobj[__objid]; \
    last_memobj_t *__last = &last_memobj[__objid]; \
    version_t __version = cm_start_atomic_insn(__mo, __last, __objid)

#define CM_END_ATOMIC_INSN(value) \
    cm_end_atomic_insn(__mo, __last, __objid, __version, value)

static __inline__ version_t cm_start_atomic_insn(memobj_t *mo, last_memobj_t *last,
        objid_t objid)
{
    version_t version;

    switch (cm_run_mode) {
    case CM_RUNMODE_RECORD:
#ifdef LAZY_LOCK_RELEASE
        if (mo->owner == cm_coreid) {
            // Use version -1 to mark as lock already hold.
            version = -1;
            memop++;
            mo->version += 2;
            break;
        }
#endif
        cm_crew_record_start_write(mo, objid, version);
        break;
    case CM_RUNMODE_REPLAY:
        wait_object_version(objid);
        wait_memop(objid);
#ifdef DEBUG_MEMCNT
        version = obj_version[objid];
#endif
        break;
    }
    return version;
}

static __inline__ void cm_end_atomic_insn(memobj_t *mo, last_memobj_t *last,
        objid_t objid, version_t version, uint64_t val)
{
    (void)val;

    if (cm_run_mode == CM_RUNMODE_RECORD) {
#ifdef LAZY_LOCK_RELEASE
        if (version != -1) {
            cm_crew_record_end_write(mo, objid, version);
        }
#else
        cm_crew_record_end_write(mo, objid, version);
#endif
    } else {
#ifdef DEBUG_MEMCNT
        check_acc_version(objid, "atomic");
#endif
        obj_version[objid] += 2;
        memop++;
    }
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "atomic_write");
#endif
}

#endif /* _CM_CREW_H */
