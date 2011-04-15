#ifndef _CM_CREW_H
#define _CM_CREW_H

void cm_crew_init(void);

int64_t cm_crew_read(void *addr, int size);

void cm_crew_write(void *addr, int64_t value, int size);

#endif /* _CM_CREW_H */
