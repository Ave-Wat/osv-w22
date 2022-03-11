#include <lib/test.h>
#include <lib/stddef.h>

/**
 * This test assumes qemu is run with 4 MB of physical memory 
 * (use make qemu-low-mem)
 */

int
main()
{
    printf("test");
    int i;
    size_t PAGES = 20;
    // reserves space to allocate pages
    volatile char *a = sbrk(PAGES * 4096);

    // allocate PAGES pages on the heap, page them in; some pages will be on disk
    for (i = 0; i < PAGES; i++) {
        a[i * 4096] = i;
    }

    pass("swp-write");
    exit(0);
}