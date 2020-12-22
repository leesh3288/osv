#include <atomic>
#include <sys/stat.h>
#include <sys/mman.h>
#include <osv/mmu.hh>
#include <osv/sched.hh>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>

#include <iostream>

// Force cache coherent behavior for thread tests
static std::atomic_int tests{0}, fails{0};

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expect_x(addr) expect(mmu::get_pte_perm((void*)(addr)) & mmu::perm_exec, static_cast<unsigned int>(mmu::perm_exec))
#define expect_nx(addr) expect(mmu::get_pte_perm((void*)(addr)) & mmu::perm_exec, 0u)
#define expect_w(addr) expect(mmu::get_pte_perm((void*)(addr)) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write))
#define expect_nw(addr) expect(mmu::get_pte_perm((void*)(addr)) & mmu::perm_write, 0u)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                ", expected " << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    return true;
}
bool do_expect(u8 actual, u8 expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                ", expected " << expecteds << ", saw 0x" << std::hex << static_cast<unsigned>(actual) << std::dec << ".\n";
        return false;
    }
    return true;
}
#define perm(addr) 

int main(int argc, char **argv)
{
    void *addr;

    // 1. Check that stack is non-executable
    expect_w(&addr);
    expect_nx(&addr);

    // 2. Check that application executable section is non-writable
    expect_nw(main);
    expect_x(main);

    // 3. Check that the semantics of mmap is unchanged - map WX pages if requested
    
    // 3.1 Large Page
    expect((addr = mmap(nullptr, mmu::pt_level_traits<1>::size::value,
        PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE, -1, 0)) != nullptr, true);
    expect_w(addr);
    expect_x(addr);
    expect(munmap(addr, mmu::pt_level_traits<1>::size::value), 0);
    
    // 3.2 Small Page
    expect((addr = mmap(nullptr, mmu::pt_level_traits<0>::size::value,
        PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE, -1, 0)) != nullptr, true);
    expect_w(addr);
    expect_x(addr);
    expect(munmap(addr, mmu::pt_level_traits<0>::size::value), 0);

    // 4. Check that the semantics of mprotect is unchanged - set WX prot if requested
    expect((addr = mmap(nullptr, mmu::pt_level_traits<1>::size::value,
        PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE, -1, 0)) != nullptr, true);
    expect_w(addr);
    expect_nx(addr);

    *(volatile u8*)addr = u8(0xc3); // ret
    *(volatile u8*)((uintptr_t)addr + mmu::pt_level_traits<0>::size::value) = u8(0xaa);

    // 4.1 mprotect partial mmaped range
    expect(mprotect(addr, mmu::pt_level_traits<0>::size::value, PROT_READ|PROT_EXEC), 0);
    expect_nw(addr);
    expect_x(addr);

    expect(*(u8*)addr, u8(0xc3));
    ((void(*)())addr)();  // Check that this is actually executable

    // 4.2 Verify that only the mprotected range changed (similar to tst-mmap testcases)
    expect_w((uintptr_t)addr + mmu::pt_level_traits<0>::size::value);
    expect_nx((uintptr_t)addr + mmu::pt_level_traits<0>::size::value);
    expect(*(u8*)((uintptr_t)addr + mmu::pt_level_traits<0>::size::value), u8(0xaa));
    expect((*(u8*)((uintptr_t)addr + mmu::pt_level_traits<0>::size::value) = u8(0xbb)), u8(0xbb));
    
    // 4.3 RWX mprotects should still work
    expect(mprotect(addr, mmu::pt_level_traits<0>::size::value, PROT_READ|PROT_WRITE|PROT_EXEC), 0);
    expect_w(addr);
    expect_x(addr);

    ((volatile u8*)addr)[0] = u8(0x90);  // nop
    ((volatile u8*)addr)[1] = u8(0xc3);  // ret
    ((void(*)())addr)();

    expect(munmap(addr, mmu::pt_level_traits<1>::size::value), 0);

    // 5. Check small malloc W^X
    expect((addr = malloc(0x80)) != nullptr, true);
    expect_w(addr);
    expect_nx(addr);
    free(addr);

    // 6. Check large malloc W^X
    expect((addr = malloc(0x40000)) != nullptr, true);
    expect_w(addr);
    expect_nx(addr);
    free(addr);

    // 7. Test kernel malloc W^X
    // This exploits the fact that argv is allocated from kernel with malloc().
    expect(argv != nullptr, true);
    expect_w(argv);
    expect_nx(argv);

    // 8. Test OSv internal thread stack W^X (kernel malloc W^X #2)
    auto th = sched::thread::make([](){
        volatile u8 c;
        expect_w(&c);
        expect_nx(&c);
    });
    th->start();
    th->join();
    delete th;

    // 9. Test OSv executable section W^X
    // Randomly chosen function, replaceable with any executable section
    expect_nw(mmu::get_pte_perm);
    expect_x(mmu::get_pte_perm);

    // 10. Test OSv writable section W^X
    // Randomly chosen variable, replaceable with any writable section
    expect_w(&mmu::phys_mem);
    expect_nx(&mmu::phys_mem);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
