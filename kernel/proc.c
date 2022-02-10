#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/kmalloc.h>
#include <kernel/thread.h>
#include <kernel/list.h>
#include <kernel/fs.h>
#include <kernel/vpmap.h>
#include <arch/elf.h>
#include <arch/trap.h>
#include <arch/mmu.h>
#include <lib/errcode.h>
#include <lib/stddef.h>
#include <lib/string.h>

List ptable; // process table
struct spinlock ptable_lock;
struct spinlock pid_lock;
static int pid_allocator;
struct kmem_cache *proc_allocator;

void proc_free(struct proc* p);
/* go through process table */
static void ptable_dump(void);
/* helper function for loading process's binary into its address space */ 
static err_t proc_load(struct proc *p, char *path, vaddr_t *entry_point);
/* helper function to set up the stack */
static err_t stack_setup(struct proc *p, char **argv, vaddr_t* ret_stackptr);
/* tranlsates a kernel vaddr to a user stack address, assumes stack is a single page */
#define USTACK_ADDR(addr) (pg_ofs(addr) + USTACK_UPPERBOUND - pg_size);

// get a process by its PID.
struct proc* get_proc_by_pid(int pid){
    for (Node *n = list_begin(&ptable); n != list_end(&ptable); n = list_next(n)) {
        struct proc *p = list_entry(n, struct proc, proc_node);
        if (p->pid == pid){
            return p;
        }
    }
    return NULL;
}

// finds a child of the process that is passed in. Can be still running or exited
int find_child_pid(struct proc* parent){
    for (Node *n = list_begin(&ptable); n != list_end(&ptable); n = list_next(n)) {
        struct proc *p = list_entry(n, struct proc, proc_node);
        if (p->parent == parent){
            return p->pid;
        }
    }
    return NULL;
}

static struct proc*
proc_alloc()
{
    struct proc* p = (struct proc*) kmem_cache_alloc(proc_allocator);
    if (p != NULL) {
        spinlock_acquire(&pid_lock);
        p->pid = pid_allocator++;
        spinlock_release(&pid_lock);
    }
    return p;
}

#pragma GCC diagnostic ignored "-Wunused-function"
static void
ptable_dump(void)
{
    spinlock_acquire(&ptable_lock);
    for (Node *n = list_begin(&ptable); n != list_end(&ptable); n = list_next(n)) {
        struct proc *p = list_entry(n, struct proc, proc_node);
        kprintf("Process %s: pid %d\n", p->name, p->pid);
    }
    spinlock_release(&ptable_lock);
}

void
proc_free(struct proc* p)
{
    kmem_cache_free(proc_allocator, p);
}

void
proc_sys_init(void)
{
    list_init(&ptable);
    spinlock_init(&ptable_lock);
    spinlock_init(&pid_lock);
    proc_allocator = kmem_cache_create(sizeof(struct proc));
    kassert(proc_allocator);
}

/*
 * Allocate and initialize basic proc structure
*/
static struct proc*
proc_init(char* name)
{
    struct super_block *sb;
    inum_t inum;
    err_t err;

    struct proc *p = proc_alloc();
    if (p == NULL) {
        return NULL;
    }
    if (as_init(&p->as) != ERR_OK) {
        proc_free(p);
        return NULL;
    }

    size_t slen = strlen(name);
    slen = slen < PROC_NAME_LEN-1 ? slen : PROC_NAME_LEN-1;
    memcpy(p->name, name, slen);
    p->name[slen] = 0;

    list_init(&p->threads);

	// cwd for all processes are root for now
    sb = root_sb;
	inum = root_sb->s_root_inum;
    if ((err = fs_get_inode(sb, inum, &p->cwd)) != ERR_OK) {
        as_destroy(&p->as);
        proc_free(p);
        return NULL;
    }

    // set default exit status
    p->exit_status = STATUS_ALIVE;
    p->parent = NULL;
    // initialize condvar
    condvar_init(&p->wait_cv);

    // initialize fileTable
    p->fileTable[0] = &stdin;
    p->fileTable[1] = &stdout;
    for(int i = 2; i < PROC_MAX_FILE; i++){
        p->fileTable[i] = NULL;
    }
    
    return p;
}

err_t
proc_spawn(char* name, char** argv, struct proc **p)
{
    // kprintf("spawn");
    err_t err;
    struct proc *proc;
    struct thread *t;
    vaddr_t entry_point;
    vaddr_t stackptr;

    if ((proc = proc_init(name)) == NULL) {
        return ERR_NOMEM;
    }

    proc->parent = proc_current();

    // load binary of the process
    if ((err = proc_load(proc, name, &entry_point)) != ERR_OK) {
        goto error;
    }

    // set up stack and allocate its memregion 
    if ((err = stack_setup(proc, argv, &stackptr)) != ERR_OK) {
        goto error;
    }

    if ((t = thread_create(proc->name, proc, DEFAULT_PRI)) == NULL) {
        err = ERR_NOMEM;
        goto error;
    }

    // add to ptable
    spinlock_acquire(&ptable_lock);
    list_append(&ptable, &proc->proc_node);
    spinlock_release(&ptable_lock);

    // set up trapframe for a new process
    tf_proc(t->tf, t->proc, entry_point, stackptr);
    thread_start_context(t, NULL, NULL);

    // fill in allocated proc
    if (p) {
        *p = proc;
    }
    return ERR_OK;
error:
    as_destroy(&proc->as);
    proc_free(proc);
    return err;
}

