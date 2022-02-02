#include <kernel/proc.h>
#include <kernel/thread.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>
#include <kernel/fs.h>
#include <lib/syscall-num.h>
#include <lib/errcode.h>
#include <lib/stddef.h>
#include <lib/string.h>
#include <arch/asm.h>
#include <kernel/pipe.h>

// syscall handlers
static sysret_t sys_fork(void* arg);
static sysret_t sys_spawn(void* arg);
static sysret_t sys_wait(void* arg);
static sysret_t sys_exit(void* arg);
static sysret_t sys_getpid(void* arg);
static sysret_t sys_sleep(void* arg);
static sysret_t sys_open(void* arg);
static sysret_t sys_close(void* arg);
static sysret_t sys_read(void* arg);
static sysret_t sys_write(void* arg);
static sysret_t sys_link(void* arg);
static sysret_t sys_unlink(void* arg);
static sysret_t sys_mkdir(void* arg);
static sysret_t sys_chdir(void* arg);
static sysret_t sys_readdir(void* arg);
static sysret_t sys_rmdir(void* arg);
static sysret_t sys_fstat(void* arg);
static sysret_t sys_sbrk(void* arg);
static sysret_t sys_meminfo(void* arg);
static sysret_t sys_dup(void* arg);
static sysret_t sys_pipe(void* arg);
static sysret_t sys_info(void* arg);
static sysret_t sys_halt(void* arg);
static int alloc_fd(struct file *f);
static bool validate_fd(int fd);

extern size_t user_pgfault;
struct sys_info {
    size_t num_pgfault;
};

/*
 * Machine dependent syscall implementation: fetches the nth syscall argument.
 */
extern bool fetch_arg(void *arg, int n, sysarg_t *ret);

/*
 * Validate string passed by user.
 */
static bool validate_str(char *s);
/*
 * Validate buffer passed by user.
 */
static bool validate_ptr(void* ptr, size_t size);


static sysret_t (*syscalls[])(void*) = {
    [SYS_fork] = sys_fork,
    [SYS_spawn] = sys_spawn,
    [SYS_wait] = sys_wait,
    [SYS_exit] = sys_exit,
    [SYS_getpid] = sys_getpid,
    [SYS_sleep] = sys_sleep,
    [SYS_open] = sys_open,
    [SYS_close] = sys_close,
    [SYS_read] = sys_read,
    [SYS_write] = sys_write,
    [SYS_link] = sys_link,
    [SYS_unlink] = sys_unlink,
    [SYS_mkdir] = sys_mkdir,
    [SYS_chdir] = sys_chdir,
    [SYS_readdir] = sys_readdir,
    [SYS_rmdir] = sys_rmdir,
    [SYS_fstat] = sys_fstat,
    [SYS_sbrk] = sys_sbrk,
    [SYS_meminfo] = sys_meminfo,
    [SYS_dup] = sys_dup,
    [SYS_pipe] = sys_pipe,
    [SYS_info] = sys_info,
    [SYS_halt] = sys_halt,
};

static bool
validate_str(char *s)
{
    struct memregion *mr;
    // find given string's memory region
    if((mr = as_find_memregion(&proc_current()->as, (vaddr_t) s, 1)) == NULL) {
        return False;
    }
    // check in case the string keeps growing past user specified amount
    for(; s < (char*) mr->end; s++){
        if(*s == 0) {
            return True;
        }
    }
    return False;
}

static bool
validate_ptr(void* ptr, size_t size)
{
    vaddr_t ptraddr = (vaddr_t) ptr;
    if (ptraddr + size < ptraddr) {
        return False;
    }
    // verify argument ptr points to a valid chunk of memory of size bytes
    return as_find_memregion(&proc_current()->as, ptraddr, size) != NULL;
}


// int fork(void);
static sysret_t
sys_fork(void *arg)
{
    struct proc *p;
    if ((p = proc_fork()) == NULL) {
        return ERR_NOMEM;
    }
    return p->pid;
}

