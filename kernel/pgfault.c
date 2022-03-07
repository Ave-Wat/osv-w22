#include <kernel/proc.h>
#include <kernel/console.h>
#include <kernel/trap.h>
#include <arch/mmu.h>
#include <lib/string.h>
#include <kernel/vpmap.h>
#include <lib/errcode.h>
#include <kernel/fs.h>
#include <kernel/pmem.h>

size_t user_pgfault = 0;
List allocated_page_list; // List that holds physical addresses of pages that are allocated in physical memory

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


    pte_t* page_table_entry = find_pte(fault_addr);
    // check if the "present in memory" bit is set. If not, 
    if ((*page_table_entry & 1) == 0){
        // pmem_alloc_or_evict()
        // fs_read_file()
    }

    // if there is a page protection issue
    if (present){
        if (write){
            // if permission of current memregion is read/write, we know it's a copy-on-write page
            if(region->perm == MEMPERM_URW){
                paddr_t src_paddr;
                swapid_t swp;
                
                // allocate physical page
                paddr_t new_page_addr;
                if (pmem_alloc(&new_page_addr) == ERR_NOMEM){
                    proc_exit(-1);
                }
                
                //copy original to newly created page
                memcpy((void*)KMAP_P2V(new_page_addr), (void*)pg_round_down(fault_addr), pg_size);

                // set perm of new page to read/write
                vpmap_set_perm(as->vpmap, kmap_p2v(new_page_addr), pg_size, MEMPERM_URW);

                // get physical address of fault address and then decrement page count
                vpmap_lookup_vaddr(as->vpmap, pg_round_down(fault_addr), &src_paddr, &swp);
                pmem_dec_refcnt(src_paddr);

                // map to new physical page
                vpmap_map(as->vpmap, fault_addr, new_page_addr, 1, MEMPERM_URW);                
            }            
            return;
        }
        else{
            proc_exit(-1);
        }
    }
    // no page protection issue
    else{
        // allocate physical page
        paddr_t new_page_addr;
        if (pmem_alloc(&new_page_addr) == ERR_NOMEM){
            // proc_exit(-1);
        
            /////////////////////////////////////////////////////////////////////////
            // no available page, so must perform swap procedure:

            // find a page to "evict": a currently allocated physical page within allocated page list
            Node* head = list_begin(&allocated_page_list);
            struct page* evicted_page = list_entry(head, struct page, node);
            vaddr_t* evicted_vaddr = kmap_p2v(page_to_paddr(evicted_page));

            // modify page table entry to indicate that page is no longer present in memory
            pte_t* page_table_entry = find_pte(evicted_vaddr);
            *page_table_entry = ~(1) & *page_table_entry;

            // puts index into the 12-47 bits of page table entry
            *page_table_entry = *page_table_entry & ~(PHYS_ADDR_MASK) | (last_swp_idx << 12);

            // write evicted page to the swap space
            fs_write_file(swpfile, pg_round_down(evicted_vaddr), 1, last_swp_idx * pg_size);
            last_swp_idx++;

            // give evicted page to current process
            new_page_addr = page_to_paddr(evicted_page); 

            // remove head of list and append to the end to maintain LRU status
            list_remove(head);
            list_append(&allocated_page_list, &paddr_to_page(new_page_addr)->node);
        }
        else{
            // there is an available page, so simply add newly allocated page to allocated page list
            list_append(&allocated_page_list, &paddr_to_page(new_page_addr)->node);
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