struct proc*
proc_fork()
{
    kassert(proc_current());  // caller of fork must be a process

    struct proc *parent = proc_current();
    struct proc *child;
    struct thread *t;

    if ((child = proc_init(parent->name)) == NULL) {
        return NULL;
    }

    // copy parent's memory to child
    if(as_copy_as(&(parent->as), &child->as) != ERR_OK){
        return NULL;
    }

    // duplicate files from the parent process and reopen files that were open
    spinlock_acquire(&ptable_lock);
    for (int i = 0; i < PROC_MAX_FILE; i++) {
        if(parent->fileTable[i] != NULL){
            child->fileTable[i] = parent->fileTable[i];
            fs_reopen_file(child->fileTable[i]); 
        }
    }
    spinlock_release(&ptable_lock);

    // create new thread to run the process
    if ((t = thread_create(child->name, child, DEFAULT_PRI)) == NULL) {
        goto error;
    }

    // add to ptable
    spinlock_acquire(&ptable_lock);

    list_append(&ptable, &child->proc_node);
    
    // copy trapframe from parent
    *t->tf = *thread_current()->tf;

    // set return value in child to 0
    tf_set_return(t->tf, 0);
    thread_start_context(t, NULL, NULL);

    // set child's parent pointer
    child->parent = parent;

    spinlock_release(&ptable_lock);

    return child;
error:
    as_destroy(&child->as);
    proc_free(child);
    return NULL;
}

struct proc*
proc_current()
{
    return thread_current()->proc;
}

void
proc_attach_thread(struct proc *p, struct thread *t)
{
    kassert(t);
    if (p) {
        list_append(&p->threads, &t->thread_node);
    }
}

bool
proc_detach_thread(struct thread *t)
{
    bool last_thread = False;
    struct proc *p = t->proc;
    if (p) {
        list_remove(&t->thread_node);
        last_thread = list_empty(&p->threads);
    }
    return last_thread;
}

int
proc_wait(pid_t pid, int* status)
{
    struct proc *p = proc_current();
    struct proc *child;
    int child_pid;

    // waiting on any child, so find a child to wait on
    if(pid == -1){
        spinlock_acquire(&ptable_lock);
        child_pid = find_child_pid(p);
        spinlock_release(&ptable_lock);
        // if no running or exited child was found, return NULL
        if (child_pid == NULL){
            return NULL;
        }
        pid = child_pid;
    } 

    // get child process
    spinlock_acquire(&ptable_lock);
    child = get_proc_by_pid(pid); 
    spinlock_release(&ptable_lock);

    // if a child with this pid is not found, return ERR_CHILD. 
    if (child == NULL){
        return ERR_CHILD;
    }
    // makes sure that child's parent is the current
    if (child->parent != p){
        return ERR_CHILD;
    }
    
    spinlock_acquire(&ptable_lock);

    while(child->exit_status == STATUS_ALIVE){
        condvar_wait(&child->wait_cv, &ptable_lock);
    }

    if (status) {
        *status = child->exit_status;
    }

    list_remove(&child->proc_node);
    proc_free(child);

    spinlock_release(&ptable_lock);
    return pid;
}

void
proc_exit(int status)
{
    
    struct thread *t = thread_current();
    struct proc *p = proc_current();

    // detach current thread, switch to kernel page table
    // free current address space if proc has no more threads
    // order matters here
    proc_detach_thread(t);
    t->proc = NULL;
    vpmap_load(kas->vpmap);
    as_destroy(&p->as);

    // release process's cwd
    fs_release_inode(p->cwd);
 
    spinlock_acquire(&ptable_lock);
    // close all open files for this process
    for (int i = 0; i < PROC_MAX_FILE; i ++) {
        if(p->fileTable[i] != NULL){
            fs_close_file(p->fileTable[i]);
        }
    }
    spinlock_release(&ptable_lock);

    spinlock_acquire(&ptable_lock);
    for (Node *n = list_begin(&ptable); n != list_end(&ptable); n = list_next(n)) {
        
        struct proc *cur_process = list_entry(n, struct proc, proc_node);
        // running children: hand off to init_proc
        if (cur_process->parent == p && cur_process->exit_status == STATUS_ALIVE){
            cur_process->parent = init_proc;
        } 

        // already exited children: remove from ptable and free struct
        else if(cur_process->parent == p && cur_process->exit_status != STATUS_ALIVE){
            list_remove(&cur_process->proc_node);
            proc_free(cur_process);
        }
    }
    // set this process' exit status to passed-in status
    p->exit_status = status;
    // change condition variable for the process
    condvar_signal(&p->wait_cv);
    spinlock_release(&ptable_lock);

    // cleanup other stuff
    thread_exit(status);
}

