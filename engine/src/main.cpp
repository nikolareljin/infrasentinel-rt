#include <cstdio>
#include <cstring>
#include <iostream>

#include "isrt/replay.hpp"
#include "isrt/version.hpp"

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: isrt --version\n"
               "       isrt replay < events   (format: docs/engine/replay.md)\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  if (std::strcmp(argv[1], "--version") == 0) {
    std::printf("isrt %.*s (%.*s)\n", static_cast<int>(isrt::version().size()),
                isrt::version().data(), static_cast<int>(isrt::platform().size()),
                isrt::platform().data());
    return 0;
  }
  if (std::strcmp(argv[1], "replay") == 0) {
    std::ios::sync_with_stdio(false);
    return isrt::replay(std::cin, std::cout, std::cerr);
  }
  return usage();
}
