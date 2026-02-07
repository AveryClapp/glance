cmake_minimum_required(VERSION 3.20)
project(Glance VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Core library (everything except main.cpp) ---
add_library(glance_lib STATIC
  src/csv_reader.cpp
  src/delim.cpp
  src/type_inference.cpp
  src/tui.cpp
  src/filter.cpp
  src/pager.cpp
)
target_include_directories(glance_lib PUBLIC ${CMAKE_SOURCE_DIR})

# --- Main executable ---
add_executable(glance src/main.cpp)
target_link_libraries(glance PRIVATE glance_lib)

install(TARGETS glance DESTINATION bin)

# --- Tests (opt-in) ---
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
  include(CTest)
  add_subdirectory(tests)
endif()
