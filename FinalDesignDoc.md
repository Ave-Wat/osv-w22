# Final Project Design Doc: Swap Space
## Resources
* https://cs.carleton.edu/faculty/awb/cs332/w22/notes/beyond-memory.html
* https://cs.carleton.edu/faculty/awb/cs332/w22/labs/memory.html
* https://www.baeldung.com/cs/virtual-memory-vs-swap-space 
* https://pages.cs.wisc.edu/~remzi/OSTEP/vm-beyondphys.pdf 
* https://www.geeksforgeeks.org/swap-space-in-operating-system/ 
* ​​https://www.linux.com/news/all-about-linux-swap-space/ 
## Overview
Implement swap space in order to create the illusion of a greater amount of memory.
## Major Parts
* Create a swap space on the disk
* Add a case to page fault handler to handle when the memory is full; move a physical page into the swap space on the disk
* When calls made to things in the swap space, need to be able to read the page back into memory
## In-depth Analysis and Implementation
### Creating swap space
* Use fs_open_file() to create a swap file on the disk in main.c after fs_init()
   * Treat the swpfile as an array of pages
* Store a global pointer to the file *swpfile in fs.h; include fs.h in main.c, pgfault.c
* Store a global variable with idx of last allocated page in swpfile
### When mem full: Edit page fault handler
* Determine which page to remove
   * Need some way of keeping track of allocated pages
      * Use the built-in List
         * There’s a node struct within the page struct associated with every page
         * Can use the page_to_paddr() function to retrieve physical address


* Write that page to the swpfile using fs_write_file(); put page in buf; size is sizeof page
* Change the pagetable entry for moved page to be idx of location in swpfile
   * As the pagetable entry is a 64 bits, use bitwise operations to repurpose physical page addr to be position in file
   * Use find_pte() using fault_addr to get pagetable entry
      * See vpmap_cow_copy for example of manipulation of pagetable entry
### When page not in mem is called: Edit page fault handler
* Use find_pte() to get pagetable entry
* If the pagetable entry in the swpfile (see locat of physical addr; if a swpfile idx, in swpfile), bring into mem. If mem full, run mem full steps
   * Use fs_read_file() to go to idx and read file (can use ofs to start at idx??)
   * Change pagetable entry to have physical addr (there’s an existing func for this - map_pages()?)
### More changes to page fault handler
* Must add pages to list holding currently allocated pages in order for swap space to work (FIFO)
### Writing Tests
* See lowmem tests in lab 4&5 (see makefile lowmem); will reduce amount of memory
      * Call fs_write to actually alloc a file (alloc 1000 of them; will overload 
         * Confused by this - doesn’t this write to disk, not memory?
      * Call sbrk 1000 times?
   * Create lowmem test to test what happens when mem fills up
   * Is the program writing to the swpfile when mem fills up?
   * Is the program swapping files when something on the disk is called?
* Swp-large
* Swp-concurrency
* Check if disk is empty when memory is not full
* Swp-write (write but not read; make sure it doesn’t throw an error)
   * If the loops fill up mem and don’t crash then it’s probably working
## Risk Analysis
### Unanswered Questions
* How to use ofs to write to a specific index in a file?
* What would the fault_addr of page not in mem be? Are we thinking about that case correctly?
### Staging of Work
1. Create swap space
2. Implement memory full case in page fault handler
3. Edit page fault handler
4. Write tests