#ifndef _LOG_H
#define _LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>

enum { LOG_BUFFER_SIZE = 4 * 1024 * 1024 };

typedef struct {
    char *buf;
    char *end;
    int fd;
} MappedLog;

int new_mapped_log(const char *name, int id, MappedLog *log);
int enlarge_mapped_log(MappedLog *log);

// The following two function return 0 on success.
int open_mapped_log(const char *name, int id, MappedLog *log);
int open_mapped_log_path(const char *path, MappedLog *log);

int unmap_log(void *start, off_t size);

void *create_mapped_file(const char *name, unsigned long size);

static inline char *next_log_entry(MappedLog *log, int entry_size) {
    if ((log->buf + entry_size) > log->end) {
        enlarge_mapped_log(log);
    }
    char *start = log->buf;
    log->buf += entry_size;
    return start;
}

#ifdef __cplusplus
}
#endif

#endif
