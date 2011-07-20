#ifndef _CM_CREW_H
#define _CM_CREW_H

extern __thread volatile uint32_t *memop;
extern volatile uint32_t memop_cnt[4];
extern __thread int cm_is_in_tc;

void cm_crew_init(void);
void cm_crew_core_init(void);

typedef struct memobj_t memobj_t;

/* Acquire lock before read/write operation, record log if necessary.
 * Unlock after operation done. */
memobj_t *cm_read_lock(const void *addr);
void      cm_read_unlock(memobj_t *mo);
memobj_t *cm_write_lock(const void *addr);
void      cm_write_unlock(memobj_t *mo);

void cm_apply_replay_log(void);

extern void *cm_crew_read_func[4];
extern void *cm_crew_write_func[4];

uint8_t cm_crew_readb(const uint8_t *addr);
uint16_t cm_crew_readw(const uint16_t *addr);
uint32_t cm_crew_readl(const uint32_t *addr);
uint64_t cm_crew_readq(const uint64_t *addr);

void cm_crew_writeb(uint8_t *addr, uint8_t val);
void cm_crew_writew(uint16_t *addr, uint16_t val);
void cm_crew_writel(uint32_t *addr, uint32_t val);
void cm_crew_writeq(uint64_t *addr, uint64_t val);

#endif /* _CM_CREW_H */
