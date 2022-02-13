#include <kernel/proc.h>
#include <kernel/console.h>
#include <kernel/trap.h>
#include <arch/mmu.h>
#include <lib/string.h>
#include <kernel/vpmap.h>
#include <lib/errcode.h>

size_t user_pgfault = 0;

void
handle_page_fault(vaddr_t fault_addr, int present, int write, int user) {
    
    if (user) {
        __sync_add_and_fetch(&user_pgfault, 1);
    }
    else{
        panic("Kernel error in page fault handler \n");
    }
    
    // turn on interrupt now that we have the fault address 
    intr_set_level(INTR_ON);

    /* Your Code Here. */

    // if there is a page protection issue, then exit process
    if (present){
        proc_exit(-1);
    }

    // get memregion of fault_addr
    struct memregion *region = as_find_memregion(&(proc_current()->as), fault_addr, pg_size);
    if (region == NULL){
        proc_exit(-1);
    }

    // a write on a read only memory permission is not valid
    if ((region->perm == MEMPERM_R || region->perm == MEMPERM_UR) && write){
        proc_exit(-1);
    }

    // allocate physical page
    paddr_t new_page_addr;
    if (pmem_alloc(&new_page_addr) == ERR_NOMEM){
        proc_exit(-1);
    }

    // memset page to 0s
    memset((void*) kmap_p2v(new_page_addr), 0, pg_size);

    //add new page to pagetable
    err_t vpmap_status;

    if ((vpmap_status = vpmap_map(proc_current()->as.vpmap, fault_addr, new_page_addr, 1, region->perm) == ERR_VPMAP_MAP)){
        proc_exit(-1);
    }
    
    /* End Your Code */
    // if (user) {
    //     // kprintf("fault addres %p, present %d, wrie %d, user %d\n", fault_addr, present, write, user);
    //     proc_exit(-1);
    //     panic("unreachable");
    // } else {
    //     // kprintf("fault addr %p\n", fault_addr);
    //     panic("Kernel error in page fault handler\n");
    // }
    return;
}
