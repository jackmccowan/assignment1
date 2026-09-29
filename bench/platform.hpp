#pragma once

// OS-specific helpers that reduce benchmark noise: pinning the benchmark to
// one CPU and raising its priority. Kept out of bench_main.cpp so the harness
// logic stays readable. None of this runs inside a timed region.
//
// Why pin: on a hybrid CPU such as the i9-13900H, the OS may move the thread
// between fast P-cores and slower E-cores during a run, and any migration also
// loses the warm L1/L2 caches. Both show up as large run-to-run variation.

#include <cstdint>
#include <cstdio>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

namespace bench {

// Picks a logical CPU on the fastest core type, or returns -1 if unknown.
// Windows reports an EfficiencyClass per core: higher means faster, and on a
// hybrid CPU the P-cores have the higher class. Among the fastest cores the
// last one is chosen, because CPU 0 usually handles more interrupts.
// Only processor group 0 is considered (machines with <= 64 logical CPUs).
inline int fastest_cpu() {
#if defined(_WIN32)
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::vector<char> buf(len);
    auto* first = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data());
    if (len == 0 || !GetLogicalProcessorInformationEx(RelationProcessorCore, first, &len)) return -1;

    int best_cpu = -1;
    int best_class = -1;
    for (DWORD offset = 0; offset < len;) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + offset);
        const PROCESSOR_RELATIONSHIP& core = info->Processor;
        const GROUP_AFFINITY& group = core.GroupMask[0];
        if (group.Group == 0 && core.EfficiencyClass >= best_class) {
            // First logical CPU (lowest set bit) of this core; skips its hyperthread sibling.
            for (int bit = 0; bit < 64; ++bit) {
                if ((static_cast<std::uint64_t>(group.Mask) >> bit) & 1u) {
                    best_cpu = bit;
                    best_class = core.EfficiencyClass;
                    break;
                }
            }
        }
        offset += info->Size;
    }
    return best_cpu;
#else
    return -1;  // no portable way to identify P-cores; use --cpu on Linux
#endif
}

// Pins the calling thread to `cpu` and raises its priority. Returns true on success.
inline bool pin_to_cpu(int cpu) {
    if (cpu < 0 || cpu >= 64) return false;
#if defined(_WIN32)
    if (SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << cpu) == 0) return false;
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    return true;
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof set, &set) == 0;
#else
    return false;
#endif
}

}  // namespace bench
