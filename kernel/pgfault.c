#include <kernel/proc.h>
#include <kernel/console.h>
#include <kernel/trap.h>
#include <lib/usyscall.h>


size_t user_pgfault = 0;


void
handle_page_fault(vaddr_t fault_addr, int present, int write, int user) {
    if (user) {
        __sync_add_and_fetch(&user_pgfault, 1);
    }
    // turn on interrupt now that we have the fault address 
    intr_set_level(INTR_ON);

    /* Your Code Here. */
    if (USTACK_UPPERBOUND >= fault_addr && (USTACK_UPPERBOUND + pg_size*10) <= fault_addr){
        paddr_t new_page_addr;
        pmem_alloc(new_page_addr);
        memset((void*) kmap_p2v(new_page_addr), 0, pg_size);

        //add new page to pagetable
        //not sure if USTACK_UPPERBOUND-pg_size is the correct start of the corresponding vaddr
        vpmap_map(proc_current()->as.vpmap, USTACK_UPPERBOUND-pg_size, new_page_addr, 1, MEMPERM_URW);

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
