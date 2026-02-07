#include <catch2/catch_test_macros.hpp>
#include "include/csv_reader.hpp"
#include "include/delim.hpp"
#include "test_helpers.hpp"

TEST_CASE("detect_delimiter: comma-separated data", "[delim]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  REQUIRE(detect_delimiter(reader.data(), reader.size()) == ',');
}

TEST_CASE("detect_delimiter: tab-separated data", "[delim]") {
  CsvReader reader(fixture_path("tabs.tsv").c_str());
  REQUIRE(detect_delimiter(reader.data(), reader.size()) == '\t');
}

TEST_CASE("detect_delimiter: pipe-separated data", "[delim]") {
  CsvReader reader(fixture_path("pipes.csv").c_str());
  REQUIRE(detect_delimiter(reader.data(), reader.size()) == '|');
}

TEST_CASE("detect_delimiter: semicolon-separated data", "[delim]") {
  CsvReader reader(fixture_path("semicolons.csv").c_str());
  REQUIRE(detect_delimiter(reader.data(), reader.size()) == ';');
}

TEST_CASE("detect_delimiter: empty data returns comma", "[delim]") {
  REQUIRE(detect_delimiter("", 0) == ',');
}

TEST_CASE("detect_delimiter: single comma line", "[delim]") {
  TempCsv csv("a,b,c\n");
  CsvReader reader(csv.path());
  REQUIRE(detect_delimiter(reader.data(), reader.size()) == ',');
}

TEST_CASE("detect_delimiter: quoted commas in pipe-delimited data",
          "[delim]") {
  TempCsv csv("a|b|c\n\"x,y\"|d|e\n1|2|3\n");
  CsvReader reader(csv.path());
  REQUIRE(detect_delimiter(reader.data(), reader.size()) == '|');
}

TEST_CASE("detect_delimiter: sample_lines parameter", "[delim]") {
  // 3 tab-delimited lines, then 10 comma-delimited lines
  std::string content = "a\tb\tc\n1\t2\t3\n4\t5\t6\n";
  for (int i = 0; i < 10; ++i)
    content += "x,y,z\n";
  TempCsv csv(content);
  CsvReader reader(csv.path());
  // Sampling only 3 lines should detect tab
  REQUIRE(detect_delimiter(reader.data(), reader.size(), 3) == '\t');
}
