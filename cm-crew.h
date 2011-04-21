#ifndef _CM_CREW_H
#define _CM_CREW_H

void cm_crew_init(void);

int64_t cm_crew_read(void *addr, int size);

void cm_crew_write(void *addr, int64_t value, int size);

extern void *cm_crew_read_func[4];
extern void *cm_crew_write_func[4];

uint8_t cm_crew_readb(uint8_t *addr);
uint16_t cm_crew_readw(uint16_t *addr);
uint32_t cm_crew_readl(uint32_t *addr);
uint64_t cm_crew_readq(uint64_t *addr);

void cm_crew_writeb(uint8_t *addr, uint8_t val);
void cm_crew_writew(uint16_t *addr, uint16_t val);
void cm_crew_writel(uint32_t *addr, uint32_t val);
void cm_crew_writeq(uint64_t *addr, uint64_t val);

#endif /* _CM_CREW_H */
