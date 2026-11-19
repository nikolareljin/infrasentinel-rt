#include <string_view>

#include "check.hpp"
#include "isrt/version.hpp"

TEST_CASE("version is x.y.z") {
  const std::string_view v = isrt::version();
  int dots = 0;
  for (char c : v) {
    if (c == '.') {
      ++dots;
    } else {
      CHECK(c >= '0' && c <= '9');
    }
  }
  CHECK(dots == 2);
}

TEST_CASE("platform is named") {
  const std::string_view p = isrt::platform();
  CHECK(p == "QNX" || p == "Linux" || p == "unknown");
}
