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
List allocated_page_list; // List that holds pages that are allocated in physical memory
bool initialized = False;

// swap operations lock
struct spinlock swp_lock;

void pmem_alloc_or_evict(paddr_t *new_page_addr){
    spinlock_acquire(&swp_lock);
    
    // no available page, so must perform swap procedure
    if (pmem_alloc(new_page_addr) == ERR_NOMEM){
        kprintf("\n");
        kprintf("PRINTING OUT allocated_page_list: \n");
        for (Node *n = list_begin(&allocated_page_list); n != list_end(&allocated_page_list); n = list_next(n)) {
            struct page *p = list_entry(n, struct page, node);
            kprintf("page_to_paddr(): %p \n", page_to_paddr(p));
        }

        // find a page to "evict": a currently allocated physical page within allocated page list
        Node* head = list_begin(&allocated_page_list);
        struct page* evicted_page = list_entry(head, struct page, node);
        paddr_t evicted_paddr = page_to_paddr(evicted_page);
        vaddr_t evicted_vaddr = kmap_p2v(evicted_paddr);

        // // modify page table entry to indicate that page is swapped to disk
        // struct proc* cur_process = proc_current();
        // pte_t* page_table_entry = find_pte(cur_process->as.vpmap->pml4, evicted_vaddr, 0);
        // *page_table_entry = ~(1) & *page_table_entry; // set present bit to 0
        // *page_table_entry = PTE_DISK | *page_table_entry; // set "swapped to disk" bit to 0

        // // puts index into the 12-47 bits of page table entry
        // *page_table_entry = (*page_table_entry & ~(PHYS_ADDR_MASK)) | (last_swp_idx << 12);

        // write evicted page to the swap space
        ssize_t result;
        offset_t ofs = last_swp_idx * pg_size;

        // kprintf("swpfile: %p \n", swpfile);
        // kprintf("evicted_vaddr: %p \n", evicted_vaddr);
        // kprintf("evicted_paddr: %p \n", evicted_paddr);
        // kprintf("ofs: %d \n", ofs);

        if ((result = fs_write_file(swpfile, (void*) evicted_vaddr, (size_t) pg_size, (offset_t*) &ofs)) == -1){
            kprintf("NO DATA WRITTEN TO DISK");
        }
        last_swp_idx++;
        
        // // give evicted page to current process
        pmem_free(evicted_paddr);
        pmem_alloc(new_page_addr);

        // remove head of list and append to the end to maintain LRU status
        list_remove(head);
        list_append(&allocated_page_list, &(paddr_to_page(*new_page_addr)->node));
    }
    else{
        // there is an available page, so simply add newly allocated page to allocated page list
        list_append(&allocated_page_list, &(paddr_to_page(*new_page_addr)->node));
    }
    spinlock_release(&swp_lock);
    return;
}

void
handle_page_fault(vaddr_t fault_addr, int present, int write, int user) {
    
    if (!initialized){
        list_init(&allocated_page_list);
        spinlock_init(&swp_lock);
        initialized = True;
    }
    
    if (user) {
        __sync_add_and_fetch(&user_pgfault, 1);
    }
    else{
        kprintf("fault_addr: %p, present %d, write %d, user %d \n", fault_addr, present, write, user);
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

    struct proc* cur_process = proc_current();
    pte_t* page_table_entry = find_pte(cur_process->as.vpmap->pml4, fault_addr, 0);
    // check if the "swapped to disk" and "present" bit is set. 
    // If the "swapped to disk" bit is set and the "present" bit is not set, then we know that it's been swapped to disk.
    if ((*page_table_entry & PTE_DISK) == PTE_DISK && !((*page_table_entry & 1) == 1)){
        kprintf("inside \n");
        spinlock_acquire(&swp_lock);
        paddr_t new_page_addr;

        // get a physical page to write data back into
        pmem_alloc_or_evict(&new_page_addr);

        // get index by masking to get phys address and right shifting 
        offset_t index = (*page_table_entry & PHYS_ADDR_MASK) >> 12;
        ssize_t result;
        offset_t ofs = index * pg_size;
        if ((result = fs_read_file(swpfile, (void *) new_page_addr, pg_size, &ofs)) == -1){
            panic("NO DATA READ");
        }
        spinlock_release(&swp_lock);
        return;
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
                spinlock_acquire(&swp_lock);
                pmem_alloc_or_evict(&new_page_addr);
                spinlock_release(&swp_lock);
            
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
        pmem_alloc_or_evict(&new_page_addr);

        kprintf("\n");
        kprintf("page fault handler: new_page_addr: %p \n", new_page_addr);
        kprintf("page fault handler: kmap_p2v(new_page_addr): %p \n", kmap_p2v(new_page_addr)); 

        // memset page to 0s
        kprintf("before memset\n");
        memset((void*) kmap_p2v(new_page_addr), 0, pg_size);
        kprintf("after memset\n");
        //add new page to pagetable
        err_t vpmap_status;
        if ((vpmap_status = vpmap_map(as->vpmap, fault_addr, new_page_addr, 1, region->perm) == ERR_VPMAP_MAP)){
            pmem_free(new_page_addr);
            proc_exit(-1);
        }
    }
    return;
}
