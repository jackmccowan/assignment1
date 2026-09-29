cmake_minimum_required(VERSION 3.16)
project(union_find LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Release: benchmarks. Debug: tests with sanitizers.
if(MSVC)
  add_compile_options(/W4)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
  # MSVC supports AddressSanitizer only (no UBSan).
  set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /fsanitize=address")
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native -DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")
endif()

add_library(uf INTERFACE)
target_include_directories(uf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(tests tests/test_union_find.cpp)
target_link_libraries(tests PRIVATE uf)

add_executable(bench bench/bench_main.cpp)
target_link_libraries(bench PRIVATE uf)

add_executable(demo demo/demo_main.cpp)
target_link_libraries(demo PRIVATE uf)

enable_testing()
add_test(NAME union_find_tests COMMAND tests)
