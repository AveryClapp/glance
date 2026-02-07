#include "include/tui.hpp"
#include "include/csv_reader.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

std::pair<size_t, size_t> get_terminal_size() {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
    return {w.ws_row, w.ws_col};
  return {24, 80};
}

static std::string format_size(size_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double val = static_cast<double>(bytes);
  int idx = 0;
  while (val >= 1024.0 && idx < 4) {
    val /= 1024.0;
    ++idx;
  }
  std::ostringstream oss;
  if (idx == 0)
    oss << bytes << " B";
  else
    oss << std::fixed << std::setprecision(1) << val << " " << units[idx];
  return oss.str();
}

static std::string format_count(size_t count) {
  if (count >= 1000000) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << (static_cast<double>(count) / 1000000.0) << "M";
    return oss.str();
  }
  if (count >= 1000) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << (static_cast<double>(count) / 1000.0) << "K";
    return oss.str();
  }
  return std::to_string(count);
}

static std::string truncate(std::string_view s, size_t max_w) {
  if (s.size() <= max_w)
    return std::string(s);
  if (max_w <= 3)
    return std::string(max_w, '.');
  return std::string(s.substr(0, max_w - 3)) + "...";
}

void render_table(const CsvReader &reader,
                  const std::vector<ColumnSchema> &schema,
                  const std::vector<size_t> *row_indices,
                  const std::vector<size_t> *col_indices, size_t max_rows,
                  size_t total_match_count) {
  auto &headers = reader.headers();
  auto [term_h, term_w] = get_terminal_size();

  // Build display column list
  std::vector<size_t> display_cols;
  if (col_indices) {
    display_cols = *col_indices;
  } else {
    display_cols.resize(reader.column_count());
    for (size_t i = 0; i < display_cols.size(); ++i)
      display_cols[i] = i;
  }
  size_t ncols = display_cols.size();

  // Row accessor
  size_t available_rows =
      row_indices ? row_indices->size() : reader.row_count();
  size_t nrows = std::min(available_rows, max_rows);

  auto get_row = [&](size_t display_idx) {
    size_t actual =
        row_indices ? (*row_indices)[display_idx] : display_idx;
    return reader.row(actual);
  };

  // Compute column widths
  std::vector<size_t> col_widths(ncols, 0);
  for (size_t c = 0; c < ncols; ++c) {
    size_t ac = display_cols[c];
    std::string hdr = unquote(headers[ac]);
    col_widths[c] = std::max(col_widths[c], hdr.size());
    if (ac < schema.size())
      col_widths[c] =
          std::max(col_widths[c], type_name(schema[ac].type).size());
  }
  for (size_t r = 0; r < nrows; ++r) {
    auto row = get_row(r);
    for (size_t c = 0; c < ncols; ++c) {
      size_t ac = display_cols[c];
      if (ac < row.size()) {
        std::string val = unquote(row[ac]);
        col_widths[c] = std::max(col_widths[c], val.size());
      }
    }
  }

  // Cap column widths to fit terminal
  size_t total_padding = ncols * 3 + 1;
  if (total_padding < term_w) {
    size_t available = term_w - total_padding;
    size_t total_content = 0;
    for (auto w : col_widths)
      total_content += w;
    if (total_content > available) {
      size_t max_per_col =
          std::max(static_cast<size_t>(5), available / ncols);
      for (auto &w : col_widths)
        w = std::min(w, max_per_col);
    }
  }

  auto hline = [&](const char *left, const char *mid, const char *right) {
    std::cout << left;
    for (size_t c = 0; c < ncols; ++c) {
      for (size_t i = 0; i < col_widths[c] + 2; ++i)
        std::cout << "\u2500";
      if (c + 1 < ncols)
        std::cout << mid;
    }
    std::cout << right << "\n";
  };

  auto print_row = [&](auto get_val) {
    std::cout << "\u2502";
    for (size_t c = 0; c < ncols; ++c) {
      std::string val = get_val(c);
      std::string display = truncate(val, col_widths[c]);
      size_t pad = col_widths[c] - display.size();
      std::cout << " " << display << std::string(pad, ' ') << " \u2502";
    }
    std::cout << "\n";
  };

  hline("\u250C", "\u252C", "\u2510");

  print_row([&](size_t c) { return unquote(headers[display_cols[c]]); });

  print_row([&](size_t c) {
    size_t ac = display_cols[c];
    return std::string(
        (ac < schema.size()) ? type_name(schema[ac].type) : "text");
  });

  hline("\u251C", "\u253C", "\u2524");

  for (size_t r = 0; r < nrows; ++r) {
    auto row = get_row(r);
    print_row([&](size_t c) {
      size_t ac = display_cols[c];
      if (ac < row.size())
        return unquote(row[ac]);
      return std::string();
    });
  }

  hline("\u2514", "\u2534", "\u2518");

  std::cout << format_count(total_match_count) << " rows";
  if (nrows < total_match_count)
    std::cout << " (showing " << nrows << ")";
  std::cout << " | " << ncols << " cols | " << format_size(reader.size())
            << "\n";
}