// int spawn(const char *args);
static sysret_t
sys_spawn(void *arg)
{
    int argc = 0;
    sysarg_t args;
    size_t len;
    char *token, *buf, **argv;
    struct proc *p;
    err_t err;

    // argument fetching and validating
    kassert(fetch_arg(arg, 1, &args));
    if (!validate_str((char*)args)) {
        return ERR_FAULT;
    }

    len = strlen((char*)args) + 1;
    if ((buf = kmalloc(len)) == NULL) {
        return ERR_NOMEM;
    }
    // make a copy of the string to not modify user data
    memcpy(buf, (void*)args, len);
    // figure out max number of arguments possible
    len = len / 2 < PROC_MAX_ARG ? len/2 : PROC_MAX_ARG;
    if ((argv = kmalloc((len+1)*sizeof(char*))) == NULL) {
        kfree(buf);
        return ERR_NOMEM;
    }
    // parse arguments  
    while ((token = strtok_r(NULL, " ", &buf)) != NULL) {
        argv[argc] = token;
        argc++;
    }
    argv[argc] = NULL;

    if ((err = proc_spawn(argv[0], argv, &p)) != ERR_OK) {
        return err;
    }
    return p->pid;
}

// int wait(int pid, int *wstatus);
static sysret_t
sys_wait(void* arg)
{
    sysarg_t pid, wstatus;

    kassert(fetch_arg(arg, 1, &pid));
    kassert(fetch_arg(arg, 2, &wstatus));

    int child_pid;
    
    if(wstatus && !validate_ptr((void*)wstatus, sizeof(wstatus))){
        return ERR_FAULT;
    }

    struct proc* cur_process = proc_current();
    if (!cur_process){
        return ERR_FAULT;
    }

    if ((int)pid == -1){
        child_pid = proc_wait(ANY_CHILD, (int *)wstatus);
        
        if (child_pid != NULL){
            return child_pid;
        }
    }
    else{
        struct proc* child = get_proc_by_pid(pid);
        if (child != NULL && child->parent == cur_process){
            return proc_wait(pid, (int *)wstatus);
        }
    }
    return ERR_CHILD;
}

// void exit(int status);
static sysret_t
sys_exit(void* arg)
{
    sysarg_t status;

    kassert(fetch_arg(arg, 1, &status));

    proc_exit(status);

    panic("function sys_exit should not return. However, sys_exit is returning");
    return status;
}

// int getpid(void);
static sysret_t
sys_getpid(void* arg)
{
    return proc_current()->pid;
}

// void sleep(unsigned int, seconds);
static sysret_t
sys_sleep(void* arg)
{
    panic("syscall sleep not implemented");
}

// int open(const char *pathname, int flags, fmode_t mode);
static sysret_t
sys_open(void *arg)
{
    sysarg_t pathname, flags, mode;
    kassert(fetch_arg(arg, 1, &pathname));
    kassert(fetch_arg(arg, 2, &flags));
    kassert(fetch_arg(arg, 3, &mode));

    if (!validate_str((char*)pathname)) {
        return ERR_FAULT;
    }

    struct file *fileToOpen = NULL;
    err_t open_err = fs_open_file((char*)pathname, flags, mode, &fileToOpen);
    int fd = alloc_fd(fileToOpen);

    if(open_err == ERR_OK) {
        return fd;
    }

    return open_err;
}

// int close(int fd);
static sysret_t
sys_close(void *arg)
{
    sysarg_t fd;
    kassert(fetch_arg(arg, 1, &fd));

    if (!validate_fd(fd)){
        return ERR_INVAL;
    }

    struct proc *p = proc_current();

    if(p->fileTable[fd] == NULL){
        return ERR_INVAL;
    }

    fs_close_file(p->fileTable[fd]);

    p->fileTable[fd] = NULL;

    return ERR_OK;
}

// int read(int fd, void *buf, size_t count);
static sysret_t
sys_read(void* arg)
{
    sysarg_t fd, buf, count;

    kassert(fetch_arg(arg, 1, &fd));
    kassert(fetch_arg(arg, 3, &count));
    kassert(fetch_arg(arg, 2, &buf));

    if (!validate_ptr((void*)buf, (size_t)count)) {
        return ERR_FAULT;
    }

    if(!validate_fd(fd)){
        return ERR_INVAL;
    }

    struct proc *p = proc_current();

    if(p->fileTable[fd] == NULL){
        return ERR_INVAL;
    }
    
    return fs_read_file(p->fileTable[fd], (void*)buf, (size_t)count, &(p->fileTable[fd]->f_pos));
}

