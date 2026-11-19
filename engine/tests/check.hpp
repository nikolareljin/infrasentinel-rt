#pragma once

// A minimal test harness: no third-party framework, so the same tests build
// with g++, clang and qcc.

#include <cstdio>
#include <functional>
#include <vector>

namespace isrt::test {

struct Case {
  const char* name;
  std::function<void()> body;
};

std::vector<Case>& registry();
void fail(const char* file, int line, const char* expr);

struct Register {
  Register(const char* name, std::function<void()> body) {
    registry().push_back({name, std::move(body)});
  }
};

}  // namespace isrt::test

#define ISRT_CAT2(a, b) a##b
#define ISRT_CAT(a, b) ISRT_CAT2(a, b)
#define TEST_CASE(name)                                                                          \
  static void ISRT_CAT(test_fn_, __LINE__)();                                                    \
  static const isrt::test::Register ISRT_CAT(test_reg_, __LINE__)(name,                          \
                                                                  ISRT_CAT(test_fn_, __LINE__)); \
  static void ISRT_CAT(test_fn_, __LINE__)()
#define CHECK(expr)                                           \
  do {                                                        \
    if (!(expr)) isrt::test::fail(__FILE__, __LINE__, #expr); \
  } while (0)
