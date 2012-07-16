#ifndef _CM_CREW_H
#define _CM_CREW_H

#include "coremu-spinlock.h"
#include "coremu-atomic.h" /* For barrier and cpu_relax */
#include "cm-replay.h"
#include "cm-mapped-log.h"

#include "rwlock.h"

#include <pthread.h>

#define DEBUG_COREMU
#include "coremu-debug.h"

//#define DEBUG_MEMCNT
//#define WRITE_RECORDING_AS_FUNC
#define FAST_MEMOBJID
#define LAZY_LOCK_RELEASE

#ifdef DEBUG_MEMCNT
extern __thread MappedLog acc_version_log;
#endif

#define STAT_RETRY_CNT
#ifdef STAT_RETRY_CNT
extern int stat_retry_cnt;
extern __thread long retry_cnt;
#endif

/* Now we track memory as MEMOBJ_SIZE shared object, each object will have a
 * memobj_t tracking its ownership */
#define MEMOBJ_SIZE 4096
#define MEMOBJ_SHIFT 12

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
    cpuid_t owner;
    uint16_t write_cnt; /* Number of writes after acquiring the lock. */
    uint16_t read_cnt; /* Number of writes after acquiring the lock. */
} memobj_t;

typedef struct {
    /* field order is important */
    version_t version;
    memop_t memop;
} last_memobj_t;

extern __thread int cm_is_in_tc;

/* Used in record mode where we only need memop of the current CPU. */
extern __thread memop_t memop;
/* Used in replay mode. */
extern memop_t **memop_cnt;

/* Array holding locked memory object */
extern __thread memobj_t **locked_memobj;
extern __thread int n_locked_memobj;
#define MAX_LOCKED_MEMOBJ 256

extern memobj_t *memobj; /* Used during recording. */
extern __thread last_memobj_t *last_memobj;

extern version_t *obj_version; /* Used during replay. */

void cm_crew_init(void);
void cm_crew_core_init(void);
void cm_crew_core_finish(void);

extern __thread unsigned long last_addr;
extern __thread objid_t last_id;

#ifdef FAST_MEMOBJID
static inline objid_t memobj_id(const void *addr)
{
    return ((long)addr >> 12) & 0xfffff;
}
#else
objid_t __memobj_id(unsigned long addr);
static inline objid_t memobj_id(const void *addr)
{
    unsigned long page_addr = (unsigned long)addr & ~0xFFF;
    if (last_addr != page_addr) {
        last_id = __memobj_id(page_addr);
        last_addr = page_addr;
    }
    return last_id;
}
#endif

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

static inline wait_version_t *next_version_log(void)
{
    return (wait_version_t *)next_log_entry(&version_log, sizeof(wait_version_t));
}

static inline void log_wait_version(version_t ver)
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

static inline rec_wait_memop_t *next_memop_log(void)
{
    return (rec_wait_memop_t *)next_log_entry(&memop_log, sizeof(rec_wait_memop_t));
}

static inline void log_other_wait_memop(objid_t objid, last_memobj_t *last)
{
    rec_wait_memop_t *wm = next_memop_log();

    wm->objid = objid;
    wm->version = last->version;
    wm->memop = last->memop;
}

static inline void log_order(objid_t objid, version_t ver,
        last_memobj_t *last)
{
    log_wait_version(ver);
    log_other_wait_memop(objid, last);
}

void cm_release_acquired_locks(void);

#ifdef DEBUG_MEMCNT
static inline void log_acc_version(version_t ver, objid_t objid)
{
    version_t *pv = (version_t *)next_log_entry(&acc_version_log, sizeof(version_t));
    *pv = ver;
}

static inline version_t read_acc_version(void)
{
    version_t *pv = (version_t *)acc_version_log.buf;
    version_t ver = *pv;
    acc_version_log.buf = (char *)(pv + 1);
    return ver;
}

static inline void print_acc_info(version_t version, objid_t objid, const char *acc)
{
    if (48 <= memop && memop <= 50) {
        coremu_debug("core %d %s memop %ld obj %d @%ld", (int)cm_coreid, acc,
                memop, objid, version);
        //coremu_backtrace();
    }
}

static inline void check_acc_version(objid_t objid, const char *acc)
{
    version_t ver = read_acc_version();
    if (ver != obj_version[objid]) {
        coremu_debug("core %d %s memop %ld obj %d recorded version = %ld, actual = %ld\n",
                cm_coreid, acc, memop, objid, ver, obj_version[objid]);
        pthread_exit(NULL);
    }
}
#endif

uint8_t  cm_crew_record_readb(const  uint8_t *addr, objid_t);
uint16_t cm_crew_record_readw(const uint16_t *addr, objid_t);
uint32_t cm_crew_record_readl(const uint32_t *addr, objid_t);
uint64_t cm_crew_record_readq(const uint64_t *addr, objid_t);

void cm_crew_record_writeb(uint8_t  *addr, objid_t,  uint8_t val);
void cm_crew_record_writew(uint16_t *addr, objid_t, uint16_t val);
void cm_crew_record_writel(uint32_t *addr, objid_t, uint32_t val);
void cm_crew_record_writeq(uint64_t *addr, objid_t, uint64_t val);

extern void *cm_crew_read_func[3][4];
extern void *cm_crew_write_func[3][4];

