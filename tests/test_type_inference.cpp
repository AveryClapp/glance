#include <catch2/catch_test_macros.hpp>
#include "include/csv_reader.hpp"
#include "include/delim.hpp"
#include "include/type_inference.hpp"
#include "test_helpers.hpp"

TEST_CASE("type_name: all enum values", "[type_inference]") {
  REQUIRE(type_name(ColumnType::Int64) == "int64");
  REQUIRE(type_name(ColumnType::Float64) == "float64");
  REQUIRE(type_name(ColumnType::Date) == "date");
  REQUIRE(type_name(ColumnType::Currency) == "currency");
  REQUIRE(type_name(ColumnType::Bool) == "bool");
  REQUIRE(type_name(ColumnType::Enum) == "enum");
  REQUIRE(type_name(ColumnType::Text) == "text");
}

TEST_CASE("infer_schema: basic.csv types", "[type_inference]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  REQUIRE(schema.size() == 6);
  REQUIRE(schema[0].name == "name");
  REQUIRE(schema[0].type == ColumnType::Text);
  REQUIRE(schema[1].name == "age");
  REQUIRE(schema[1].type == ColumnType::Int64);
  REQUIRE(schema[2].name == "salary");
  REQUIRE(schema[2].type == ColumnType::Currency);
  REQUIRE(schema[3].name == "active");
  REQUIRE(schema[3].type == ColumnType::Bool);
  REQUIRE(schema[4].name == "start_date");
  REQUIRE(schema[4].type == ColumnType::Date);
}

TEST_CASE("infer_schema: int64 column", "[type_inference]") {
  TempCsv csv("val\n1\n-2\n+3\n42\n0\n100\n");
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Int64);
}

TEST_CASE("infer_schema: float64 column", "[type_inference]") {
  TempCsv csv("val\n1.5\n-2.3\n0.0\n3.14\n");
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Float64);
}

TEST_CASE("infer_schema: date column", "[type_inference]") {
  TempCsv csv("val\n2024-01-15\n2023-12-31\n2020-06-01\n");
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Date);
}

TEST_CASE("infer_schema: currency column", "[type_inference]") {
  TempCsv csv("val\n$12.99\n$1,200.00\n$0.50\n");
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Currency);
}

TEST_CASE("infer_schema: bool column", "[type_inference]") {
  TempCsv csv("val\ntrue\nfalse\nYES\nno\n1\n0\n");
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Bool);
}

TEST_CASE("infer_schema: mixed types fall to text", "[type_inference]") {
  TempCsv csv("val\n123\nhello\n2024-01-01\n");
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Text);
}

TEST_CASE("infer_schema: enum detection", "[type_inference]") {
  // 30 rows with only 2 unique values -> enum
  // threshold: unique < max(2, count/10) = max(2, 3) = 3, 2 < 3 = true
  std::string content = "status\n";
  for (int i = 0; i < 30; ++i)
    content += (i % 2 == 0) ? "A\n" : "B\n";
  TempCsv csv(content);
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Enum);
}

TEST_CASE("infer_schema: empty column is text", "[type_inference]") {
  TempCsv csv("val\n\n\n\n");
  CsvReader reader(csv.path());
  reader.parse(',');
  auto schema = infer_schema(reader);
  REQUIRE(schema[0].type == ColumnType::Text);
}
