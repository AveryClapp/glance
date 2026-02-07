#include <catch2/catch_test_macros.hpp>
#include "include/csv_reader.hpp"
#include "include/delim.hpp"
#include "test_helpers.hpp"

TEST_CASE("CsvReader: open basic.csv", "[csv_reader]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  REQUIRE(reader.data() != nullptr);
  REQUIRE(reader.size() == 496);
}

TEST_CASE("CsvReader: parse basic.csv fully", "[csv_reader]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  REQUIRE(reader.column_count() == 6);
  REQUIRE(reader.row_count() == 10);
  REQUIRE(reader.total_rows() == 10);

  auto &headers = reader.headers();
  REQUIRE(unquote(headers[0]) == "name");
  REQUIRE(unquote(headers[1]) == "age");
  REQUIRE(unquote(headers[2]) == "salary");
  REQUIRE(unquote(headers[3]) == "active");
  REQUIRE(unquote(headers[4]) == "start_date");
  REQUIRE(unquote(headers[5]) == "department");
}

TEST_CASE("CsvReader: parse_head with limit 3", "[csv_reader]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse_head(',', 3);
  REQUIRE(reader.row_count() == 3);
  REQUIRE(reader.total_rows() == 10);
}

TEST_CASE("CsvReader: row accessor returns correct values", "[csv_reader]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');

  auto row0 = reader.row(0);
  REQUIRE(row0.size() == 6);
  REQUIRE(unquote(row0[0]) == "Alice");
  REQUIRE(unquote(row0[1]) == "30");

  auto row2 = reader.row(2);
  REQUIRE(unquote(row2[0]) == "Charlie");
  REQUIRE(unquote(row2[1]) == "35");
}

TEST_CASE("CsvReader: quoted.csv with embedded newlines", "[csv_reader]") {
  CsvReader reader(fixture_path("quoted.csv").c_str());
  char delim = detect_delimiter(reader.data(), reader.size());
  reader.parse(delim);
  REQUIRE(reader.row_count() == 3);
  REQUIRE(reader.column_count() == 3);

  // Row 1 (Doe, Jane) has a newline in the description field
  std::string desc = unquote(reader.row(1)[1]);
  REQUIRE(desc.find('\n') != std::string::npos);
}

TEST_CASE("CsvReader: quoted.csv with escaped quotes", "[csv_reader]") {
  CsvReader reader(fixture_path("quoted.csv").c_str());
  char delim = detect_delimiter(reader.data(), reader.size());
  reader.parse(delim);

  // Row 0 (Smith, John) description: He said "hello"
  std::string desc = unquote(reader.row(0)[1]);
  REQUIRE(desc == "He said \"hello\"");
}

TEST_CASE("CsvReader: tabs.tsv parsed with tab delimiter", "[csv_reader]") {
  CsvReader reader(fixture_path("tabs.tsv").c_str());
  reader.parse('\t');
  REQUIRE(reader.column_count() == 4);
  REQUIRE(reader.row_count() == 4);
  REQUIRE(unquote(reader.headers()[0]) == "id");
}

TEST_CASE("CsvReader: pipes.csv parsed with pipe delimiter", "[csv_reader]") {
  CsvReader reader(fixture_path("pipes.csv").c_str());
  reader.parse('|');
  REQUIRE(reader.column_count() == 4);
  REQUIRE(reader.row_count() == 4);
}

TEST_CASE("CsvReader: edge_cases.csv handles empty fields", "[csv_reader]") {
  CsvReader reader(fixture_path("edge_cases.csv").c_str());
  reader.parse(',');
  REQUIRE(reader.row_count() == 9);

  // Row 1 (id=2): label is "hello", value is empty
  std::string val = unquote(reader.row(1)[2]);
  REQUIRE(val.empty());
}

TEST_CASE("CsvReader: edge_cases.csv handles negative numbers",
          "[csv_reader]") {
  CsvReader reader(fixture_path("edge_cases.csv").c_str());
  reader.parse(',');

  // Row 6 (id=7): value is -42
  std::string val = unquote(reader.row(6)[2]);
  REQUIRE(val == "-42");
}

TEST_CASE("CsvReader: large.csv parses 150 rows", "[csv_reader]") {
  CsvReader reader(fixture_path("large.csv").c_str());
  reader.parse(',');
  REQUIRE(reader.row_count() == 150);
  REQUIRE(reader.column_count() == 9);
}

TEST_CASE("CsvReader: parse_head large.csv with limit 10", "[csv_reader]") {
  CsvReader reader(fixture_path("large.csv").c_str());
  reader.parse_head(',', 10);
  REQUIRE(reader.row_count() == 10);
  REQUIRE(reader.total_rows() == 150);
}

TEST_CASE("CsvReader: nonexistent file throws", "[csv_reader]") {
  REQUIRE_THROWS_AS(CsvReader("nonexistent_file_xyz.csv"),
                    std::runtime_error);
}