/* Inline function is slower than directly putting the code in. */
#ifdef WRITE_RECORDING_AS_FUNC
#define __inline__ inline __attribute__((always_inline))
static __inline__ version_t __cm_crew_record_start_write(memobj_t *mo)
{
#ifdef NO_LOCK
#elif defined(USE_RWLOCK)
    tbb_start_write(&mo->rwlock);
#else
#  ifndef LAZY_LOCK_RELEASE
    coremu_spin_lock(&mo->write_lock);
#  else // LAZY_LOCK_RELEASE
    // Release all acquired locks to avoid deadlock.
    if (coremu_spin_trylock(&mo->write_lock) == BUSY) {
        cm_release_acquired_locks();
        coremu_spin_lock(&mo->write_lock);
    }
    // Add the locked memobj to the array for release later
    assert(n_locked_memobj < MAX_LOCKED_MEMOBJ);
    locked_memobj[n_locked_memobj++] = mo;
    mo->owner = cm_coreid;
#  endif // LAZY_LOCK_RELEASE
#endif

    version_t version = mo->version;
    barrier();
    mo->version++;
    return version;
}

static __inline__ void cm_crew_record_end_write(memobj_t *mo, objid_t objid, version_t version)
{
#ifdef NO_LOCK
#elif defined(USE_RWLOCK)
    /* rwlock uses atoimc instructions, so no extra barrier needed. */
    tbb_end_write(&mo->rwlock);
#else
#  ifndef LAZY_LOCK_RELEASE
    mo->version++;
    coremu_spin_unlock(&mo->write_lock);
#  else
#  endif // LAZY_LOCK_RELEASE
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
}

#define cm_crew_record_start_write(mo, version) \
    version = __cm_crew_record_start_write(mo)

#else /* WRITE_RECORDING_AS_FUNC */

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
    last->version = version + 2;

#endif

/**********************************************************************
 * Replay
 **********************************************************************/

extern __thread wait_version_t wait_version;

static inline void read_next_version_log(void)
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

static inline void wait_object_version(objid_t objid)
{
    if (*memop_cnt[cm_coreid] == wait_version.memop) {
        while (obj_version[objid] < wait_version.version) {
            cpu_relax();
        }

        if (obj_version[objid] != wait_version.version) {
            coremu_debug("core %d memop %ld obj %d wait @%ld actual @%ld", (int)cm_coreid,
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

static inline wait_memop_t *next_wait_memop(objid_t objid) {
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

static inline void wait_memop(objid_t objid)
{
    wait_memop_t *log;
    while ((log = next_wait_memop(objid)) != NULL) {
        while (*memop_cnt[log->coreid] <= log->memop) {
            cpu_relax();
        }
    }
}

uint8_t  cm_crew_replay_readb(const  uint8_t *addr, objid_t);
uint16_t cm_crew_replay_readw(const uint16_t *addr, objid_t);
uint32_t cm_crew_replay_readl(const uint32_t *addr, objid_t);
uint64_t cm_crew_replay_readq(const uint64_t *addr, objid_t);

void cm_crew_replay_writeb(uint8_t *addr,  objid_t objid, uint8_t val);
void cm_crew_replay_writew(uint16_t *addr, objid_t objid, uint16_t val);
void cm_crew_replay_writel(uint32_t *addr, objid_t objid, uint32_t val);
void cm_crew_replay_writeq(uint64_t *addr, objid_t objid, uint64_t val);

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
    version_t __version = cm_start_atomic_insn(__mo, __objid)

#define CM_END_ATOMIC_INSN(value) \
    cm_end_atomic_insn(__mo, __objid, __version, value)

static inline version_t cm_start_atomic_insn(memobj_t *mo, objid_t objid)
{
    assert(cm_is_in_tc);
    version_t version;

    switch (cm_run_mode) {
    case CM_RUNMODE_RECORD:
#ifdef LAZY_LOCK_RELEASE
        if (mo->owner == cm_coreid) {
            mo->write_cnt++;
            break;
        }
#endif
        cm_crew_record_start_write(mo, version);
        break;
    case CM_RUNMODE_REPLAY:
        wait_object_version(objid);
        wait_memop(objid);
#ifdef DEBUG_MEMCNT
        return obj_version[objid];
#endif
        break;
    }
    return version;
}

static inline void cm_end_atomic_insn(memobj_t *mo, objid_t objid,
        version_t version, uint64_t val)
{
    (void)val;

    if (cm_run_mode == CM_RUNMODE_RECORD) {
        cm_crew_record_end_write(mo, objid, version);
    } else {
#ifdef DEBUG_MEMCNT
        check_acc_version(objid, "atomic");
#endif
        obj_version[objid] += 2;
    }
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "atomic_write");
#endif
}

/* The atomic read insn function are for ARM target. */
static inline version_t cm_start_atomic_read_insn(memobj_t *mo, objid_t objid)
{
    version_t version = -1;

    switch (cm_run_mode) {
    case CM_RUNMODE_RECORD:
        printf("not implemented\n");
        assert(0);
        break;
    case CM_RUNMODE_REPLAY:
        wait_object_version(objid);
        break;
    }
    return version;
}

static inline void cm_end_atomic_read_insn(memobj_t *mo, objid_t objid, uint64_t val)
{
    (void)val;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    debug_mem_access(val, objid, "atomic_read");
#endif
}

#endif /* _CM_CREW_H */
