#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <sys/mman.h>
#include <osv/sched.hh>
#include <osv/debug.h>


void test_mmap_success_1()
{
    void *mmap_addr;
    
    mmap_addr = mmap(nullptr, 0x100000, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(mmap_addr != MAP_FAILED);

    // This must pass.
    ((char*)mmap_addr)[0xfffff] = 0xc3;
    ((void(*)())((char*)mmap_addr + 0xfffff))();
}

void test_mmap_success_2()
{
    void *mmap_addr;
    
    mmap_addr = mmap(nullptr, 0x100000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(mmap_addr != MAP_FAILED);

    // mprotect to create RWX section. This must pass.
    mprotect((char*)mmap_addr + 0xff000, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC);
    ((char*)mmap_addr)[0xfffff] = 0xc3;
    ((void(*)())((char*)mmap_addr + 0xfffff))();
}

void test_mmap_NX()
{
    void *mmap_addr;
    
    mmap_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(mmap_addr != MAP_FAILED);
    
    // NX fault in mmaped region. This fails by default.
    *(char*)mmap_addr = 0xc3;
    ((void(*)())mmap_addr)();

    assert(false);
}

void test_malloc_small_NX()
{
    void *malloc_addr;

    malloc_addr = malloc(0x80);
    assert(malloc_addr != NULL);

    // NX fault in small malloc region. This fails by default.
    *(char*)malloc_addr = 0xc3;
    ((void(*)())malloc_addr)();

    assert(false);
}

void test_malloc_large_NX()
{
    void *malloc_addr;

    malloc_addr = malloc(0x40000);
    assert(malloc_addr != NULL);

    // NX fault in large malloc region, which will likely be allocated by mmap.
    // This fails by default.
    *(char*)malloc_addr = 0xc3;
    ((void(*)())malloc_addr)();

    assert(false);
}

void test_kernel_malloc_NX(char *argv[])
{
    void *kernel_malloc_addr = (void*)argv;

    assert(kernel_malloc_addr != NULL);

    printf("argv: %p\n", kernel_malloc_addr);

    // NX fault in kernel malloc region, allocated using linear mapper.
    // Since kernel malloc() is not exposed, we use argv which is allocated from kernel malloc().
    // This succeeds by default. After implementation of W^X, this should fail.
    *(char*)kernel_malloc_addr = 0xc3;
    ((void(*)())kernel_malloc_addr)();

    assert(false);
}

void test_internal_thread_stack()
{
    auto th = sched::thread::make([](){
        volatile char c;
        void *stack_addr = (void*)&c;

        assert(stack_addr != NULL);

        // Write to OSv internal stack and execute.
        // This succeeds by default. After implementation of W^X, this should fail.
        *(volatile char*)stack_addr = 0xc3;
        ((void(*)())stack_addr)();

        assert(false);
    });

    th->start();
    th->join();
}

void test_kernel_elf_NX()
{
    void *kernel_func_addr = (void*)static_cast<void(*)(const char *)>(debug);

    assert(kernel_func_addr != NULL);

    // Write to kernel function and execute.
    // This succeeds by default. After implementation of W^X, this should fail.
    *(char*)kernel_func_addr = 0xc3;
    ((void(*)(const char *))kernel_func_addr)("If function is overwritten, this won't be printed");

    assert(false);
}

int main(int argc, char *argv[], char *envp[])
{
    test_mmap_success_1();
    test_mmap_success_2();
    
    //test_kernel_malloc_NX(argv);
    test_internal_thread_stack();

    return 0;
}