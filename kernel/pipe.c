#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/kmalloc.h>
#include <kernel/thread.h>
#include <kernel/list.h>
#include <kernel/fs.h>
#include <lib/errcode.h>
#include <kernel/pipe.h>


static struct kmem_cache *pipe_allocator;

static struct file_operations pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .close = pipe_close
};

int 
pipe_init(int* fds)
{
    struct pipe *p;

    struct file *read_file = fs_alloc_file();
    struct file *write_file = fs_alloc_file();

    struct proc *process = proc_current();

    bool read_spot_found = False;
    bool write_spot_found = False;
    for(int i = 0; i < PROC_MAX_FILE; i++){
        if(process->fileTable[i] == NULL){
            process->fileTable[i] = read_file;
            fds[0] = i;
            read_spot_found = True;
        }
    }

    for(int i = 0; i < PROC_MAX_FILE; i++){
        if(process->fileTable[i] == NULL){
            process->fileTable[i] = write_file;
            fds[1] = i;
            write_spot_found = True;
        }
    }

    if (!(read_spot_found && write_spot_found)){
        return ERR_NOMEM;
    }

    if (pipe_allocator == NULL) {
        if ((pipe_allocator = kmem_cache_create(sizeof(struct pipe))) == NULL) {
            return NULL;
        }
    }

    if ((p = kmem_cache_alloc(pipe_allocator)) == NULL) {
        return NULL;
    }

    spinlock_init(&p->lock);
    condvar_init(&p->data_written);
    condvar_init(&p->data_read);

    p->read_open = True;
    p->write_open = True;
    p->front = 0;
    p->next_empty = 0;

    read_file->info = p;
    write_file->info = p;

    read_file->f_ops = &pipe_ops;
    write_file->f_ops = &pipe_ops;

    read_file->oflag = FS_RDONLY;
    write_file->oflag = FS_WRONLY;
    
    return ERR_OK;
}

void 
pipe_free(struct pipe *p) 
{
    kmem_cache_free(pipe_allocator, p);
}
 
ssize_t pipe_write(struct file *file, const void *buf, size_t count, offset_t *ofs)
{
    struct pipe *p = file->info;
    int bytes_wrote = 0;
    // if read end is not open, return error
    if (!p->read_open){
        return ERR_END;
    }

    spinlock_acquire(&p->lock);
    // write count bytes to the buffer. if full, wait until a read occurs
    for (int i = 0; i < (int)count; i++){
        while ((p->next_empty - p->front) == MAX_SIZE) {
            condvar_wait(&p->data_read, &p->lock);
        }
        p->data[(p->next_empty) % MAX_SIZE] = ((char*)buf)[i];
        p->next_empty++;
        bytes_wrote++;
    }
    condvar_signal(&p->data_written);
    spinlock_release(&p->lock);
    //kprintf(bytes_wrote);
    return bytes_wrote;
}

ssize_t 
pipe_read(struct file *file, void *buf, size_t count, offset_t *ofs)
{
    struct pipe *p = file->info;
    int bytes_read = 0;
    // if write end is not open, return 0 if pipe is empty. Otherwise, read up to count bytes and 
    // return number of bytes read
    if (!p->write_open){
        spinlock_acquire(&p->lock);
        if (p->next_empty == p->front){
            spinlock_release(&p->lock);
            //kprintf("return 0\n");
            return 0;
        }
        for (int i = 0; i < (int)count; i++){
            if (p->next_empty == p->front){
                break;
            }
            ((char*)buf)[i] = p->data[(p->front) % MAX_SIZE];
            p->front++;
            bytes_read++;
        }
        spinlock_release(&p->lock);
        //kprintf((char *)bytes_read);
        return bytes_read;
    }

    // the default case. Read up to count bytes, and wait for more data to be written if necessary.
    spinlock_acquire(&p->lock);
    for (int i = 0; i < count; i++){
        while (p->next_empty == p->front) {
            condvar_wait(&p->data_written, &p->lock);
        }
        ((char*)buf)[i] = p->data[(p->front) % MAX_SIZE];
        p->front++;
    }
    condvar_signal(&p->data_read);
    spinlock_release(&p->lock);
    return bytes_read;
}

void pipe_close(struct file *f){
    kprintf("inside pipe close");
    struct pipe *p = f->info;
    if(f->oflag == FS_RDONLY){
        p->read_open = False;
    } else if (f->oflag == FS_WRONLY){
        p->write_open = False;
    }
    
    struct proc *process = proc_current();

    for(int i = 0; i < PROC_MAX_FILE; i++){
        if(process->fileTable[i] == f){
            process->fileTable[i] = NULL;
        }
    }

    if(!p->read_open && !p->write_open){
        pipe_free(p);
    }
}
