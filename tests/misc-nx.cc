#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <sys/mman.h>
#include <osv/sched.hh>
#include <osv/debug.h>

#define NOT_REACHED abort("FAIL %s\n", __func__)


void test_mmap_NX()
{
    void *mmap_addr;
    
    mmap_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(mmap_addr != MAP_FAILED);
    
    // NX fault in mmaped region. This fails by default.
    *(volatile char*)mmap_addr = 0xc3;
    ((void(*)())mmap_addr)();

    NOT_REACHED;
}

void test_malloc_small_NX()
{
    void *malloc_addr;

    malloc_addr = malloc(0x80);
    assert(malloc_addr != NULL);

    // NX fault in small malloc region. This fails by default.
    *(volatile char*)malloc_addr = 0xc3;
    ((void(*)())malloc_addr)();

    NOT_REACHED;
}

void test_malloc_large_NX()
{
    void *malloc_addr;

    malloc_addr = malloc(0x40000);
    assert(malloc_addr != NULL);

    // NX fault in large malloc region, which will likely be allocated by mmap.
    // This fails by default.
    *(volatile char*)malloc_addr = 0xc3;
    ((void(*)())malloc_addr)();

    NOT_REACHED;
}

void test_kernel_malloc_NX(char *argv[])
{
    void *kernel_malloc_addr = (void*)argv;

    assert(kernel_malloc_addr != NULL);

    printf("argv: %p\n", kernel_malloc_addr);

    // NX fault in kernel malloc region, allocated using linear mapper.
    // Since kernel malloc() is not exposed, we use argv which is allocated from kernel malloc().
    // This succeeds by default. After implementation of W^X, this should fail.
    *(volatile char*)kernel_malloc_addr = 0xc3;
    ((void(*)())kernel_malloc_addr)();

    NOT_REACHED;
}

void test_internal_thread_stack_NX()
{
    auto th = sched::thread::make([](){
        volatile char c;
        void *stack_addr = (void*)&c;

        assert(stack_addr != NULL);

        printf("internal thread stack: %p\n", stack_addr);

        // Write to OSv internal stack and execute.
        // This succeeds by default. After implementation of W^X, this should fail.
        *(volatile char*)stack_addr = 0xc3;
        ((void(*)())stack_addr)();
    });

    th->start();
    th->join();
    delete th;

    NOT_REACHED;
}

void test_kernel_elf_NX()
{
    void *kernel_func_addr = (void*)static_cast<void(*)(const char *)>(debug);

    assert(kernel_func_addr != NULL);

    printf("debug: %p\n", kernel_func_addr);

    // Write to kernel function and execute.
    // This succeeds by default. After implementation of W^X, this should fail.
    *(volatile char*)kernel_func_addr = 0xc3;
    ((void(*)(const char *))kernel_func_addr)("If function is overwritten, this won't be printed");

    NOT_REACHED;
}

int main(int argc, char *argv[], char *envp[])
{
    //test_mmap_NX();
    //test_malloc_small_NX();
    //test_malloc_large_NX()
    //test_kernel_malloc_NX(argv);
    //test_internal_thread_stack_NX();
    test_kernel_elf_NX();

    return 0;
}