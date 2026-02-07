#include "include/filter.hpp"
#include "include/csv_reader.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>

static std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ')
    s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ')
    s.remove_suffix(1);
  return s;
}

static std::string to_lower(std::string_view s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s)
    result += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  return result;
}

struct OpToken {
  std::string_view token;
  FilterOp op;
};

static constexpr OpToken op_tokens[] = {
    {">=", FilterOp::Gte}, {"<=", FilterOp::Lte}, {"!=", FilterOp::Neq},
    {"==", FilterOp::Eq},  {">", FilterOp::Gt},   {"<", FilterOp::Lt},
};

struct WordOpToken {
  std::string_view token;
  FilterOp op;
};

static constexpr WordOpToken word_op_tokens[] = {
    {"starts_with", FilterOp::StartsWith},
    {"ends_with", FilterOp::EndsWith},
    {"contains", FilterOp::Contains},
};

Filter parse_filter(std::string_view expr) {
  expr = trim(expr);
  if (expr.empty())
    throw std::runtime_error("Empty filter expression");

  // Normalize shell-escaped characters (zsh/bash escape ! to \!)
  std::string normalized;
  normalized.reserve(expr.size());
  for (size_t i = 0; i < expr.size(); ++i) {
    if (expr[i] == '\\' && i + 1 < expr.size() &&
        (expr[i + 1] == '!' || expr[i + 1] == '>' || expr[i + 1] == '<' ||
         expr[i + 1] == '=')) {
      normalized += expr[i + 1];
      ++i;
    } else {
      normalized += expr[i];
    }
  }
  std::string_view nexpr = normalized;

  for (auto &wop : word_op_tokens) {
    std::string search = std::string(" ") + std::string(wop.token) + " ";
    auto pos = nexpr.find(search);
    if (pos != std::string_view::npos) {
      auto col = trim(nexpr.substr(0, pos));
      auto val = trim(nexpr.substr(pos + search.size()));
      if (col.empty() || val.empty())
        throw std::runtime_error(
            "Invalid filter: column and value required around '" +
            std::string(wop.token) + "'");
      return {std::string(col), wop.op, std::string(val)};
    }
  }

  for (auto &op : op_tokens) {
    auto pos = nexpr.find(op.token);
    if (pos != std::string_view::npos) {
      auto col = trim(nexpr.substr(0, pos));
      auto val = trim(nexpr.substr(pos + op.token.size()));
      if (col.empty() || val.empty())
        throw std::runtime_error(
            "Invalid filter: column and value required around '" +
            std::string(op.token) + "'");
      return {std::string(col), op.op, std::string(val)};
    }
  }

  throw std::runtime_error("No valid operator found in filter: '" +
                           std::string(nexpr) + "'\n"
                           "Supported: ==, !=, >, <, >=, <=, contains, "
                           "starts_with, ends_with");
}

static double parse_numeric(const std::string &s) {
  std::string cleaned;
  cleaned.reserve(s.size());
  for (char c : s) {
    if (c == '$' || c == ',')
      continue;
    cleaned += c;
  }
  return std::stod(cleaned);
}

static bool is_numeric_type(ColumnType t) {
  return t == ColumnType::Int64 || t == ColumnType::Float64 ||
         t == ColumnType::Currency;
}

static bool compare_strings(const std::string &cell, FilterOp op,
                            const std::string &value, bool ci) {
  std::string a = ci ? to_lower(cell) : cell;
  std::string b = ci ? to_lower(value) : value;

  switch (op) {
  case FilterOp::Eq:
    return a == b;
  case FilterOp::Neq:
    return a != b;
  case FilterOp::Gt:
    return a > b;
  case FilterOp::Lt:
    return a < b;
  case FilterOp::Gte:
    return a >= b;
  case FilterOp::Lte:
    return a <= b;
  case FilterOp::Contains:
    return a.find(b) != std::string::npos;
  case FilterOp::StartsWith:
    return a.size() >= b.size() && a.compare(0, b.size(), b) == 0;
  case FilterOp::EndsWith:
    return a.size() >= b.size() &&
           a.compare(a.size() - b.size(), b.size(), b) == 0;
  }
  return false;
}

static bool compare_numeric(double cell_val, FilterOp op, double filter_val) {
  switch (op) {
  case FilterOp::Eq:
    return cell_val == filter_val;
  case FilterOp::Neq:
    return cell_val != filter_val;
  case FilterOp::Gt:
    return cell_val > filter_val;
  case FilterOp::Lt:
    return cell_val < filter_val;
  case FilterOp::Gte:
    return cell_val >= filter_val;
  case FilterOp::Lte:
    return cell_val <= filter_val;
  case FilterOp::Contains:
  case FilterOp::StartsWith:
  case FilterOp::EndsWith:
    return false;
  }
  return false;
}

static bool row_matches(std::span<const std::string_view> row,
                        const Filter &filter, size_t col_idx,
                        ColumnType col_type, bool ci) {
  if (col_idx >= row.size())
    return false;

  std::string cell_str = unquote(row[col_idx]);

  if (is_numeric_type(col_type) && filter.op != FilterOp::Contains &&
      filter.op != FilterOp::StartsWith &&
      filter.op != FilterOp::EndsWith) {
    try {
      double cell_val = parse_numeric(cell_str);
      double filter_val = parse_numeric(filter.value);
      return compare_numeric(cell_val, filter.op, filter_val);
    } catch (...) {
    }
  }

  return compare_strings(cell_str, filter.op, filter.value, ci);
}

