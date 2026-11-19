#include "isrt/version.hpp"

namespace isrt {

std::string_view platform() noexcept {
#if defined(__QNXNTO__)
  return "QNX";
#elif defined(__linux__)
  return "Linux";
#else
  return "unknown";
#endif
}

}  // namespace isrt
