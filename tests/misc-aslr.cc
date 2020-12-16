#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <thread>
#include <osv/sched.hh>

int main(int argc, char *argv[])
{
    int stack_var;
    void * small_heap_addr, * large_heap_addr, * mmap_addr, * mmap_32bit_addr;

    small_heap_addr = malloc(0x80);
    large_heap_addr = malloc(0x10000);
    mmap_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    mmap_32bit_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_32BIT, -1, 0);

    printf("mmaped address : %p\n", mmap_addr);
    printf("32bit mmaped address : %p\n", mmap_32bit_addr);
    printf("main program stack address : %p\n", &stack_var);

    std::thread t = std::thread([]() {
        int thread_stack_var;
        printf("thread stack address : %p\n", &thread_stack_var);
    });

    t.join();

    printf("small heap address : %p\n", small_heap_addr);
    printf("large heap address : %p\n", large_heap_addr);

    printf("main program address (main function) : %p\n", main);
    printf("kernel base address (sched::cpu::schedule function) : %p\n", sched::cpu::schedule);
    
    
    return 0;
}