// int write(int fd, const void *buf, size_t count)
static sysret_t
sys_write(void* arg)
{
    sysarg_t fd, count, buf;

    kassert(fetch_arg(arg, 1, &fd));
    kassert(fetch_arg(arg, 3, &count));
    kassert(fetch_arg(arg, 2, &buf));

    if (!validate_ptr((void*)buf, (size_t)count)) {
        return ERR_FAULT;
    }

    struct proc *p = proc_current();
    if(!validate_fd(fd)){
        return ERR_INVAL;
    }

    if(p->fileTable[fd] == NULL){
        return ERR_INVAL;
    }

    if (fd == 1) {
        // write some stuff for now assuming one string
        return console_write((void*)buf, (size_t) count);
    } else {
        return fs_write_file(p->fileTable[fd], (void*)buf, (size_t) count, (offset_t*)(p->fileTable[fd]->f_pos));
    }
    return ERR_END;
}

// int link(const char *oldpath, const char *newpath)
static sysret_t
sys_link(void *arg)
{
    sysarg_t oldpath, newpath;

    kassert(fetch_arg(arg, 1, &oldpath));
    kassert(fetch_arg(arg, 2, &newpath));

    if (!validate_str((char*)oldpath) || !validate_str((char*)newpath)) {
        return ERR_FAULT;
    }

    return fs_link((char*)oldpath, (char*)newpath);
}

// int unlink(const char *pathname)
static sysret_t
sys_unlink(void *arg)
{
    sysarg_t pathname;

    kassert(fetch_arg(arg, 1, &pathname));

    if (!validate_str((char*)pathname)) {
        return ERR_FAULT;
    }

    return fs_unlink((char*)pathname);
}

// int mkdir(const char *pathname)
static sysret_t
sys_mkdir(void *arg)
{
    sysarg_t pathname;

    kassert(fetch_arg(arg, 1, &pathname));

    if (!validate_str((char*)pathname)) {
        return ERR_FAULT;
    }

    return fs_mkdir((char*)pathname);
}

// int chdir(const char *path)
static sysret_t
sys_chdir(void *arg)
{
    sysarg_t path;
    struct inode *inode;
    struct proc *p;
    err_t err;

    kassert(fetch_arg(arg, 1, &path));

    if (!validate_str((char*)path)) {
        return ERR_FAULT;
    }

    if ((err = fs_find_inode((char*)path, &inode)) != ERR_OK) {
        return err;
    }

    p = proc_current();
    kassert(p);
    kassert(p->cwd);
    fs_release_inode(p->cwd);
    p->cwd = inode;
    return ERR_OK;
}

// int readdir(int fd, struct dirent *dirent);
/*  
 * fd: file descriptor of a directory
 * dirent: struct direct pointer
 * 
 * Populate the struct dirent pointer with the next entry in a directory. 
 * The current position of the file descriptor is updated to the next entry.
 * Only fds corresponding to directories are valid for readdir. 
*/

static sysret_t
sys_readdir(void *arg)
{
    sysarg_t fd, dirent;

    kassert(fetch_arg(arg, 1, &fd));
    kassert(fetch_arg(arg, 2, &dirent));

    if(!validate_fd(fd)){
        return ERR_INVAL;
    }

    if(!validate_ptr((void*)dirent, sizeof(struct dirent))){
        return ERR_FAULT;
    }

    struct proc *p = proc_current();

    if(p->fileTable[fd] == NULL){
        return ERR_INVAL;
    }

    return fs_readdir(p->fileTable[fd], (struct dirent *)dirent);
}

// int rmdir(const char *pathname);
static sysret_t
sys_rmdir(void *arg)
{
    sysarg_t pathname;

    kassert(fetch_arg(arg, 1, &pathname));

    if (!validate_str((char*)pathname)) {
        return ERR_FAULT;
    }

    return fs_rmdir((char*)pathname);
}

