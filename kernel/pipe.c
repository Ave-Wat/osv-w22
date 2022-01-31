#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/kmalloc.h>
#include <kernel/thread.h>
#include <kernel/list.h>
#include <kernel/fs.h>
#include <lib/errcode.h>
#include <kernel/pipe.h>

static struct kmeme_cache *pipe_allocator;

static struct file_operations pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .close = pipe_close
};

static struct pipe*
pipe_init()
{
    struct pipe *p;

    struct file *f = fs_alloc_file();

    if(f == NULL) {
        return ERR_FAULT;
    }
    if (pipe_allocator == NULL) {
        if ((pipe_allocator = kmem_cache_allocator(sizeof(struct pipe))) == NULL) {
        return NULL;
        }
    }

    if ((p = kmem_cache_alloc(pipe_allocator)) == NULL) {
        return NULL;
    }

    p->front = 0;
    p->next_empty = 0;
    spinlock_init(&p->lock);
    condvar_init(&p->data_written);
    condvar_init(&p->data_read);
    // TODO open these two files
    p->read_open = True;
    p->write_open = True;
    f->info = p;
}

void 
pipe_free(struct pipe *p) 
{
    kmem_cache_free(pipe_allocator, p);
}

static 
ssize_t pipe_write(struct file *file, void *buf, size_t count, offset_t *ofs)
{
    struct pipe *p = file->info;

    // if read end is not open, return an error
    if (!p->read_open){
        //return error
    }

    spinlock_acquire(&p->lock);

    // TODO should we cast count?
    for (int i = 0; i < count; i++){
        // if full, wait until a read occurs
        while ((p->next_empty - p->front) == MAX_SIZE) {
            condvar_wait(&p->data_read, &p->lock);
        }
        p->data[(i + p->next_empty) % MAX_SIZE] = buf[i];
        p->next_empty++;
    }

    condvar_signal(&p->data_written);
    spinlock_release(&p->lock);

}

static ssize_t 
pipe_read(struct file *file, void *buf, size_t count, offset_t *ofs)
{
    struct pipe *p = file->info;
    char data[MAX_SIZE];

    spinlock_acquire(&p->lock);
    while (p->front == p->next_empty) {
        condvar_wait(&p->data_written, &p->lock);
    }
    
    data = p->data[p->front % MAX_SIZE];
    p->front++;
    condvar_signal(&p->data_read);
    spinlock_release(&p->lock);
    return data;
}
