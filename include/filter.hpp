#pragma once

#include "type_inference.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class CsvReader;

enum class FilterOp {
  Eq,
  Neq,
  Gt,
  Lt,
  Gte,
  Lte,
  Contains,
  StartsWith,
  EndsWith
};

struct Filter {
  std::string column;
  FilterOp op;
  std::string value;
};

Filter parse_filter(std::string_view expr);

std::vector<size_t>
apply_filters(const std::vector<Filter> &filters, const CsvReader &reader,
              const std::vector<ColumnSchema> &schema,
              bool case_insensitive = false, bool or_logic = false);

void sort_indices(std::vector<size_t> &indices, const CsvReader &reader,
                  const std::vector<ColumnSchema> &schema,
                  const std::string &col_name, bool descending);

std::vector<size_t> resolve_columns(const std::string &select_str,
                                    const CsvReader &reader);
