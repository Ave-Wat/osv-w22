// #include <lib/test.h>
// #include <lib/stddef.h>

// int
// main()
// {
//     int n, status, ret;
//     int nproc = 6;
//     int pid, mypid;

//     // fork nproc number of process and exit them with their pids as exit status
//     for (n = 0; n < nproc; n++) {
//         pid = fork();

//         if (pid == 0) {
//             int i;
//             size_t PAGES = 400;
//             // reserves space to allocate pages
//             volatile char *a = sbrk(PAGES * 4096);

//             // allocate PAGES pages on the heap, page them in; some pages will be on disk
//             for (i = 0; i < PAGES; i++) {
//                 a[i * 4096] = i;
//                 printf("%d \n", i);
//             }

//             // read pages; since starting from zero, the early pages won't be in memory (have to pull from disk)
//             // This tests whether we are correctly allocating and reading pages
//             for (i = 0; i < PAGES; i++) {
//                 if (a[i * 4096] != i){
//                     error("reading pages caused error: unexpected page value");
//                 }
//             }

//         }
//     }

//     pass("swp-concurrency");
//     exit(0);
//     return 0;
// }
