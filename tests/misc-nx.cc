#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>

int main(void)
{
    void *mmap_addr;
    
    mmap_addr = mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    *(char*)mmap_addr = 0xc3;
    
    // NX fault in mmaped region. This fails by default.
    ((void(*)())mmap_addr)();

    return 0;
}