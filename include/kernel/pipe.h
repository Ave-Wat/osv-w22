#ifndef _PIPE_H_
#define _PIPE_H_

#include <kernel/types.h>
#include <kernel/synch.h>
#include <kernel/fs.h>

#define MAX_SIZE 128

struct pipe {
    // Not confident in these two 
    struct condvar read_open;
    struct condvar write_open;

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

struct pipe *init_pipe;

static ssize_t pipe_read(struct file *file, void *buf, size_t count, offset_t *ofs);
static ssize_t pipe_write(struct file *file, const void *buf, size_t count, offset_t *ofs);
static void pipe_close(struct file *p);

static struct file_operations pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .close = pipe_close
};

#endif