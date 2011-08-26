#ifndef _CM_BUF_H
#define _CM_BUF_H


typedef struct {
    uint8_t     *buffer_head;
    uint8_t     *current;
    uint8_t     *end;
} CMLogBuf;

/* Pipe tramsmission structure */
typedef struct PipeUnit{  
    CMLogBuf    *cm_record_buffer;    
    int         cm_coreid;
    int         cm_record_type;
}CMPipeUnit;

static inline void *get_buffer_entry(CMLogBuf *buf, size_t size)
{
    void *p = buf->current;
    buf->current += size;
    return p;
}

static inline void malloc_new_buffer(CMLogBuf *buf, size_t size)
{
    buf->buffer_head=malloc(size);
    buf->current=buf->buffer_head;
    buf->end=buf->buffer_head+size;
}

/* Need to be modified */
static inline int buffer_is_full(CMLogBuf *buf){
    if (buf->current==buf->end){
            return 1;
        }
    else return 0;
}//(struct crew_log *) clog = get_buffer(buf, 8);
//clog->coreid = 0;

#endif