/* helper function for loading process's binary into its address space */ 
static err_t
proc_load(struct proc *p, char *path, vaddr_t *entry_point)
{
    int i;
    err_t err;
    offset_t ofs = 0;
    struct elfhdr elf;
    struct proghdr ph;
    struct file *f;
    paddr_t paddr;
    vaddr_t vaddr;
    vaddr_t end = 0;

    if ((err = fs_open_file(path, FS_RDONLY, 0, &f)) != ERR_OK) {
        return err;
    }

    // check if the file is actually an executable file
    if (fs_read_file(f, (void*) &elf, sizeof(elf), &ofs) != sizeof(elf) || elf.magic != ELF_MAGIC) {
        return ERR_INVAL;
    }

    // read elf and load binary
    for (i = 0, ofs = elf.phoff; i < elf.phnum; i++) {
        if (fs_read_file(f, (void*) &ph, sizeof(ph), &ofs) != sizeof(ph)) {
            return ERR_INVAL;
        }
        if(ph.type != PT_LOAD)
            continue;

        if(ph.memsz < ph.filesz || ph.vaddr + ph.memsz < ph.vaddr) {
            return ERR_INVAL;
        }

        memperm_t perm = MEMPERM_UR;
        if (ph.flags & PF_W) {
            perm = MEMPERM_URW;
        }

        // found loadable section, add as a memregion
        struct memregion *r = as_map_memregion(&p->as, pg_round_down(ph.vaddr), 
            pg_round_up(ph.memsz + pg_ofs(ph.vaddr)), perm, NULL, ph.off, False);
        if (r == NULL) {
            return ERR_NOMEM;
        }
        end = r->end;

        // pre-page in code and data, may span over multiple pages
        int count = 0;
        size_t avail_bytes;
        size_t read_bytes = ph.filesz;
        size_t pages = pg_round_up(ph.memsz + pg_ofs(ph.vaddr)) / pg_size;
        // vaddr may start at a nonaligned address
        vaddr = pg_ofs(ph.vaddr);
        while (count < pages) {
            // allocate a physical page and zero it first
            if ((err = pmem_alloc(&paddr)) != ERR_OK) {
                return err;
            }
            vaddr += kmap_p2v(paddr);
            memset((void*)pg_round_down(vaddr), 0, pg_size);
            // calculate how many bytes to read from file
            avail_bytes = read_bytes < (pg_size - pg_ofs(vaddr)) ? read_bytes : (pg_size - pg_ofs(vaddr));
            if (avail_bytes && fs_read_file(f, (void*)vaddr, avail_bytes, &ph.off) != avail_bytes) {
                return ERR_INVAL;
            }
            // map physical page with code/data content to expected virtual address in the page table
            if ((err = vpmap_map(p->as.vpmap, ph.vaddr+count*pg_size, paddr, 1, perm)) != ERR_OK) {
                return err;
            }
            read_bytes -= avail_bytes;
            count++;
            vaddr = 0;
        }
    }
    *entry_point = elf.entry;

    // create memregion for heap after data segment
    if ((p->as.heap = as_map_memregion(&p->as, end, 0, MEMPERM_URW, NULL, 0, 0)) == NULL) {
        return ERR_NOMEM;
    }

    return ERR_OK;
}

err_t
stack_setup(struct proc *p, char **argv, vaddr_t* ret_stackptr)
{
    err_t err;
    paddr_t paddr;
    vaddr_t stackptr;
    vaddr_t stacktop = USTACK_UPPERBOUND-pg_size;

    // allocate a page of physical memory for stack
    if ((err = pmem_alloc(&paddr)) != ERR_OK) {
        return err;
    }
    memset((void*) kmap_p2v(paddr), 0, pg_size);
    // create memregion for stack
    if (as_map_memregion(&p->as, stacktop, pg_size*10, MEMPERM_URW, NULL, 0, False) == NULL) {
        err = ERR_NOMEM;
        goto error;
    }
    // map in first 10 stack pages
    if ((err = vpmap_map(p->as.vpmap, stacktop, paddr, 10, MEMPERM_URW)) != ERR_OK) {
        goto error;
    }
    // kernel virtual address of the user stack, points to top of the stack
    // as you allocate things on stack, move stackptr downward.
    stackptr = kmap_p2v(paddr) + pg_size*10;

    /* Your Code Here.  */
    // allocate space for fake return address, argc, argv
    // remove following line when you actually set up the stack
    stackptr -= 3 * sizeof(void*);

    // translates stackptr from kernel virtual address to user stack address
    *ret_stackptr = USTACK_ADDR(stackptr); 
    return err;
error:
    pmem_free(paddr);
    return err;
}