// int fstat(int fd, struct stat *stat);
static sysret_t
sys_fstat(void *arg)
{
    sysarg_t fd, stat;

    kassert(fetch_arg(arg, 1, &fd));
    kassert(fetch_arg(arg, 2, &stat));
    
    if(!validate_fd(fd)){
        return ERR_INVAL;
    }

    if(fd == 0 || fd == 1){
        return ERR_INVAL;
    }

    if(!validate_ptr(((struct stat *)stat), sizeof(struct stat))){
        return ERR_FAULT;
    }

    struct proc *p = proc_current();
    if(p->fileTable[fd] == NULL){
        return ERR_INVAL;
    }
    
    ((struct stat *)stat)->ftype = p->fileTable[fd]->f_inode->i_ftype;
    ((struct stat *)stat)->inode_num = p->fileTable[fd]->f_inode->i_inum;
    ((struct stat *)stat)->size = p->fileTable[fd]->f_inode->i_size;

    return ERR_OK;
}

// void *sbrk(size_t increment);
static sysret_t
sys_sbrk(void *arg)
{
    panic("syscall sbrk not implemented");
}

// void memifo();
static sysret_t
sys_meminfo(void *arg)
{
    as_meminfo(&proc_current()->as);
    return ERR_OK;
}

// int dup(int fd);
static sysret_t
sys_dup(void *arg)
{
    sysarg_t fd;

    kassert(fetch_arg(arg, 1, &fd));

    if(!validate_fd(fd)) {
        return ERR_INVAL;
    }

    struct proc *p = proc_current();

    if(p->fileTable[fd] == NULL){
        return ERR_INVAL;
    }

    int len = sizeof(p->fileTable)/sizeof(p->fileTable[0]);

    //struct file dup_file = file_table[fd];

    for(int i = 0; i < len; i++){
        if(p->fileTable[i] == NULL){
            p->fileTable[i] = p->fileTable[fd];
            fs_reopen_file(p->fileTable[fd]);
            return i;
        }
    }
    return ERR_NOMEM;
}

// int pipe(int* fds);
static sysret_t
sys_pipe(void* arg)
{
    kprintf("inside sys_pipe");
    sysarg_t fds;

    kassert(fetch_arg(arg, 1, &fds));

    if(!validate_ptr((int *)fds, sizeof(int *))){
        kprintf("validating pointer");
        return ERR_FAULT;
    }

    if(!validate_fd(((int *)fds)[0]) || !validate_fd(((int *)fds)[1])){
        kprintf("validating fd");
        return ERR_FAULT;
    }

    // struct proc *cur = proc_current();
    // if(cur->fileTable[((int *)fds)[0]] || cur->fileTable[((int *)fds)[1]]){
    //     kprintf("checking filetable");
    //     return ERR_FAULT;
    // }

    pipe_init((int *)fds);

    return ERR_OK;
}

// void sys_info(struct sys_info *info);
static sysret_t
sys_info(void* arg)
{
    sysarg_t info;

    kassert(fetch_arg(arg, 1, &info));

    if (!validate_ptr((void*)info, sizeof(struct sys_info))) {
        return ERR_FAULT;
    }
    // fill in using user_pgfault 
    ((struct sys_info*)info)->num_pgfault = user_pgfault;
    return ERR_OK;
}

// void halt();
static sysret_t 
sys_halt(void* arg)
{
    shutdown();
    panic("shutdown failed");
}


sysret_t
syscall(int num, void *arg)
{
    kassert(proc_current());
    if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        return syscalls[num](arg);
    } else {
        panic("Unknown system call");
    }
}

// Will get a pointer to file, look through process’s open file table to find an available fd, 
// and store the pointer there. Returns the chosen fd
static int alloc_fd(struct file *f) {
    struct proc *process = proc_current();
    int len = sizeof(process->fileTable)/sizeof(process->fileTable[0]);

    for(int i = 0; i < len; i++){
        if(process->fileTable[i] == NULL){
            process->fileTable[i] = f;
            return i;
        }
    }

    // return an error value
    return ERR_NOMEM;
}

// Will get the file descriptor, making sure it’s a valid file descriptor 
// (in the open file table for the process)
static bool validate_fd(int fd) {
    return !(fd >= PROC_MAX_FILE || fd < 0);
}

