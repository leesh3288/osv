#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <sys/mman.h>


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

int main(int argc, char *argv[], char *envp[])
{
    test_kernel_malloc_NX(argv);

    return 0;
}