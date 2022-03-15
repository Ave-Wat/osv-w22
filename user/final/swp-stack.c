#include <lib/test.h>
#include <lib/stddef.h>

/**
 * This test assumes qemu is run with 4 MB of physical memory 
 * (use make qemu-low-mem)
 */

int
main()
{
    
    int x = 50000;
    int a[x];
    int i, j;

    for (i = 0; i < x; i++) {
        a[i] = i;
    }

    for (j = 0; j < x; j++) {
        if (a[j] != j){
            error("reading pages caused error: unexpected page value");
        }
    }
    
    pass("swp-stack");
    exit(0);
}