#ifndef _PIPE_H_
#define _PIPE_H_

#include <kernel/types.h>
#include <kernel/synch.h>
#include <kernel/fs.h>

#define MAX_SIZE 512

struct pipe {
    bool read_open;
    bool write_open;

    // Below is for the blocking bounded queue
    // BBQ Synchronization variables
    struct spinlock lock;
    struct condvar data_written;
    struct condvar data_read;
    // BBQ State variables
    char data[MAX_SIZE];
    int front;
    int next_empty;

};

//void init_pipe(void);

ssize_t pipe_read(struct file *file, void *buf, size_t count, offset_t *ofs);
ssize_t pipe_write(struct file *file, const void *buf, size_t count, offset_t *ofs);
void pipe_close(struct file *f);
int pipe_init(int* fds);

#endif