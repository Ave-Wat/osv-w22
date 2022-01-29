#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/kmalloc.h>
#include <kernel/thread.h>
#include <kernel/list.h>
#include <kernel/fs.h>
#include <lib/errcode.h>
#include <kernel/pipe.h>

static struct kmeme_cache *pipe_allocator;

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

    f->info = p;
}

void pipe_free(struct pipe *p) {
    kmem_cache_free(pipe_allocator, p);
}

static ssize_t pipe_read(struct file *file, void *buf, size_t count, offset_t *ofs){
    struct pipe *p = file->info;
    spinlock_acquire(&p->lock);
    while ((p->next_empty - p->front) == MAX) {
        condvar_wait(&item_removed, &p->lock);
    }
    p->items[p->next_empty % MAX] = item;
    p->nextEmpty++;
    condvar_signal(&p->item_added);
    spinlock_release(&p->lock);
}