#pragma once

#include <string_view>

namespace isrt {

// Version of the engine, taken from the VERSION file at configure time.
std::string_view version() noexcept;

// Operating system the engine was built for: "QNX", "Linux" or "unknown".
std::string_view platform() noexcept;

}  // namespace isrt
