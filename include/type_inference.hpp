#pragma once

#include <string>
#include <string_view>
#include <vector>

class CsvReader;

enum class ColumnType { Int64, Float64, Date, Currency, Bool, Enum, Text };

struct ColumnSchema {
  std::string name;
  ColumnType type;
};

std::string_view type_name(ColumnType t);

std::vector<ColumnSchema> infer_schema(const CsvReader &reader,
                                       size_t sample_size = 100);
