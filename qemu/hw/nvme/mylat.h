#ifndef MY_LATENCY_H
#define MY_LATENCY_H


#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
// #include <sys/time.h>

#define ptime_t uint64_t

const uint64_t CPU_FREQUENCY = 2500; // CPU Frequency settings
const uint64_t BASE_LATENCY_NS = 80; // Basic local latency - ns

static inline uint64_t ns_to_cycles(ptime_t ns);
static inline ptime_t cycles_to_ns(uint64_t cycles);
static inline ptime_t get_rdtsc(void);
static inline ptime_t get_rdtscp(void);
inline void simulate_latency(ptime_t ns);

static inline uint64_t ns_to_cycles(ptime_t ns)
{
    return ns * CPU_FREQUENCY / 1000;
}

static inline ptime_t cycles_to_ns(uint64_t cycles)
{
    return cycles * 1000 / CPU_FREQUENCY;
}

#if defined(__i386__)
static inline uint64_t get_rdtsc(void)
{
    uint64_t lo, hi;
    __asm__ volatile("rdtsc"
                     : "=a"(lo), "=d"(hi));
    return lo | hi << 32;
}

static inline uint64_t get_rdtscp(void)
{
    uint64_t lo, hi;
    __asm__ volatile("rdtscp"
                     : "=a"(lo), "=d"(hi)::"ecx");
    return lo | hi << 32;
}

#elif defined(__x86_64__)
static inline uint64_t get_rdtsc(void)
{
    uint64_t hi, lo;
    __asm__ __volatile__("rdtsc"
                         : "=a"(lo), "=d"(hi));
    return lo | hi << 32;
}

static inline uint64_t get_rdtscp(void)
{
    uint64_t hi, lo;
    __asm__ __volatile__("rdtscp"
                         : "=a"(lo), "=d"(hi)::"rcx");
    return lo | hi << 32;
}

#endif

inline void simulate_latency(ptime_t ns)
{
    if (ns == 0) return;
    ptime_t cycles;
    ptime_t start;
    ptime_t end;

    start = get_rdtsc();
    // cycles = ns_to_cycles(((ns >> 6) + 1) * BASE_LATENCY_NS);
    cycles = ns_to_cycles(ns);

    do
    {
        end = get_rdtsc();
    } while (end - start < cycles);
}

#endif