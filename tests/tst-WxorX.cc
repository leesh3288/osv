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
#define perm(ptr) mmu::get_pte_perm((void*)addr)

int main(int argc, char **argv)
{
    void *addr;

    // 1. Check that stack is non-executable
    addr = static_cast<void*>(&addr);
    expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(addr) & mmu::perm_exec, 0u);

    // 2. Check that application executable section is non-writable
    addr = (void*)main;
    expect(perm(addr) & mmu::perm_write, 0u);
    expect(perm(addr) & mmu::perm_exec, static_cast<unsigned int>(mmu::perm_exec));

    // 3. Check that the semantics of mmap is unchanged - map WX pages if requested
    expect((addr = mmap(nullptr, 0x100000, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE, -1, 0)) != nullptr, true);
    expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(addr) & mmu::perm_exec, static_cast<unsigned int>(mmu::perm_exec));
    expect(munmap(addr, 0x100000), 0);

    // 4. Check that the semantics of mprotect is unchanged - set WX prot if requested
    expect((addr = mmap(nullptr, 0x100000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE, -1, 0)) != nullptr, true);
    expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(addr) & mmu::perm_exec, 0u);
    expect(mprotect(addr, 0x100000, PROT_READ|PROT_EXEC), 0);
    expect(perm(addr) & mmu::perm_write, 0u);
    expect(perm(addr) & mmu::perm_exec, static_cast<unsigned int>(mmu::perm_exec));
    expect(mprotect(addr, 0x100000, PROT_READ|PROT_WRITE|PROT_EXEC), 0);
    expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(addr) & mmu::perm_exec, static_cast<unsigned int>(mmu::perm_exec));
    expect(munmap(addr, 0x100000), 0);

    // 5. Check small malloc W^X
    expect((addr = malloc(0x80)) != nullptr, true);
    expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(addr) & mmu::perm_exec, 0u);
    free(addr);

    // 6. Check large malloc W^X
    expect((addr = malloc(0x40000)) != nullptr, true);
    expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(addr) & mmu::perm_exec, 0u);
    free(addr);

    // 7. Test kernel malloc W^X
    expect(argv != nullptr, true);
    expect(perm(argv) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(argv) & mmu::perm_exec, 0u);

    // 8. Test OSv internal thread stack W^X (kernel malloc W^X #2)
    auto th = sched::thread::make([](){
        volatile char c;
        void *addr = (void*)&c;
        expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
        expect(perm(addr) & mmu::perm_exec, 0u);
    });
    th->start();
    th->join();
    delete th;

    // 9. Test OSv executable section W^X
    // Randomly chosen function, replaceable with any executable section
    addr = (void*)mmu::get_pte_perm;
    expect(perm(addr) & mmu::perm_write, 0u);
    expect(perm(addr) & mmu::perm_exec, static_cast<unsigned int>(mmu::perm_exec));

    // 10. Test OSv writable section W^X
    // Randomly chosen variable, replaceable with any writable section
    addr = (void*)&mmu::phys_mem;
    expect(perm(addr) & mmu::perm_write, static_cast<unsigned int>(mmu::perm_write));
    expect(perm(addr) & mmu::perm_exec, 0u);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
