#include <stdio.h>
#include <stdint.h>
#include "coremu-logbuffer.h"
#include "coremu-config.h"
#include "coremu-atomic.h"
#include "coremu-intr.h"
#include "coremu-sched.h"

#include "coremu-malloc.h"
#include "cm-memtrace.h"
#include "cm-intr.h"
#include "exec.h"

typedef struct CMMemtraceReq {
    CMIntr *base;
} CMMemtraceReq;

uint64_t global_mem_event_counter;
int memtrace_enable=0;
COREMU_THREAD int flush_cnt;

void memtrace_buf_full(void);
void memtrace_buf_full(void)
{	
	flush_cnt++;
	printf("\033[1;40;32m [COREMU Core %d] Memtrace Buffer Flush %d (%lu Records) \033[0m \n",
	    cpu_single_env->cpu_index,flush_cnt,
	    (cpu_single_env->memtrace_buf->cur-cpu_single_env->memtrace_buf->buf)/8);
	fflush(stdout);
	if(!memtrace_enable)
		flush_cnt=0;

	uint64_t *ptr= cpu_single_env->memtrace_buf->buf;
	uint64_t *end= cpu_single_env->memtrace_buf->cur;

	for(;ptr!=end;ptr+=2){
		if(ptr[1] == 0) {
			printf("Bad Record Check: %016lx %016lx \n",ptr[0],ptr[1]);
			abort();
			return;
		}
	}
	
    coremu_logbuf_flush(cpu_single_env->memtrace_buf);
}

void cm_print_memtrace(FILE *file, uint64_t *buf)
{
    //printf("Print Record : %016lx %016lx \n",buf[0],buf[1]);
	
	if(buf[1] == 0) {
		printf("Bad Record : %016lx %016lx \n",buf[0],buf[1]);
		return;
	}
    uint64_t addr = qemu_ram_addr_from_host((void*)buf[1]);
	uint64_t cnt = buf[0];
	int write=cnt&1;
	
	//fwrite(&buf[0], 8, 1, file);
	//fwrite(&addr, 8, 1, file);
	
	fprintf(file,"%016lx %lx 0 %s\n",cnt,addr,write?"ST":"LD");
}

void memtrace_logging(uint64_t addr, int write)
{	
	return;
	if(!memtrace_enable)
		return;
    CMLogbuf *buffer = cpu_single_env->memtrace_buf;
	uint64_t* buf_ptr = ((uint64_t*)buffer->cur);
	uint64_t cnt = atomic_xadd2(&global_mem_event_counter) | write;
	buf_ptr[1] = (uint64_t)addr;
	buf_ptr[0] = cnt;
	buffer->cur += 16;
	if(buffer->cur == buffer->end){
        memtrace_buf_full();
	}
}

static void tb_flush_handler(void *opaque)
{
    tb_flush(cpu_single_env);
	if(!memtrace_enable)
		memtrace_buf_full();
}

static void broadcast_tb_flush(void)
{
    int cpu_idx;
    for(cpu_idx = 0; cpu_idx < coremu_get_targetcpu(); cpu_idx++) {
		CMIntr *req = coremu_mallocz(sizeof(*req));
		req->handler = tb_flush_handler;
        coremu_send_intr(req, cpu_idx);
    }
}

void memtrace_start(void)
{
	memtrace_enable=1;
	broadcast_tb_flush();
}

void memtrace_stop(void)
{
	memtrace_enable=0;
	broadcast_tb_flush();
}
