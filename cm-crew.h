#ifndef _CM_CREW_H
#define _CM_CREW_H

#include "coremu-spinlock.h"
#include "coremu-atomic.h" /* For barrier and cpu_relax */
#include "cm-replay.h"
#include "cm-mapped-log.h"
#include <string.h>

/* Now we track memory as MEMOBJ_SIZE shared object, each object will have a
 * memobj_t tracking its ownership */
#define MEMOBJ_SIZE 4096
#define MEMOBJ_SHIFT 12

typedef int memop_t;
typedef int version_t;
typedef int objid_t;

/* Used in record mode where we only need memop of the current CPU. */
extern __thread memop_t memop;
/* Used in replay mode. */
extern memop_t **memop_cnt;

typedef struct {
    volatile version_t version;
    CMSpinLock write_lock;
} memobj_t;

struct last_memobj_t {
    /* field order is important */
    version_t version;
    memop_t memop;
};
//} __attribute___((packed));
typedef struct last_memobj_t last_memobj_t;

extern memobj_t *memobj; /* Used during recording. */
extern __thread last_memobj_t *last_memobj;

extern version_t *obj_version; /* Used during replay. */

extern __thread int cm_is_in_tc;

void cm_crew_init(void);
void cm_crew_core_init(void);
void cm_crew_core_finish(void);

extern __thread unsigned long last_addr;
extern __thread objid_t last_id;

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

/* Log */

extern __thread MappedLog memop_log;
extern __thread MappedLog version_log;

struct WaitVersion {
    memop_t memop;
    version_t version;
} __attribute__((packed));
typedef struct WaitVersion WaitVersion;

/* For recording */

static inline WaitVersion *next_version_log(void)
{
    return (WaitVersion *)next_log_entry(&version_log, sizeof(WaitVersion));
}

static inline void log_wait_version(version_t ver)
{
    WaitVersion *wv = next_version_log();
    wv->memop = memop;
    wv->version = ver;
}

/* This struct is only used during recording. */
typedef struct {
    objid_t objid;
    last_memobj_t last;
} RecWaitMemop;

static inline RecWaitMemop *next_memop_log(void)
{
    return (RecWaitMemop *)next_log_entry(&memop_log, sizeof(RecWaitMemop));
}

static inline void log_other_wait_memop(objid_t objid, last_memobj_t *last)
{
    RecWaitMemop *wm = next_memop_log();

    wm->objid = objid;
    wm->last = *last;
}

static inline void log_order(int objid, version_t ver,
        last_memobj_t *last)
{
    log_wait_version(ver);
    log_other_wait_memop(objid, last);
}

uint8_t  cm_crew_record_readb(const  uint8_t *addr, objid_t);
uint16_t cm_crew_record_readw(const uint16_t *addr, objid_t);
uint32_t cm_crew_record_readl(const uint32_t *addr, objid_t);
uint64_t cm_crew_record_readq(const uint64_t *addr, objid_t);

void cm_crew_record_writeb(uint8_t  *addr, objid_t,  uint8_t val);
void cm_crew_record_writew(uint16_t *addr, objid_t, uint16_t val);
void cm_crew_record_writel(uint32_t *addr, objid_t, uint32_t val);
void cm_crew_record_writeq(uint64_t *addr, objid_t, uint64_t val);

extern void *cm_crew_record_read_func[4];
extern void *cm_crew_record_write_func[4];

/* For replay */

extern __thread WaitVersion wait_version;

static inline void read_next_version_log(void)
{
    WaitVersion *wv = (WaitVersion *)version_log.buf;
    if (wv->memop == -1) {
        /* Assuming no overflow occurs for memop */
        wait_version.memop = -1;
        return;
    }

    wait_version = *wv;
    if (version_log.buf + sizeof(WaitVersion) > version_log.end) {
        version_log.buf = version_log.end;
        version_log.end = version_log.buf + LOG_BUFFER_SIZE;
    }
}

static inline void wait_object_version(objid_t objid)
{
    if (*memop_cnt[cm_coreid] == wait_version.memop) {
        while (obj_version[objid] < wait_version.version) {
            cpu_relax();
        }

        assert(obj_version[objid] == wait_version.version);
        read_next_version_log();
    }
}

typedef struct {
    // Order of field must match with binary log
    version_t version;
    memop_t memop;
    cpuid_t coreid;
} WaitMemop;

typedef struct {
    WaitMemop *log;
    int n;
    int size;
} WaitMemopLog;

extern WaitMemopLog *wait_memop_log;
extern int *wait_memop_idx;

static inline WaitMemop *next_wait_memop(objid_t objid, version_t version) {
    int i;
    WaitMemop *log = wait_memop_log[objid].log;
    for (i = wait_memop_idx[objid]; i <= wait_memop_log[objid].size &&
            (version > log[i].version || log[i].coreid == cm_coreid); ++i);

    if (i <= wait_memop_log[objid].size && version == log[i].version) {
        wait_memop_idx[objid] = i + 1;
        return &log[i];
    }
    return NULL;
}

static inline void wait_memop(objid_t objid)
{
    WaitMemop *log;
    while ((log = next_wait_memop(objid, obj_version[objid])) != NULL) {
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

/* For debug. */

extern __thread uint32_t tlb_fill_cnt;

void debug_read_access(uint64_t val);
void debug_write_access(uint64_t val);

void cm_assert_not_in_tc(void);

/* For atomic instructions */

#define CM_START_ATOMIC_INSN(addr) \
    objid_t __objid = memobj_id((void *)addr); \
    memobj_t *__mo = &memobj[__objid]; \
    version_t __version = cm_start_atomic_insn(__mo, __objid)

#define CM_END_ATOMIC_INSN(value) \
    cm_end_atomic_insn(__mo, __objid, __version, value)

static inline version_t cm_start_atomic_insn(memobj_t *mo, objid_t objid)
{
    version_t version;

    switch (cm_run_mode) {
    case CM_RUNMODE_RECORD:
        coremu_spin_lock(&mo->write_lock);

        version = mo->version;
        barrier();
        mo->version++;
        barrier();
        break;
    case CM_RUNMODE_REPLAY:
        wait_object_version(objid);
        wait_memop(objid);
        break;
    }
    return version;
}

static inline void cm_end_atomic_insn(memobj_t *mo, int objid,
        version_t version, uint64_t val)
{
    (void)val;
    last_memobj_t *last = &last_memobj[objid];

    if (cm_run_mode == CM_RUNMODE_RECORD) {
        mo->version++;

        coremu_spin_unlock(&mo->write_lock);

        if (last->version != version) {
            log_order(objid, version, last);
        }
        last->memop = memop;
        last->version = version + 2;
    }
#ifdef DEBUG_MEM_ACCESS
    if (cm_run_mode != CM_RUNMODE_NORMAL)
        debug_write_access(val);
#endif
    memop++;
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

static inline void cm_end_atomic_read_insn(memobj_t *mo, uint64_t val)
{
    (void)val;
    memop++;
#ifdef DEBUG_MEM_ACCESS
    if (cm_run_mode != CM_RUNMODE_NORMAL)
        debug_read_access(val);
#endif
}

#endif /* _CM_CREW_H */
