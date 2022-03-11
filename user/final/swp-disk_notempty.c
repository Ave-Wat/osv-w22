#include <lib/test.h>
#include <lib/stddef.h>

/**
 * This test assumes qemu is run with 4 MB of physical memory 
 * (use make qemu-low-mem)
 */

int
main()
{
    int i;
    size_t PAGES = 400;
    // reserves space to allocate pages
    volatile char *a = sbrk(PAGES * 4096);

    // allocate PAGES pages on the heap, page them in; no pages should be on disk
    for (i = 0; i < PAGES; i++) {
        a[i * 4096] = i;
    }

    int fd = open("/swp", FS_RDONLY, 0);

    struct stat stat;
    if (fstat(fd, &stat)){
        error("error in fstat, did not return ERR_OK");
    }
    else{
        if (stat.size == 0){
            error("swap space empty");
        }
    }
   
    pass("swp-disk-notempty");
    exit(0);
}