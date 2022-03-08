#include <lib/test.h>
#include <lib/stddef.h>

/**
 * This test assumes qemu is run with 4 MB of physical memory 
 * (use make qemu-low-mem)
 */

int
main()
{
    int page_count = 0;

    // could add pages to the memory
    // could increase the size of the current process (using sys_sbrk) to fill the memory and then add pages
}