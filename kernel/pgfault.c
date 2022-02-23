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

    struct addrspace *as = &(proc_current()->as);

    // get memregion of fault_addr
    struct memregion *region = as_find_memregion(as, fault_addr, 1);
    if (region == NULL){
        proc_exit(-1);
    }

    // if there is a page protection issue, exit
    if (present){
        if (write){
            if(region->perm == MEMPERM_R){
                // optional 
                // if the reference count for the page is 1, change the permissions of that page to read/write and return

                // getting physical address of fault address
                paddr_t src_paddr;
                swapid_t swp;
                vpmap_lookup_vaddr(as->vpmap, pg_round_down(fault_addr), &src_paddr, &swp);

                // allocate physical page
                paddr_t new_page_addr;
                if (pmem_alloc(&new_page_addr) == ERR_NOMEM){
                    proc_exit(-1);
                }
                //copy original to newly created page
                memcpy((void*)KMAP_P2V(new_page_addr), (void*)KMAP_P2V(PTE_ADDR(src_paddr)), pg_size);

                // set perm of original to read/write
                vpmap_set_perm(as->vpmap, fault_addr, pg_round_up(region->end - region->start)/pg_size, MEMPERM_RW);
                
                // decrement the page count of the original
                pmem_dec_refcnt(src_paddr);
            }            
            return;
        }
        else{
            proc_exit(-1);
        }
    }
    else{
        // allocate physical page
        paddr_t new_page_addr;
        if (pmem_alloc(&new_page_addr) == ERR_NOMEM){
            proc_exit(-1);
        }

        // memset page to 0s
        memset((void*) kmap_p2v(new_page_addr), 0, pg_size);

        //add new page to pagetable
        err_t vpmap_status;
        if ((vpmap_status = vpmap_map(as->vpmap, fault_addr, new_page_addr, 1, region->perm) == ERR_VPMAP_MAP)){
            pmem_free(new_page_addr);
            proc_exit(-1);
        }
    }
    return;
}
