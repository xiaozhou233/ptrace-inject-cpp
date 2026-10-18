cmake_minimum_required(VERSION 3.16)
project(ptrace_inject_cpp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(PTRACE_INJECT_BUILD_CLI "Build the command line tool" ON)
option(PTRACE_INJECT_BUILD_EXAMPLES "Build the examples" ON)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

add_library(ptrace_inject
  src/library.cpp
  src/log.cpp
  src/util.cpp
  src/maps.cpp
  src/memory.cpp
  src/ptrace_utils.cpp
  src/symbols.cpp
  src/inject.cpp
  src/process_lookup.cpp
)

add_library(ptrace_inject::ptrace_inject ALIAS ptrace_inject)

target_include_directories(ptrace_inject
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(ptrace_inject PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ptrace_inject PRIVATE dl)

set_target_properties(ptrace_inject PROPERTIES
  OUTPUT_NAME ptrace_inject
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# CLI and examples
# ---------------------------------------------------------------------------

if(PTRACE_INJECT_BUILD_CLI)
  add_executable(ptrace_inject_cli src/main.cpp)
  set_target_properties(ptrace_inject_cli PROPERTIES OUTPUT_NAME ptrace-inject-cpp)
  target_link_libraries(ptrace_inject_cli PRIVATE ptrace_inject)
endif()

if(PTRACE_INJECT_BUILD_EXAMPLES)
  add_executable(example_by_pid examples/inject_by_pid.c)
  set_target_properties(example_by_pid PROPERTIES LINKER_LANGUAGE CXX)
  target_link_libraries(example_by_pid PRIVATE ptrace_inject)

  add_executable(example_by_name examples/inject_by_name.cpp)
  target_link_libraries(example_by_name PRIVATE ptrace_inject)

  add_library(sample_library SHARED examples/sample_library.c)
  set_target_properties(sample_library PROPERTIES
    OUTPUT_NAME ptrace_inject_sample
  )
endif()
