#ifndef _CM_CREW_H
#define _CM_CREW_H

extern __thread volatile uint32_t *memop;
extern volatile uint32_t *memop_cnt;
extern __thread int cm_is_in_tc;

extern __thread uint32_t tlb_fill_cnt;

void cm_crew_init(void);
void cm_crew_core_init(void);

typedef struct memobj_t memobj_t;

extern __thread unsigned long last_addr;
extern __thread long last_id;

long __memobj_id(unsigned long addr);
static inline long memobj_id(const void *addr)
{
    unsigned long page_addr = (unsigned long)addr & ~0xFFF;
    if (last_addr != page_addr) {
        last_id = __memobj_id(page_addr);
        last_addr = page_addr;
    }
    return last_id;
}

/* Acquire lock before read/write operation, record log if necessary.
 * Unlock after operation done. */

memobj_t *cm_read_lock(long objid);
void      cm_read_unlock(memobj_t *mo);
memobj_t *cm_write_lock(long objid);
void      cm_write_unlock(memobj_t *mo);

void cm_apply_replay_log(void);

uint8_t  cm_crew_record_readb(const  uint8_t *addr, long);
uint16_t cm_crew_record_readw(const uint16_t *addr, long);
uint32_t cm_crew_record_readl(const uint32_t *addr, long);
uint64_t cm_crew_record_readq(const uint64_t *addr, long);

void cm_crew_record_writeb(uint8_t  *addr, long,  uint8_t val);
void cm_crew_record_writew(uint16_t *addr, long, uint16_t val);
void cm_crew_record_writel(uint32_t *addr, long, uint32_t val);
void cm_crew_record_writeq(uint64_t *addr, long, uint64_t val);

uint8_t  cm_crew_replay_readb(const  uint8_t *addr);
uint16_t cm_crew_replay_readw(const uint16_t *addr);
uint32_t cm_crew_replay_readl(const uint32_t *addr);
uint64_t cm_crew_replay_readq(const uint64_t *addr);

void cm_crew_replay_writeb(uint8_t *addr,  uint8_t val);
void cm_crew_replay_writew(uint16_t *addr, uint16_t val);
void cm_crew_replay_writel(uint32_t *addr, uint32_t val);
void cm_crew_replay_writeq(uint64_t *addr, uint64_t val);

/* General function for both read and write. */

uint8_t cm_crew_readb(const uint8_t *addr);
uint16_t cm_crew_readw(const uint16_t *addr);
uint32_t cm_crew_readl(const uint32_t *addr);
uint64_t cm_crew_readq(const uint64_t *addr);

void cm_crew_writeb(uint8_t *addr, uint8_t val);
void cm_crew_writew(uint16_t *addr, uint16_t val);
void cm_crew_writel(uint32_t *addr, uint32_t val);
void cm_crew_writeq(uint64_t *addr, uint64_t val);

extern void *cm_crew_record_read_func[4];
extern void *cm_crew_record_write_func[4];
extern void *cm_crew_replay_read_func[4];
extern void *cm_crew_replay_write_func[4];

/* For debug. */

void debug_read_access(uint64_t val);
void debug_write_access(uint64_t val);

void cm_assert_not_in_tc(void);

#endif /* _CM_CREW_H */
