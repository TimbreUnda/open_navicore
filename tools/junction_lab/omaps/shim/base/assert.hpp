// Shim for Organic Maps base/assert.hpp (junction lab host build).
// ASSERT* follow OM release builds: compiled out, arguments not evaluated.
// CHECK* stay active but throw omaps_shim::CheckFailed instead of aborting,
// so one bad route does not kill the lab tool.
#pragma once

#include <sstream>
#include <stdexcept>
#include <string>

namespace omaps_shim
{
struct CheckFailed : std::runtime_error
{
  using std::runtime_error::runtime_error;
};

[[noreturn]] inline void FailCheck(char const * file, int line, char const * what)
{
  std::ostringstream out;
  out << "CHECK failed " << file << ":" << line << ": " << what;
  throw CheckFailed(out.str());
}
}  // namespace omaps_shim

#define ASSERT(X, msg) ((void)0)
#define ASSERT_EQUAL(X, Y, msg) ((void)0)
#define ASSERT_NOT_EQUAL(X, Y, msg) ((void)0)
#define ASSERT_LESS(X, Y, msg) ((void)0)
#define ASSERT_LESS_OR_EQUAL(X, Y, msg) ((void)0)
#define ASSERT_GREATER(X, Y, msg) ((void)0)
#define ASSERT_GREATER_OR_EQUAL(X, Y, msg) ((void)0)
#define ASSERT_FAIL(msg) ((void)0)

#define CHECK(X, msg)                                        \
  do                                                         \
  {                                                          \
    if (!(X))                                                \
      ::omaps_shim::FailCheck(__FILE__, __LINE__, #X);       \
  }                                                          \
  while (false)
#define CHECK_EQUAL(X, Y, msg) CHECK((X) == (Y), msg)
#define CHECK_NOT_EQUAL(X, Y, msg) CHECK((X) != (Y), msg)
#define CHECK_LESS(X, Y, msg) CHECK((X) < (Y), msg)
#define CHECK_LESS_OR_EQUAL(X, Y, msg) CHECK((X) <= (Y), msg)
#define CHECK_GREATER(X, Y, msg) CHECK((X) > (Y), msg)
#define CHECK_GREATER_OR_EQUAL(X, Y, msg) CHECK((X) >= (Y), msg)
#define UNREACHABLE() ::omaps_shim::FailCheck(__FILE__, __LINE__, "unreachable")
