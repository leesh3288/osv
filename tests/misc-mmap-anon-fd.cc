#include <sys/mman.h>
#include <sys/stat.h>
#include <osv/anonfd.hh>
#include <osv/prex.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cerrno>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <fcntl.h>

#define HUNDRED_MB (100ull * 1024 * 1024)

/*
1. Thread 1 creates a virtual memory via map() with MAP_PRIVATE or with MAP_SHARED (without MAP_PRIVATE).
2. Thread 2 creates a virtual memory via map() with MAP_PRIVATE or with MAP_SHARED.
3. Thread 2 writes a certain value with memset() to the virtual memory  -> With MAP_PRIVATE, CoW happens for each page write. 
4. Thread 3 creates a virtual memory via map() with MAP_PRIVATE or with MAP_SHARED.
5. Thread 3 reads the virtual memory -> No CoW happen
6. Check what values thread 3 reads.
    In the case of MAP_PRIVATE, thread 3 must not read the value written by thread 2.
    In the case of MAP_SHARED, thread 3 must read the value written by thread 2.
Things to evaluate:
- Create a file size ranging from 100 MB, 200M, 300MB .. up to 1 GB (If your virtual machine does not have enough memory, 500 MB is fine)
i) mmap() with MAP_SHARED, run the above step 1 ~ 4,  measure the time spent by thread 2, and report what values thread 3 reads
ii) mmap() with MAP_PRIVATE, run the above step 1 ~ 4, measure the time spent by thread 2, and report what values thread 3 reads
*/

void mmap_anon_fd_test(int mult, bool do_share)
{
    std::mutex mtx;
    std::condition_variable cv;
    int state = 1;

    size_t file_size = HUNDRED_MB * mult;

    int fd = create_anon_fd();
    ftruncate(fd, file_size);

    std::chrono::milliseconds t2_time;

    std::thread T1([file_size, fd, &cv, &mtx, &state]{
        // Step 1: mmap & init memory
        void *ptr = mmap((void*)0x1000000000, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        for (size_t i = 0; i < file_size; i += PAGE_SIZE)
            memset(ptr + i, 0xaa, PAGE_SIZE);  // init memory data
        
        std::unique_lock<std::mutex> lk(mtx);
        state = 2;
        lk.unlock();
        cv.notify_all();

        lk.lock();
        cv.wait(lk, [&state]{ return state == 4; });
        lk.unlock();
        munmap(ptr, file_size);
    });

    std::thread T2([file_size, fd, do_share, &cv, &mtx, &state, &t2_time]{
        // Step 2: T2 mmap
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&state]{ return state == 2; });
        lk.unlock();
        void *ptr = mmap((void*)0x2000000000, file_size, PROT_READ | PROT_WRITE, do_share ? MAP_SHARED : MAP_PRIVATE, fd, 0);

        // Step 3: T2 write
        auto st = std::chrono::system_clock::now();
        memset(ptr, 0xbb, file_size);
        auto en = std::chrono::system_clock::now();
        t2_time = std::chrono::duration_cast<std::chrono::milliseconds>(en - st);
        
        lk.lock();
        state = 3;
        lk.unlock();
        cv.notify_all();

        lk.lock();
        cv.wait(lk, [&state]{ return state == 4; });
        lk.unlock();
        munmap(ptr, file_size);
    });

    std::thread T3([file_size, fd, do_share, &cv, &mtx, &state]{
        // Step 4. T2 mmap
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&state]{ return state == 3; });
        lk.unlock();
        unsigned char *ptr = (unsigned char *)mmap((void*)0x3000000000, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

        // Step 5: T2 read
        for (size_t i = 0; i < file_size; i += PAGE_SIZE)
            if ((do_share && ptr[i] != 0xbb) || (!do_share && ptr[i] != 0xaa))
                fprintf(stderr, "Wrong data %02x at offset %x\n", ptr[i], i);
        
        lk.lock();
        state = 4;
        lk.unlock();
        cv.notify_all();

        munmap(ptr, file_size);
    });

    T1.join();
    T2.join();
    T3.join();

    destroy_anon_fd(fd);

    printf("T2 Time Spent (Filesize %2d * 100MB, do_share %c): %8lld ms\n", mult, do_share ? 'T' : 'F', t2_time.count());
}

int main()
{
    for (int i = 1; i <= 10; i++)
        mmap_anon_fd_test(i, true);

    for (int i = 1; i <= 10; i++)
        mmap_anon_fd_test(i, false);

    return 0;
}
