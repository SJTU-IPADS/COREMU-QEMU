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

/* These function are called in ld/st raw */

DATA_TYPE glue(cm_crew_read, SUFFIX)(const DATA_TYPE *addr)
{
    DATA_TYPE val[READLOG_N];
    uint16_t owner;
    int objid = memobj_id(addr);
    memobj_t *mo = &memobj[objid];


    tbb_start_read(&mo->lock);

    return *addr;
}

void glue(cm_crew_write, SUFFIX)(DATA_TYPE *addr, DATA_TYPE val)
{
    //coremu_debug("addr = %p, val = %lx", addr, (long)val);
    *addr = val;
}

#undef DATA_BITS
#undef DATA_TYPE
#undef SUFFIX
