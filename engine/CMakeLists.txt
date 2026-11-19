add_library(isrt_core
  src/version.cpp
  src/platform.cpp
)
target_include_directories(isrt_core PUBLIC include)
target_compile_definitions(isrt_core PRIVATE ISRT_VERSION_STRING="${PROJECT_VERSION}")

set(ISRT_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
if(ISRT_WERROR)
  list(APPEND ISRT_WARNINGS -Werror)
endif()
target_compile_options(isrt_core PRIVATE ${ISRT_WARNINGS})

if(ISRT_SANITIZE)
  target_compile_options(isrt_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(isrt_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(isrt src/main.cpp)
target_link_libraries(isrt PRIVATE isrt_core)
target_compile_options(isrt PRIVATE ${ISRT_WARNINGS})

add_executable(isrt_tests tests/test_main.cpp tests/test_version.cpp)
target_link_libraries(isrt_tests PRIVATE isrt_core)
target_compile_options(isrt_tests PRIVATE ${ISRT_WARNINGS})
add_test(NAME isrt_tests COMMAND isrt_tests)
