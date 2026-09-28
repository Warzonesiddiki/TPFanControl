#pragma once

#include <cstdio>
#include <cstdlib>

namespace tpfancontrol {
namespace test {

// A test assertion that survives NDEBUG.
//
// std::assert is compiled out when NDEBUG is defined, which is exactly what a
// Release build does. A test suite written with assert therefore either runs
// nothing in Release or, worse, appears to pass. Worse still, the variables
// that existed only to be asserted on become unused, and the suite stops
// compiling at /W4 /WX - which is how this was found.
//
// CHECK has no conditional compilation, so a Release build runs the same
// assertions as a Debug build, and every value is genuinely referenced in both.
inline void reportFailure(const char* file, int line, const char* expression)
{
    std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", file, line, expression);
    std::fflush(stderr);
    std::abort();
}

} // namespace test
} // namespace tpfancontrol

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            ::tpfancontrol::test::reportFailure(__FILE__, __LINE__, #expression); \
        }                                                                        \
    } while (false)