void render_schema_json(const std::vector<ColumnSchema> &schema,
                        const std::vector<size_t> *col_indices,
                        size_t row_count, size_t file_size) {
  std::vector<size_t> cols;
  if (col_indices) {
    cols = *col_indices;
  } else {
    cols.resize(schema.size());
    for (size_t i = 0; i < cols.size(); ++i)
      cols[i] = i;
  }

  std::cout << "{\n";
  std::cout << "  \"row_count\": " << row_count << ",\n";
  std::cout << "  \"file_size\": " << file_size << ",\n";
  std::cout << "  \"columns\": [\n";
  for (size_t i = 0; i < cols.size(); ++i) {
    size_t ac = cols[i];
    std::cout << "    {\"name\": \"" << schema[ac].name << "\", \"type\": \""
              << type_name(schema[ac].type) << "\"}";
    if (i + 1 < cols.size())
      std::cout << ",";
    std::cout << "\n";
  }
  std::cout << "  ]\n";
  std::cout << "}\n";
}

// --- CSV/TSV output ---

static std::string csv_escape(const std::string &val, char delim) {
  bool needs_quote = false;
  for (char c : val) {
    if (c == delim || c == '"' || c == '\n' || c == '\r') {
      needs_quote = true;
      break;
    }
  }
  if (!needs_quote)
    return val;

  std::string result = "\"";
  for (char c : val) {
    if (c == '"')
      result += "\"\"";
    else
      result += c;
  }
  result += '"';
  return result;
}

void render_csv(const CsvReader &reader,
                const std::vector<size_t> *row_indices,
                const std::vector<size_t> *col_indices, size_t max_rows,
                char delimiter) {
  auto &headers = reader.headers();

  std::vector<size_t> cols;
  if (col_indices) {
    cols = *col_indices;
  } else {
    cols.resize(reader.column_count());
    for (size_t i = 0; i < cols.size(); ++i)
      cols[i] = i;
  }

  // Header
  for (size_t i = 0; i < cols.size(); ++i) {
    if (i > 0)
      std::cout << delimiter;
    std::string hdr = unquote(headers[cols[i]]);
    std::cout << csv_escape(hdr, delimiter);
  }
  std::cout << "\n";

  // Rows
  size_t available = row_indices ? row_indices->size() : reader.row_count();
  size_t nrows = std::min(available, max_rows);

  for (size_t r = 0; r < nrows; ++r) {
    size_t actual = row_indices ? (*row_indices)[r] : r;
    auto row = reader.row(actual);
    for (size_t i = 0; i < cols.size(); ++i) {
      if (i > 0)
        std::cout << delimiter;
      size_t ac = cols[i];
      std::string val = (ac < row.size()) ? unquote(row[ac]) : "";
      std::cout << csv_escape(val, delimiter);
    }
    std::cout << "\n";
  }
}

// --- JSON output ---

static std::string json_escape(const std::string &val) {
  std::string result;
  result.reserve(val.size());
  for (char c : val) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
        result += buf;
      } else {
        result += c;
      }
    }
  }
  return result;
}

void render_json(const CsvReader &reader,
                 const std::vector<ColumnSchema> &schema,
                 const std::vector<size_t> *row_indices,
                 const std::vector<size_t> *col_indices, size_t max_rows) {
  auto &headers = reader.headers();

  std::vector<size_t> cols;
  if (col_indices) {
    cols = *col_indices;
  } else {
    cols.resize(reader.column_count());
    for (size_t i = 0; i < cols.size(); ++i)
      cols[i] = i;
  }

  size_t available = row_indices ? row_indices->size() : reader.row_count();
  size_t nrows = std::min(available, max_rows);

  std::cout << "[\n";
  for (size_t r = 0; r < nrows; ++r) {
    size_t actual = row_indices ? (*row_indices)[r] : r;
    auto row = reader.row(actual);

    std::cout << "  {";
    for (size_t i = 0; i < cols.size(); ++i) {
      size_t ac = cols[i];
      std::string col_name = unquote(headers[ac]);
      std::string val = (ac < row.size()) ? unquote(row[ac]) : "";

      if (i > 0)
        std::cout << ", ";
      std::cout << "\"" << json_escape(col_name) << "\": ";

      // Type-aware value encoding
      ColumnType ct =
          (ac < schema.size()) ? schema[ac].type : ColumnType::Text;
      if (val.empty()) {
        std::cout << "null";
      } else if (ct == ColumnType::Bool) {
        std::string lower;
        for (char c : val)
          lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        bool b = (lower == "true" || lower == "yes" || lower == "1");
        std::cout << (b ? "true" : "false");
      } else if (ct == ColumnType::Int64) {
        std::cout << val;
      } else if (ct == ColumnType::Float64) {
        std::cout << val;
      } else {
        std::cout << "\"" << json_escape(val) << "\"";
      }
    }
    std::cout << "}";
    if (r + 1 < nrows)
      std::cout << ",";
    std::cout << "\n";
  }
  std::cout << "]\n";
}
