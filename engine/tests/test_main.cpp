#include "check.hpp"

namespace isrt::test {

namespace {
int failures = 0;
}

std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

void fail(const char* file, int line, const char* expr) {
  ++failures;
  std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expr);
}

}  // namespace isrt::test

int main() {
  int failed_cases = 0;
  for (const auto& c : isrt::test::registry()) {
    const int before = isrt::test::failures;
    c.body();
    const bool ok = isrt::test::failures == before;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", c.name);
    if (!ok) ++failed_cases;
  }
  std::printf("%zu cases, %d failed\n", isrt::test::registry().size(), failed_cases);
  return failed_cases == 0 ? 0 : 1;
}
