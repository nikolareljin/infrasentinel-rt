#include <cstdio>
#include <cstring>

#include "isrt/version.hpp"

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("isrt %.*s (%.*s)\n", static_cast<int>(isrt::version().size()),
                isrt::version().data(), static_cast<int>(isrt::platform().size()),
                isrt::platform().data());
    return 0;
  }
  std::fprintf(stderr, "usage: isrt --version\n");
  return 2;
}
