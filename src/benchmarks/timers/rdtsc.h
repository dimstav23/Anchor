#pragma once

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

unsigned long get_time();

inline uint64_t get_tsc() { //should be inlined to remove function call overhead
    uint32_t eax;
    uint32_t edx;
    asm volatile ("rdtsc" : "=a" (eax), "=d" (edx));
    return (static_cast<uint64_t>(edx) << 32) | eax;
}

uint64_t get_time_in_ns(size_t tsc_value);

uint64_t get_time_in_ms(size_t tsc_value);

long double get_time_in_s(size_t tsc_value);

uint64_t get_time(size_t cpu_cycles);

uint64_t get_time_diff(uint64_t start, uint64_t end);

long double get_time_diff_in_s(long double start, long double end);
