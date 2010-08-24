#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "coremu-logbuffer.h"
#include "coremu-config.h"
#include "coremu-atomic.h"
#include "coremu-intr.h"
#include "coremu-sched.h"
#include "coremu-debug.h"
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

__thread FILE *memtrace_log;
__thread CMLogbuf *memtrace_buf;

void memtrace_buf_full(void);
void memtrace_buf_full(void)
{	
	flush_cnt++;
	printf("\033[1;40;32m [COREMU Core %d] Memtrace Buffer Flush %d (%lu Records) \033[0m \n",
	    cpu_single_env->cpu_index,flush_cnt,
	    (memtrace_buf->cur-memtrace_buf->buf)/8);
	fflush(stdout);
	if(!memtrace_enable)
		flush_cnt=0;

	uint64_t *ptr = (uint64_t *)memtrace_buf->buf;
	uint64_t *end = (uint64_t *)memtrace_buf->cur;

	for(;ptr!=end;ptr+=2){
		if(ptr[1] == 0) {
			printf("Bad Record Check: %016lx %016lx \n",ptr[0],ptr[1]);
			abort();
			return;
		}
	}

    coremu_logbuf_flush(memtrace_buf);
}

static void cm_print_memtrace(FILE *file, void *bufv)
{
    //printf("Print Record : %016lx %016lx \n",buf[0],buf[1]);
    uint64_t *buf = (uint64_t *)bufv;
	
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
    CMLogbuf *buffer = memtrace_buf;
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

static void memtrace_start(void)
{
	memtrace_enable=1;
	broadcast_tb_flush();
}

static void memtrace_stop(void)
{
	memtrace_enable=0;
	broadcast_tb_flush();
}

void cm_memtrace_init(int cpuidx)
{
	char filename[255];

	sprintf(filename,"memtrace-core%d.log",cpu_single_env->cpu_index);
    FILE *memtrace_log = fopen(filename, "w");
    if (!memtrace_log) {
        fprintf(stderr, "Can't open memtrace log\n");
        abort();
    }
    memtrace_buf = coremu_logbuf_new(100 * 1024 * 1024 / 16 , 16,
            cm_print_memtrace, memtrace_log);
}

void helper_memtrace_hypercall(void)
{
    target_ulong req = cpu_single_env->regs[R_EAX];
    coremu_debug("memtrace request %ld", req);
    switch (req) {
    case CM_PROFILE_CACHE_START:
		memtrace_start();
		break;
	case CM_PROFILE_CACHE_STOP:
		memtrace_stop();
		break;
    default:
        printf("error hypercall command : %ld", req);
    }
}

