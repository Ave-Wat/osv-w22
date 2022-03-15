# Final Project Readme
## The features from your proposal that you successfully implemented
We attempted to create a swap space feature in order to give the illusion of a larger memory space.

## Any non-functional features you attempted to implement
Creation of a swap space
    Writing pages into the swap space if memory is full
    Reading pages from swap space into memory when an access to that page is made.

## The files you added or modified, and how they relate to the features above
Files modified: 
* `fs.h`:
    * This is where we declare a pointer to our swap space(swpfile) and last_swp_index, which stores the next available spot within our swap space that can be written to.
* `main.c`:
    * This is where we initialized our swap space by using fs_open_file(). This is also where we initialized last_swp_index.
* `mmu.h`: 
    * We added a macro for our bitwise operations. PTE_DISK is used to toggle the 52nd bit in order to indicate whether a page is on disk or not. 
* `pgfault.c`: 
    * This is where the bulk of our work occurred. 
    * In the page fault handler, we initialize our list of allocated pages and our lock for swap operations(if it hasn't already occurred). We modified the page fault handler to append to our linked list of allocated pages everytime a page is allocated so that we can evict if necessary. 
    * To accomplish this, we created a new function called pmem_alloc_or_evict(), which calls pmem_alloc() and adds to the list if memory isn't full. If memory is full, then we find a page to evict using FIFO, modify its page table entry to indicate that it has been swapped out to disk, store the index of where the page is within the swap space(in the portion of the page table entry where the physical address used to be), write that page to the swap space, give that page to the process that wanted it, and then removing the evicted page and appending it to the end(to maintain least-recently-used property).
    * When a page fault occurs, one of the causes may be that page was swapped out to disk. Therefore, in our page fault handler, we check the page table entry of the fault address, and if the "swapped to disk" bit is set, then we know that we need to retrieve that page from the swap space. We use the index that we previously stored in the page table entry to read from the swap space and write it into a page in memory.

## What aspects of the implementation each test case tests
swp-disk-empty: 
* This test checks that our implementation does not activate with "typical" operations: i.e situations where memory capacity is not an issue. This is done by making sure that our swap space is empty at the end of the test.
* Behavior: Passes.

swp-disk-notempty:
* This test checks that our implementation successfully writes to the swap space within the disk when memory becomes full. This is done by making sure that our swap space is not empty at the end of the test.
* Behavior: Faults due to the page table error at line 156 in `pgfault.c`.

swp-small:
* This test checks whether our implementation can swap a small number of pages to the swap space and then correctly retrieve those pages.
* Behavior: Prints out number of allocated pages and then faults at line 156 in `pgfault.c`.

swp-large:
* This test checks whether our implementation can swap a large number of pages to the swap space and then correctly retrieve those pages.
* Behavior: Faults due to the page table error at line 156 in `pgfault.c`.

swp-concurrency:
* This test checks whether our implementation can successfully handle multiple processes swapping a small number of pages to the swap space and then retrieving those pages.
* Behavior: Faults due to the page table error at line 156 in `pgfault.c`. 

swp-stack
* This test checks whether our implementation successfully writes and retrieves pages to the stack.
* Behavior: this test exits with status -1 due to [insert here]

## Any features or edge cases the test cases do not address
Full File:
* The tests and code don't address what happens when the swp_file reaches maximum file size.

Page permissions preservation:
* No test for ensuring the file permissions are peserved during swaps.
* Example: parent alloc's pages. The child does something to cause pages to be swapped out. The child then goes to write, can it still write?


## Any known bugs
The code breaks with a "PANIC: Kernel error in page fault handler" on line 156 in `pgfault.c`. This can be demonstrated by running swp-small. 
* Once the memory fills up, the program tries to evict a page to the swap space and allocate a new page in memory. However, our page table entries are incorrect, causing the retrieval of the new page's address to fault. 
* The page table entries are incorrect because we haven't figured out how to manipulate the page table to include another entry when the page is being handed off to the same process. Both of the pages, the evicted page and the new page, are possibly still pointing to the same page table address.

Implementation uses FIFO for page eviction.
* Because our implementation uses first in first out to decide which page to swap, thrashing can occur. FIFO is not as efficient of an algorithm as LFU or LRU at deciding which page to evict.

## Differences in implementation from Project Proposal
Unfortunately, we were not able to meet our minimum viable goals of eviction and accessing evicted pages.

## Anything interesting you would like to share
We found it highly useful to create a design document to outline our thought process and ease implementation. We have included the design doc in the top level of our project.
