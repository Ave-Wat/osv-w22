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

    // allocate PAGES pages on the heap, page them in; some pages will be on disk
    for (i = 0; i < PAGES; i++) {
        a[i * 4096] = i;
        printf("%d \n", i);
    }

    // read pages; since starting from zero, the early pages won't be in memory (have to pull from disk)
    // This tests whether we are correctly allocating and reading pages
    for (i = 0; i < PAGES; i++) {
        if (a[i * 4096] != i){
            printf("i: %d \n", i);
            printf("a[i * 4096]: %d \n", a[i * 4096]);
            error("reading pages caused error: unexpected page value");
        }
    }
    pass("swp-small");
    exit(0);
}