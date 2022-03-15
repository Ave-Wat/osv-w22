# The features from your proposal that you successfully implemented
We attempted to complete a swap space feature in order to give the illusion of a larger memory space.

# Any non-functional features you attempted to implement
Creation of a swap space
    Writing pages into the swap space if memory is full
    Reading pages from swap space into memory when an access to that page is made.

# The files you added or modified, and how they relate to the features above
Files modified: 
    - fs.h
        This is where we declare a pointer to our swap space(swpfile) and last_swp_index, which stores the next available spot within our swap space that can be written to.

    - main.c:
        This is where we initialized our swap space by using fs_open_file(). This is also where we initialized last_swp_index.

    - mmu.h:
        We added a macro for our bitwise operations. PTE_DISK is used to toggle the 52nd bit in order to indicate whether a page is on disk or not. 

    - pgfault.c:
        This is where the bulk of our work occurred. 
        
        In the page fault handler, we initialize our list of allocated pages and our lock for swap operations(if it hasn't already occurred). We modified the page fault handler to append to our linked list of allocated pages everytime a page is allocated so that we can evict if necessary. 
        
        To accomplish this, we created a new function called pmem_alloc_or_evict(), which calls pmem_alloc() and adds to the list if memory isn't full. If memory is full, then we find a page to evict, modify its page table entry to indicate that it has been swapped out to disk, store the index of where the page is within the swap space(in the portion of the page table entry where the physical address used to be), write that page to the swap space, give that page to the process that wanted it, and then removing the evicted page and appending it to the end(to maintain least-recently-used property).
        
        When a page fault occurs, one of the causes may be that page was swapped out to disk. Therefore, in our page fault handler, we check the page table entry of the fault address, and if the "swapped to disk" bit is set, then we know that we need to retrieve that page from the swap space. We use the index that we previously stored in the page table entry to read from the swap space and write it into a page in memory.
    
    

# What aspects of the implementation each test case tests
swp-disk-empty: 
    This test checks that our implementation does not activate with "typical" operations: i.e situations where memory capacity is not an issue. This is done by making sure that our swap space is empty at the end of the test.

swp-disk-notempty:
    This test checks that our implementation successfully writes to the swap space within the disk when memory becomes full. This is done by making sure that our swap space is not empty at the end of the test.

swp-small:
    This test checks whether our implementation can swap a small number of pages to the swap space and then correctly retrieve those pages.

swp-large:
    This test checks whether our implementation can swap a large number of pages to the swap space and then correctly retrieve those pages.

swp-concurrency:
    This test checks whether our implementation can successfully handle multiple processes swapping a small number of pages to the swap space and then retrieving those pages.

swp-stack
    This test checks whether our implementation successfully writes and retrieves pages to the stack.
    Note: this test ... [insert behavior]

# Any features or edge cases the test cases do not address
Full File:
    The tests and code don't address what happens when the swp_file reaches maximum file size.

Page permissions preservation:
    No test for ensuring the file permissions are peserved during swaps.
    Example: parent alloc's pages. The child does something to cause pages to be swapped out. The child then goes to write, can it still write?


# Any known bugs
The code panics prior to completion.
- put in tons of detail, line numbers, etc



# Anything interesting you would like to share