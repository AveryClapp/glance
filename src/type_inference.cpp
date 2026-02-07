#include "include/type_inference.hpp"
#include "include/csv_reader.hpp"
#include <algorithm>
#include <set>
#include <string>

std::string_view type_name(ColumnType t) {
  switch (t) {
  case ColumnType::Int64:
    return "int64";
  case ColumnType::Float64:
    return "float64";
  case ColumnType::Date:
    return "date";
  case ColumnType::Currency:
    return "currency";
  case ColumnType::Bool:
    return "bool";
  case ColumnType::Enum:
    return "enum";
  case ColumnType::Text:
    return "text";
  }
  return "text";
}

static bool is_int64(std::string_view s) {
  if (s.empty())
    return false;
  size_t i = 0;
  if (s[0] == '-' || s[0] == '+')
    ++i;
  if (i == s.size())
    return false;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9')
      return false;
  }
  return true;
}

static bool is_float64(std::string_view s) {
  if (s.empty())
    return false;
  size_t i = 0;
  if (s[0] == '-' || s[0] == '+')
    ++i;
  if (i == s.size())
    return false;
  bool has_dot = false;
  bool has_digit = false;
  for (; i < s.size(); ++i) {
    if (s[i] == '.') {
      if (has_dot)
        return false;
      has_dot = true;
    } else if (s[i] >= '0' && s[i] <= '9') {
      has_digit = true;
    } else if (s[i] == 'e' || s[i] == 'E') {
      if (!has_digit)
        return false;
      ++i;
      if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        ++i;
      if (i == s.size())
        return false;
      for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9')
          return false;
      }
      return true;
    } else {
      return false;
    }
  }
  return has_digit && has_dot;
}

static bool is_date(std::string_view s) {
  if (s.size() < 8 || s.size() > 10)
    return false;
  if (s.size() == 10 && (s[4] == '-' || s[4] == '/') &&
      (s[7] == '-' || s[7] == '/')) {
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
      if (s[i] < '0' || s[i] > '9')
        return false;
    }
    return true;
  }
  if (s.size() == 10 && (s[2] == '/' || s[2] == '-') &&
      (s[5] == '/' || s[5] == '-')) {
    for (int i : {0, 1, 3, 4, 6, 7, 8, 9}) {
      if (s[i] < '0' || s[i] > '9')
        return false;
    }
    return true;
  }
  return false;
}

static bool is_currency(std::string_view s) {
  if (s.size() < 2)
    return false;
  size_t i = 0;
  if (s[0] == '$' || s[0] == '\xc2') {
    i = 1;
    if (i < s.size() && (s[i] == '\xa3' || s[i] == '\xa5'))
      ++i;
  }
  if (i == s.size())
    return false;
  if (s[i] == '-' || s[i] == '+')
    ++i;
  bool has_digit = false;
  bool has_dot = false;
  for (; i < s.size(); ++i) {
    if (s[i] >= '0' && s[i] <= '9') {
      has_digit = true;
    } else if (s[i] == ',') {
    } else if (s[i] == '.') {
      if (has_dot)
        return false;
      has_dot = true;
    } else {
      return false;
    }
  }
  return has_digit && s[0] == '$';
}

static bool is_bool(std::string_view s) {
  if (s.size() < 1 || s.size() > 5)
    return false;
  std::string lower;
  lower.reserve(s.size());
  for (char c : s)
    lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  return lower == "true" || lower == "false" || lower == "yes" ||
         lower == "no" || lower == "1" || lower == "0";
}

std::vector<ColumnSchema> infer_schema(const CsvReader &reader,
                                       size_t sample_size) {
  std::vector<ColumnSchema> schema;
  size_t ncols = reader.column_count();
  size_t nrows = std::min(reader.row_count(), sample_size);

  for (size_t col = 0; col < ncols; ++col) {
    std::string col_name = unquote(reader.headers()[col]);

    if (nrows == 0) {
      schema.push_back({std::move(col_name), ColumnType::Text});
      continue;
    }

    std::vector<std::string_view> values;
    std::set<std::string> unique_values;
    for (size_t r = 0; r < nrows; ++r) {
      auto row = reader.row(r);
      if (col < row.size()) {
        std::string val = unquote(row[col]);
        if (!val.empty()) {
          values.push_back(row[col]);
          unique_values.insert(val);
        }
      }
    }

    if (values.empty()) {
      schema.push_back({std::move(col_name), ColumnType::Text});
      continue;
    }

    auto all_match = [&](auto pred) {
      for (auto &v : values) {
        std::string uv = unquote(v);
        if (!pred(std::string_view(uv)))
          return false;
      }
      return true;
    };

    ColumnType detected = ColumnType::Text;
    if (all_match(is_bool)) {
      detected = ColumnType::Bool;
    } else if (all_match(is_currency)) {
      detected = ColumnType::Currency;
    } else if (all_match(is_date)) {
      detected = ColumnType::Date;
    } else if (all_match(is_int64)) {
      detected = ColumnType::Int64;
    } else if (all_match(is_float64)) {
      detected = ColumnType::Float64;
    } else if (unique_values.size() <
               std::max(static_cast<size_t>(2), values.size() / 10)) {
      detected = ColumnType::Enum;
    }

    schema.push_back({std::move(col_name), detected});
  }

  return schema;
}
