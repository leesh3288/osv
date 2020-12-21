#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <thread>
#include <osv/sched.hh>
#include <chrono>

#define ITER_CNT 10000

int main(int argc, char *argv[])
{
    void * small_heap_addr, * large_heap_addr, * mmap_addr, * mmap_32bit_addr;
    size_t time_ns;

    std::chrono::steady_clock::time_point begin;
    std::chrono::steady_clock::time_point end;

    printf("[*] mmap-munmap : ");
    begin = std::chrono::steady_clock::now();
    for(int i = 0; i < ITER_CNT; i++){
        mmap_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        munmap(mmap_addr, 0x1000);
    }
    end = std::chrono::steady_clock::now();
    time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    printf("%f Kops/s\n",(float)ITER_CNT/1000/time_ns*10000000000);

    printf("[*] mmap-munmap (32bit) : ");
    begin = std::chrono::steady_clock::now();
    for(int i = 0; i < ITER_CNT; i++){
        mmap_32bit_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_32BIT, -1, 0);
        munmap(mmap_32bit_addr, 0x1000);
    }
    end = std::chrono::steady_clock::now();
    time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    printf("%f Kops/s\n",(float)ITER_CNT/1000/time_ns*10000000000);

    
    printf("[*] thread create-join : ");
    begin = std::chrono::steady_clock::now();
    for(int i = 0; i < ITER_CNT; i++){
        std::thread t = std::thread([]() {
            //bearly do nothing
            int i = 0;
            i++;
        });
        t.join();
    }
    end = std::chrono::steady_clock::now();
    time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    printf("%f Kops/s\n",(float)ITER_CNT/1000/time_ns*10000000000);

    printf("[*] malloc-free (small) : ");
    begin = std::chrono::steady_clock::now();
    for(int i = 0; i < ITER_CNT; i++){
        small_heap_addr = malloc(0x80);
        free(small_heap_addr);
    }
    end = std::chrono::steady_clock::now();
    time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    printf("%f Kops/s\n",(float)ITER_CNT/1000/time_ns*10000000000);

    printf("[*] malloc-free (large) : ");
    begin = std::chrono::steady_clock::now();
    for(int i = 0; i < ITER_CNT; i++){
        large_heap_addr = malloc(0x10000);
        free(large_heap_addr);
    }
    end = std::chrono::steady_clock::now();
    time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    printf("%f Kops/s\n",(float)ITER_CNT/1000/time_ns*10000000000); 
    
    return 0;
}
