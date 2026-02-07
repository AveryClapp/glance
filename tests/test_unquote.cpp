#include <catch2/catch_test_macros.hpp>
#include "include/csv_reader.hpp"

TEST_CASE("unquote: plain field passes through", "[unquote]") {
  REQUIRE(unquote("hello") == "hello");
  REQUIRE(unquote("some data") == "some data");
}

TEST_CASE("unquote: strips surrounding double quotes", "[unquote]") {
  REQUIRE(unquote("\"hello\"") == "hello");
  REQUIRE(unquote("\"world\"") == "world");
}

TEST_CASE("unquote: unescapes doubled quotes", "[unquote]") {
  REQUIRE(unquote("\"He said \"\"hi\"\"\"") == "He said \"hi\"");
  REQUIRE(unquote("\"a\"\"b\"") == "a\"b");
}

TEST_CASE("unquote: empty string", "[unquote]") {
  REQUIRE(unquote("") == "");
}

TEST_CASE("unquote: empty quoted field", "[unquote]") {
  REQUIRE(unquote("\"\"") == "");
}

TEST_CASE("unquote: single quote not stripped", "[unquote]") {
  // A single " character is not bookended, so it passes through
  std::string result = unquote("\"");
  REQUIRE(result == "\"");
}

TEST_CASE("unquote: multiple escaped quotes", "[unquote]") {
  // Four quotes: "" "" -> after stripping outer quotes: "" -> one "
  REQUIRE(unquote("\"\"\"\"") == "\"");
}

TEST_CASE("unquote: quoted field with comma", "[unquote]") {
  REQUIRE(unquote("\"Smith, John\"") == "Smith, John");
  REQUIRE(unquote("\"a,b,c\"") == "a,b,c");
}
