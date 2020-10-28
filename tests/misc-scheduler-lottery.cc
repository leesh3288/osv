/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test code modified from misc-scheduler.cc.
// Benchmark the correctness of lottery scheduler on a single CPU.
// NOTE: This test must be run with 1 cpu - it will refuse to run with more.
//
// This test checks the following scenarios:
//
// 1. Run several tight loops (wishing to use 100% of the CPU) together,
//    to see they each get a proportional share of the CPU time.
// 2. Similarly, for several threads with different priorities.

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <limits>

#ifdef __OSV__
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#endif

#define LOOP 10000000000ULL

typedef std::chrono::time_point<std::chrono::system_clock> tp_t;

void _loop(int iterations)
{
    for (register int i=0; i<iterations; i++) {
        for (register int j=0; j<10000; j++) {
            // To force gcc to not optimize this loop away
            asm volatile("" : : : "memory");
        }
    }
}

#ifdef __OSV__
void ticket_test(u64 loop, std::vector<ticket_t> ts)
{
    std::cout << "Ticket test:";
    for (auto t: ts)
        std::cout << " " << t;
    std::cout << "\n";

    std::vector<sched::thread*> threads;
    std::atomic<int> ended {0};
    std::vector<std::pair<ticket_t, tp_t>> results;
    mutex mtx;

    int tscnt = (int)ts.size();
    for (auto t: ts) {
        auto thr = sched::thread::make([&mtx, &results, &ended, t, tscnt, loop](){
            u64 val = 0;
            for (u64 i = 1; i <= loop; i++) {
                val *= i;
            }
            tp_t end = std::chrono::system_clock::now();
            WITH_LOCK (mtx) {
                results.emplace_back(t, end);
            }
            ended++;

            while (ended != tscnt) {  // do a busy-loop until all threads end
                _loop(1);
            }
        });
        thr->set_ticket(t);
        threads.push_back(thr);
    }

    // save tester's old ticket & bump ticket priority to max
    ticket_t tester_ticket = sched::thread::current()->ticket();
    sched::thread::current()->set_ticket(sched::thread::ticket_infinity);

    for (auto thr: threads) {
        thr->start();
    }

    tp_t start = std::chrono::system_clock::now();
    sched::thread::current()->set_ticket(tester_ticket);

    for (auto thr: threads) {
        thr->join();
        delete thr;
    }
    auto minlen = results.front().second;
    for (auto x: results) {
        if (x.second < minlen) {
            minlen = x.second;
        }
    }
    std::chrono::duration<float> mind = minlen - start;
    for (auto x: results) {
        std::chrono::duration<float> d = x.second - start;
        std::cout << "Ticket #" << x.first << ": " << d.count() << "s (x" << (d.count() / mind.count()) << ")\n";
    }
    std::cout << "Ticket test done\n";
}
#endif

int main()
{
    if (std::thread::hardware_concurrency() != 1) {
        std::cerr << "Detected " << std::thread::hardware_concurrency() <<
                " CPUs, but this test requires exactly 1.\n";
        return 0;
    }

#ifdef __OSV__
    // given 3 test cases
    ticket_test(LOOP, {100, 100, 100, 100});
    ticket_test(LOOP, {100, 400});
    ticket_test(LOOP, {100, 200, 200, 400});

    // this should be the same as case 3, as it's just scaled by *10000
    ticket_test(LOOP, {1000000, 2000000, 2000000, 4000000});

    // x2 threads compared to case 1, how does the time scale?
    ticket_test(LOOP, {100, 100, 100, 100, 100, 100, 100, 100});

    // now try 8 threads but with exponentially distributed time
    ticket_test(LOOP, {100, 200, 400, 800, 1600, 3200, 6400, 12800});
#endif

    return 0;
}
