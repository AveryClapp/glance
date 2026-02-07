#include <catch2/catch_test_macros.hpp>
#include "include/csv_reader.hpp"
#include "include/filter.hpp"
#include "include/type_inference.hpp"
#include "test_helpers.hpp"
#include <algorithm>
#include <numeric>

// --- parse_filter tests ---

TEST_CASE("parse_filter: equality operator", "[filter]") {
  auto f = parse_filter("name == Alice");
  REQUIRE(f.column == "name");
  REQUIRE(f.op == FilterOp::Eq);
  REQUIRE(f.value == "Alice");
}

TEST_CASE("parse_filter: greater than", "[filter]") {
  auto f = parse_filter("age > 30");
  REQUIRE(f.column == "age");
  REQUIRE(f.op == FilterOp::Gt);
  REQUIRE(f.value == "30");
}

TEST_CASE("parse_filter: not equal", "[filter]") {
  auto f = parse_filter("status != active");
  REQUIRE(f.column == "status");
  REQUIRE(f.op == FilterOp::Neq);
  REQUIRE(f.value == "active");
}

TEST_CASE("parse_filter: contains word operator", "[filter]") {
  auto f = parse_filter("name contains Al");
  REQUIRE(f.column == "name");
  REQUIRE(f.op == FilterOp::Contains);
  REQUIRE(f.value == "Al");
}

TEST_CASE("parse_filter: starts_with and ends_with", "[filter]") {
  auto f1 = parse_filter("name starts_with A");
  REQUIRE(f1.op == FilterOp::StartsWith);
  REQUIRE(f1.value == "A");

  auto f2 = parse_filter("name ends_with e");
  REQUIRE(f2.op == FilterOp::EndsWith);
  REQUIRE(f2.value == "e");
}

TEST_CASE("parse_filter: empty expression throws", "[filter]") {
  REQUIRE_THROWS_AS(parse_filter(""), std::runtime_error);
  REQUIRE_THROWS_AS(parse_filter("   "), std::runtime_error);
}

// --- apply_filters tests ---

TEST_CASE("apply_filters: basic equality", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  std::vector<Filter> filters = {{"name", FilterOp::Eq, "Alice"}};
  auto result = apply_filters(filters, reader, schema);
  REQUIRE(result.size() == 1);
  REQUIRE(result[0] == 0);
}

TEST_CASE("apply_filters: numeric greater-than", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  std::vector<Filter> filters = {{"age", FilterOp::Gt, "30"}};
  auto result = apply_filters(filters, reader, schema);
  REQUIRE(result.size() == 5); // Charlie(35), Eve(32), Frank(45), Hank(38), Jack(33)
}

TEST_CASE("apply_filters: case insensitive", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  std::vector<Filter> filters = {{"name", FilterOp::Eq, "alice"}};
  // Without case insensitive: no match
  auto result_cs = apply_filters(filters, reader, schema, false, false);
  REQUIRE(result_cs.empty());

  // With case insensitive: match
  auto result_ci = apply_filters(filters, reader, schema, true, false);
  REQUIRE(result_ci.size() == 1);
  REQUIRE(result_ci[0] == 0);
}

TEST_CASE("apply_filters: OR logic", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  std::vector<Filter> filters = {
      {"department", FilterOp::Eq, "Engineering"},
      {"department", FilterOp::Eq, "Management"},
  };
  // AND logic: impossible (can't be both)
  auto result_and = apply_filters(filters, reader, schema, false, false);
  REQUIRE(result_and.empty());

  // OR logic: Engineering + Management
  auto result_or = apply_filters(filters, reader, schema, false, true);
  REQUIRE(result_or.size() == 6);
}

TEST_CASE("apply_filters: unknown column throws", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  std::vector<Filter> filters = {{"nonexistent", FilterOp::Eq, "foo"}};
  REQUIRE_THROWS_AS(apply_filters(filters, reader, schema),
                    std::runtime_error);
}

// --- sort_indices tests ---

TEST_CASE("sort_indices: ascending numeric sort", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  std::vector<size_t> indices(reader.row_count());
  std::iota(indices.begin(), indices.end(), static_cast<size_t>(0));

  sort_indices(indices, reader, schema, "age", false);

  // First should be youngest (Bob, age 25, original index 1)
  REQUIRE(unquote(reader.row(indices[0])[1]) == "25");
  // Last should be oldest (Frank, age 45)
  REQUIRE(unquote(reader.row(indices.back())[1]) == "45");
}

TEST_CASE("sort_indices: descending string sort", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');
  auto schema = infer_schema(reader);

  std::vector<size_t> indices(reader.row_count());
  std::iota(indices.begin(), indices.end(), static_cast<size_t>(0));

  sort_indices(indices, reader, schema, "name", true);

  // Descending: first name alphabetically last
  std::string first_name = unquote(reader.row(indices[0])[0]);
  std::string last_name = unquote(reader.row(indices.back())[0]);
  REQUIRE(first_name > last_name);
}

// --- resolve_columns tests ---

TEST_CASE("resolve_columns: selects correct indices", "[filter]") {
  CsvReader reader(fixture_path("basic.csv").c_str());
  reader.parse(',');

  auto cols = resolve_columns("name, salary", reader);
  REQUIRE(cols.size() == 2);
  REQUIRE(cols[0] == 0); // name
  REQUIRE(cols[1] == 2); // salary

  REQUIRE_THROWS_AS(resolve_columns("nonexistent", reader),
                    std::runtime_error);
}
