#include <kernel/proc.h>
#include <kernel/console.h>
#include <kernel/trap.h>
#include <arch/mmu.h>
#include <lib/string.h>
#include <kernel/vpmap.h>

size_t user_pgfault = 0;

void
handle_page_fault(vaddr_t fault_addr, int present, int write, int user) {
    if (user) {
        __sync_add_and_fetch(&user_pgfault, 1);
    }
    // turn on interrupt now that we have the fault address 
    intr_set_level(INTR_ON);

    /* Your Code Here. */

    // get memregion of fault_addr
    struct memregion *region = as_find_memregion(&(proc_current()->as), fault_addr, pg_size);

    // a write on a read only memory permission is not valid
    if (region->perm == MEMPERM_R && write){
        return;
    }

    // if fault_addr is within the stack memregion
    if (fault_addr <= USTACK_UPPERBOUND && fault_addr >= (USTACK_UPPERBOUND - pg_size * 10)){
        paddr_t new_page_addr;

        // allocate physical page
        pmem_alloc(&new_page_addr);

        // memset page to 0s
        memset((void*) kmap_p2v(new_page_addr), 0, pg_size);

        //add new page to pagetable
        vpmap_map(proc_current()->as.vpmap, fault_addr, new_page_addr, 1, MEMPERM_URW);
        
        return;
    }

    // if fault_addr is within the heap memregion
    struct memregion *heap_region = (proc_current()->as).heap;

    // if fault_addr is within the heap memregion
    if (fault_addr <= heap_region->end && fault_addr >= heap_region->start){
        paddr_t new_page_addr;

        // allocate physical page
        pmem_alloc(&new_page_addr);

        // memset page to 0s
        memset((void*) kmap_p2v(new_page_addr), 0, pg_size);

        //add new page to pagetable
        vpmap_map(proc_current()->as.vpmap, fault_addr, new_page_addr, 1, MEMPERM_URW);

        return;
    }
    /* End Your Code */

    if (user) {
        // kprintf("fault addres %p, present %d, wrie %d, user %d\n", fault_addr, present, write, user);
        proc_exit(-1);
        panic("unreachable");
    } else {
        // kprintf("fault addr %p\n", fault_addr);
        panic("Kernel error in page fault handler\n");
    }
}
