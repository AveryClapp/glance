#include <catch2/catch_test_macros.hpp>
#include "include/csv_reader.hpp"
#include "include/tui.hpp"
#include "include/type_inference.hpp"
#include "test_helpers.hpp"
#include <sstream>
#include <string>

static size_t count_lines(const std::string &s) {
  size_t n = 0;
  for (char c : s)
    if (c == '\n')
      ++n;
  return n;
}

TEST_CASE("render_csv: basic.csv round-trips headers", "[output]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');

  CaptureStdout cap;
  render_csv(reader, nullptr, nullptr, 10, ',');
  std::string out = cap.str();

  // 1 header + 10 data rows = 11 lines
  REQUIRE(count_lines(out) == 11);

  // First line should be the header
  std::string first_line = out.substr(0, out.find('\n'));
  REQUIRE(first_line == "name,age,salary,active,start_date,department");
}

TEST_CASE("render_csv: column subset", "[output]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');

  std::vector<size_t> cols = {0, 2}; // name, salary

  CaptureStdout cap;
  render_csv(reader, nullptr, &cols, 3, ',');
  std::string out = cap.str();

  std::string first_line = out.substr(0, out.find('\n'));
  REQUIRE(first_line == "name,salary");

  // 1 header + 3 data rows = 4 lines
  REQUIRE(count_lines(out) == 4);
}

TEST_CASE("render_json: produces valid JSON array", "[output]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  CaptureStdout cap;
  render_json(reader, schema, nullptr, nullptr, 2);
  std::string out = cap.str();

  // Should start with [ and contain closing ]
  REQUIRE(out.front() == '[');
  REQUIRE(out.find(']') != std::string::npos);

  // Should contain Alice
  REQUIRE(out.find("\"name\": \"Alice\"") != std::string::npos);

  // int64 should be unquoted
  REQUIRE(out.find("\"age\": 30") != std::string::npos);

  // bool should be JSON boolean
  REQUIRE(out.find("\"active\": true") != std::string::npos);
}

TEST_CASE("render_schema_json: contains column types", "[output]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  CaptureStdout cap;
  render_schema_json(schema, nullptr, 10, reader.size());
  std::string out = cap.str();

  REQUIRE(out.find("\"row_count\": 10") != std::string::npos);
  REQUIRE(out.find("\"columns\"") != std::string::npos);
  REQUIRE(out.find("\"int64\"") != std::string::npos);
  REQUIRE(out.find("\"currency\"") != std::string::npos);
  REQUIRE(out.find("\"bool\"") != std::string::npos);
}
