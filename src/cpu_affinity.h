// program to bind the thread to the first core
#ifndef CPU_AFFINITY_H_INCLUDED
#define CPU_AFFINITY_H_INCLUDED

#if defined(__linux__) && !defined(__ANDROID__)
    #include <sched.h>
#endif

namespace Stockfish {

// Bind current thread to CPU core 0 (Linux only)
// This ensures all threads run on a single CPU core as required
// Note: Requires _GNU_SOURCE to be defined (set via -D_GNU_SOURCE in build flags)
inline void bind_to_cpu_0() {
#if defined(__linux__) && !defined(__ANDROID__)
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    
    // Bind current thread to CPU 0
    // If binding fails, we continue anyway (non-fatal for compatibility)
    // This allows the code to work even if CPU 0 is not in the process’ allowed CPU set
    (void)sched_setaffinity(0, sizeof(mask), &mask);
#endif
    // On other platforms, this is a no-op (Linux-only requirement)
}

}  // namespace Stockfish

#endif  // #ifndef CPU_AFFINITY_H_INCLUDED