std::vector<size_t>
apply_filters(const std::vector<Filter> &filters, const CsvReader &reader,
              const std::vector<ColumnSchema> &schema, bool case_insensitive,
              bool or_logic) {
  struct ResolvedFilter {
    const Filter *filter;
    size_t col_idx;
    ColumnType col_type;
  };
  std::vector<ResolvedFilter> resolved;
  resolved.reserve(filters.size());

  auto &headers = reader.headers();
  for (auto &f : filters) {
    bool found = false;
    std::string f_col = case_insensitive ? to_lower(f.column) : f.column;
    for (size_t i = 0; i < headers.size(); ++i) {
      std::string hdr_name = unquote(headers[i]);
      if (case_insensitive)
        hdr_name = to_lower(hdr_name);
      if (hdr_name == f_col) {
        ColumnType ct =
            (i < schema.size()) ? schema[i].type : ColumnType::Text;
        resolved.push_back({&f, i, ct});
        found = true;
        break;
      }
    }
    if (!found) {
      throw std::runtime_error("Column '" + f.column +
                               "' not found. Available columns: " +
                               [&]() {
                                 std::string cols;
                                 for (size_t i = 0; i < headers.size(); ++i) {
                                   if (i > 0)
                                     cols += ", ";
                                   cols += unquote(headers[i]);
                                 }
                                 return cols;
                               }());
    }
  }

  std::vector<size_t> result;
  for (size_t r = 0; r < reader.row_count(); ++r) {
    auto row = reader.row(r);
    bool match;
    if (or_logic) {
      match = false;
      for (auto &rf : resolved) {
        if (row_matches(row, *rf.filter, rf.col_idx, rf.col_type,
                        case_insensitive)) {
          match = true;
          break;
        }
      }
    } else {
      match = true;
      for (auto &rf : resolved) {
        if (!row_matches(row, *rf.filter, rf.col_idx, rf.col_type,
                         case_insensitive)) {
          match = false;
          break;
        }
      }
    }
    if (match)
      result.push_back(r);
  }

  return result;
}

void sort_indices(std::vector<size_t> &indices, const CsvReader &reader,
                  const std::vector<ColumnSchema> &schema,
                  const std::string &col_name, bool descending) {
  auto &headers = reader.headers();
  size_t col_idx = SIZE_MAX;
  ColumnType col_type = ColumnType::Text;

  for (size_t i = 0; i < headers.size(); ++i) {
    if (unquote(headers[i]) == col_name) {
      col_idx = i;
      if (i < schema.size())
        col_type = schema[i].type;
      break;
    }
  }
  if (col_idx == SIZE_MAX) {
    throw std::runtime_error("Sort column '" + col_name +
                             "' not found. Available columns: " +
                             [&]() {
                               std::string cols;
                               for (size_t i = 0; i < headers.size(); ++i) {
                                 if (i > 0)
                                   cols += ", ";
                                 cols += unquote(headers[i]);
                               }
                               return cols;
                             }());
  }

  bool numeric = is_numeric_type(col_type);

  std::stable_sort(indices.begin(), indices.end(),
                   [&](size_t a, size_t b) -> bool {
                     auto row_a = reader.row(a);
                     auto row_b = reader.row(b);
                     std::string va =
                         (col_idx < row_a.size()) ? unquote(row_a[col_idx]) : "";
                     std::string vb =
                         (col_idx < row_b.size()) ? unquote(row_b[col_idx]) : "";

                     if (numeric) {
                       try {
                         double da = parse_numeric(va);
                         double db = parse_numeric(vb);
                         return descending ? da > db : da < db;
                       } catch (...) {
                       }
                     }
                     return descending ? va > vb : va < vb;
                   });
}

std::vector<size_t> resolve_columns(const std::string &select_str,
                                    const CsvReader &reader) {
  std::vector<size_t> indices;
  auto &headers = reader.headers();

  std::istringstream ss(select_str);
  std::string token;
  while (std::getline(ss, token, ',')) {
    // Trim whitespace
    size_t start = token.find_first_not_of(' ');
    size_t end = token.find_last_not_of(' ');
    if (start == std::string::npos)
      continue;
    token = token.substr(start, end - start + 1);

    bool found = false;
    for (size_t i = 0; i < headers.size(); ++i) {
      if (unquote(headers[i]) == token) {
        indices.push_back(i);
        found = true;
        break;
      }
    }
    if (!found) {
      throw std::runtime_error("Column '" + token +
                               "' not found. Available columns: " +
                               [&]() {
                                 std::string cols;
                                 for (size_t i = 0; i < headers.size(); ++i) {
                                   if (i > 0)
                                     cols += ", ";
                                   cols += unquote(headers[i]);
                                 }
                                 return cols;
                               }());
    }
  }

  if (indices.empty())
    throw std::runtime_error("No valid columns in --select");

  return indices;
}
