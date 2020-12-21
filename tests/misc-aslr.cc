#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <thread>
#include <osv/sched.hh>

int main(int argc, char *argv[])
{
    int stack_var;
    void * small_heap_addr, * large_heap_addr, * mmap_addr, * mmap_32bit_addr;
    
    printf("ADDRESS_LIST:{");
    
    small_heap_addr = malloc(0x80);
    large_heap_addr = malloc(0x10000);
    mmap_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    mmap_32bit_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_32BIT, -1, 0);

    printf("\"mmaped\":\"%p\",", mmap_addr);
    printf("\"32bit_mmaped\":\"%p\",", mmap_32bit_addr);
    printf("\"main_stack\":\"%p\",", &stack_var);

    std::thread t = std::thread([]() {
        int thread_stack_var;
        printf("\"thread_stack\" : \"%p\",", &thread_stack_var);
    });

    t.join();

    printf("\"small_heap\":\"%p\",", small_heap_addr);
    printf("\"large_heap\":\"%p\",", large_heap_addr);

    printf("\"main_program\":\"%p\"", main);
    //printf("kernel base address (sched::cpu::schedule function) : %p\n", sched::cpu::schedule);

    printf("}\n");
    
    
    return 0;
}
