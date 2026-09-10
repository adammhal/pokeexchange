cmake_minimum_required(VERSION 3.20)
project(pokex LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# The engine itself: headers only, no I/O, no threads, no clock.
add_library(pokex INTERFACE)
target_include_directories(pokex INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pokex INTERFACE cxx_std_20)

add_library(pokex_warnings INTERFACE)
target_compile_options(pokex_warnings INTERFACE -Wall -Wextra -Wpedantic)

option(POKEX_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(POKEX_SANITIZE)
  target_compile_options(pokex_warnings INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(pokex_warnings INTERFACE -fsanitize=address,undefined)
endif()

add_executable(pokex-replay src/replay_main.cpp)
target_link_libraries(pokex-replay PRIVATE pokex pokex_warnings)

add_executable(pokex-sim src/sim_main.cpp)
target_link_libraries(pokex-sim PRIVATE pokex pokex_warnings)

add_executable(pokex-bench src/bench_main.cpp)
target_include_directories(pokex-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pokex-bench PRIVATE pokex pokex_warnings)

option(POKEX_BUILD_TESTS "Build the test suite" ON)
if(POKEX_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.0
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(pokex-tests
    tests/test_matching.cpp
    tests/test_properties.cpp
    tests/test_order_types.cpp
    tests/test_modify.cpp
    tests/test_sim.cpp)
  target_include_directories(pokex-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_link_libraries(pokex-tests PRIVATE pokex pokex_warnings Catch2::Catch2WithMain)

  enable_testing()
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(pokex-tests)

  # Differential and determinism checks drive the replay CLI, so they live in
  # CTest rather than in the C++ binary.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    # Each flow gets a fresh engine, and v1's construction cost is proportional
    # to the price domain rather than to the number of orders: it builds 65,536
    # std::deque levels per side. That is irrelevant in production, where the
    # book is constructed once, but at 10,000 flows under a sanitizer it
    # dominates the whole test run. So the count is tunable, and the sanitizer
    # build uses fewer.
    set(POKEX_DIFF_FLOWS 10000 CACHE STRING "Flows per differential test")
    foreach(book v0 v1 v2 v3)
      add_test(NAME differential_vs_python_reference_${book}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_differential.py
                --replay $<TARGET_FILE:pokex-replay> --book ${book}
                --flows ${POKEX_DIFF_FLOWS})
    endforeach()
    add_test(NAME sim_is_deterministic
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_sim_determinism.py
              --sim $<TARGET_FILE:pokex-sim>)
    add_test(NAME replay_is_deterministic
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_determinism.py
              --replay $<TARGET_FILE:pokex-replay>)
  else()
    message(STATUS "Python3 not found: skipping differential and determinism tests")
  endif()
endif()
