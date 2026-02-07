include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(glance_tests
  test_unquote.cpp
  test_csv_reader.cpp
  test_delim.cpp
  test_type_inference.cpp
  test_filter.cpp
  test_output.cpp
)

target_link_libraries(glance_tests PRIVATE glance_lib Catch2::Catch2WithMain)
target_compile_definitions(glance_tests PRIVATE
  TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

catch_discover_tests(glance_tests)
