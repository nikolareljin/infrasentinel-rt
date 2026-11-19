cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ISRT_VERSION)
string(STRIP "${ISRT_VERSION}" ISRT_VERSION)

project(infrasentinel_rt VERSION ${ISRT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ISRT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ISRT_WERROR "Treat warnings as errors" ON)

enable_testing()
add_subdirectory(engine)
