#pragma once

// Minimal test helper shared by the test executables. No external framework:
// CHECK records a failure and carries on, and main returns non-zero if any failed.

#include <cstdio>

inline int g_failures = 0;
inline const char* g_variant = "";  // printed with each failure: which variant was under test

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "[%s] %s:%d: CHECK failed: %s\n", g_variant,    \
                         __FILE__, __LINE__, #cond);                             \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)
