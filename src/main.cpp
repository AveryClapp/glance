#include "include/csv_reader.hpp"
#include "include/delim.hpp"
#include "include/filter.hpp"
#include "include/pager.hpp"
#include "include/tui.hpp"
#include "include/type_inference.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <unistd.h>
#include <vector>

static void print_usage() {
  std::cerr
      << "Usage: glance [file.csv | -] [options]\n"
      << "\n"
      << "Options:\n"
      << "  -n, --head <N>           Show first N rows (default: 50)\n"
      << "  -t, --tail <N>           Show last N rows\n"
      << "  -s, --schema             Output inferred schema as JSON\n"
      << "  -w, --where <expr>       Filter rows (repeatable)\n"
      << "  -i, --ignore-case        Case-insensitive filtering\n"
      << "  --logic <and|or>         Filter logic (default: and)\n"
      << "  --select <col1,col2,...> Show only specified columns\n"
      << "  --sort <col>             Sort by column (ascending)\n"
      << "  --sort-desc <col>        Sort by column (descending)\n"
      << "  --count                  Output only the count of matching rows\n"
      << "  --format <fmt>           Output format: table, csv, tsv, json\n"
      << "  --no-pager               Disable interactive pager\n"
      << "  -h, --help               Show this help\n"
      << "\n"
      << "Filter operators: ==, !=, >, <, >=, <=, contains, starts_with, "
         "ends_with\n"
      << "Example: glance data.csv --where \"age > 30\" --where \"name "
         "contains Al\"\n"
      << "Stdin:   cat data.csv | glance - --format json\n";
}

enum class OutputFormat { Table, Csv, Tsv, Json };

int main(int argc, char *argv[]) {
  std::string input_path;
  int head_count = -1; // -1 = not specified
  int tail_count = -1;
  bool schema_mode = false;
  bool count_mode = false;
  bool no_pager = false;
  bool ignore_case = false;
  bool or_logic = false;
  OutputFormat format = OutputFormat::Table;
  std::string select_str;
  std::string sort_col;
  bool sort_desc = false;
  std::vector<std::string> where_exprs;

  for (int i = 1; i < argc; ++i) {
    if ((std::strcmp(argv[i], "-n") == 0 ||
         std::strcmp(argv[i], "--head") == 0) &&
        i + 1 < argc) {
      head_count = std::atoi(argv[++i]);
    } else if ((std::strcmp(argv[i], "-t") == 0 ||
                std::strcmp(argv[i], "--tail") == 0) &&
               i + 1 < argc) {
      tail_count = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "-s") == 0 ||
               std::strcmp(argv[i], "--schema") == 0) {
      schema_mode = true;
    } else if ((std::strcmp(argv[i], "-w") == 0 ||
                std::strcmp(argv[i], "--where") == 0) &&
               i + 1 < argc) {
      where_exprs.push_back(argv[++i]);
    } else if (std::strcmp(argv[i], "-i") == 0 ||
               std::strcmp(argv[i], "--ignore-case") == 0) {
      ignore_case = true;
    } else if (std::strcmp(argv[i], "--logic") == 0 && i + 1 < argc) {
      ++i;
      if (std::strcmp(argv[i], "or") == 0)
        or_logic = true;
      else if (std::strcmp(argv[i], "and") != 0) {
        std::cerr << "Unknown logic: " << argv[i] << " (use 'and' or 'or')\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--select") == 0 && i + 1 < argc) {
      select_str = argv[++i];
    } else if (std::strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
      sort_col = argv[++i];
      sort_desc = false;
    } else if (std::strcmp(argv[i], "--sort-desc") == 0 && i + 1 < argc) {
      sort_col = argv[++i];
      sort_desc = true;
    } else if (std::strcmp(argv[i], "--count") == 0) {
      count_mode = true;
    } else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
      ++i;
      if (std::strcmp(argv[i], "csv") == 0)
        format = OutputFormat::Csv;
      else if (std::strcmp(argv[i], "tsv") == 0)
        format = OutputFormat::Tsv;
      else if (std::strcmp(argv[i], "json") == 0)
        format = OutputFormat::Json;
      else if (std::strcmp(argv[i], "table") != 0) {
        std::cerr << "Unknown format: " << argv[i]
                  << " (use table, csv, tsv, json)\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--no-pager") == 0) {
      no_pager = true;
    } else if (std::strcmp(argv[i], "-h") == 0 ||
               std::strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 0;
    } else if (argv[i][0] != '-' || std::strcmp(argv[i], "-") == 0) {
      input_path = argv[i];
    } else {
      std::cerr << "Unknown option: " << argv[i] << "\n";
      print_usage();
      return 1;
    }
  }

  // Handle stdin: if no path and stdin is piped, read from stdin
  if (input_path.empty()) {
    if (!isatty(STDIN_FILENO))
      input_path = "-";
    else {
      print_usage();
      return 1;
    }
  }

  // Validate mutually exclusive options
  if (head_count >= 0 && tail_count >= 0) {
    std::cerr << "Error: --head and --tail are mutually exclusive\n";
    return 1;
  }

  try {
    CsvReader reader(input_path.c_str());
    char delim = detect_delimiter(reader.data(), reader.size());

    // Determine if we need interactive pager
    bool stdout_is_tty = isatty(STDOUT_FILENO);
    bool interactive =
        stdout_is_tty && !schema_mode && !count_mode &&
        format == OutputFormat::Table && !no_pager;

    // Determine parse mode: full parse needed for filters, sort, tail, or
    // interactive pager
    bool needs_full = interactive || !where_exprs.empty() ||
                      !sort_col.empty() || tail_count >= 0;

    if (needs_full) {
      reader.parse(delim);
    } else {
      size_t limit = (head_count >= 0) ? static_cast<size_t>(head_count) : 50;
      size_t parse_count = std::max(limit, static_cast<size_t>(100));
      reader.parse_head(delim, parse_count);
    }

    if (reader.column_count() == 0) {
      std::cerr << "Error: no columns found in file\n";
      return 1;
    }

    auto schema = infer_schema(reader);

    // Resolve column selection
    std::vector<size_t> col_indices;
    const std::vector<size_t> *col_ptr = nullptr;
    if (!select_str.empty()) {
      col_indices = resolve_columns(select_str, reader);
      col_ptr = &col_indices;
    }

    // Apply filters
    std::vector<size_t> filtered;
    const std::vector<size_t> *row_ptr = nullptr;
    size_t match_count = reader.total_rows();

    if (!where_exprs.empty()) {
      std::vector<Filter> filters;
      filters.reserve(where_exprs.size());
      for (auto &expr : where_exprs)
        filters.push_back(parse_filter(expr));
      filtered =
          apply_filters(filters, reader, schema, ignore_case, or_logic);
      row_ptr = &filtered;
      match_count = filtered.size();
    }

    // Apply sorting
    if (!sort_col.empty()) {
      if (!row_ptr) {
        filtered.resize(reader.row_count());
        std::iota(filtered.begin(), filtered.end(), static_cast<size_t>(0));
        row_ptr = &filtered;
      }
      sort_indices(filtered, reader, schema, sort_col, sort_desc);
    }

    // Apply tail (take last N)
    if (tail_count >= 0) {
      size_t total = row_ptr ? row_ptr->size() : reader.row_count();
      size_t n = std::min(static_cast<size_t>(tail_count), total);
      if (!row_ptr) {
        filtered.resize(reader.row_count());
        std::iota(filtered.begin(), filtered.end(), static_cast<size_t>(0));
        row_ptr = &filtered;
      }
      if (filtered.size() > n)
        filtered.erase(filtered.begin(),
                       filtered.begin() +
                           static_cast<ptrdiff_t>(filtered.size() - n));
      match_count = filtered.size();
    }

    // Determine max rows to display
    size_t display_total = row_ptr ? row_ptr->size() : reader.row_count();
    size_t max_rows;
    if (head_count >= 0)
      max_rows = static_cast<size_t>(head_count);
    else if (tail_count >= 0)
      max_rows = display_total; // tail already limited
    else if (interactive)
      max_rows = display_total; // pager shows all
    else
      max_rows = 50; // non-interactive default

    // --- Output ---

    if (count_mode) {
      std::cout << match_count << "\n";
    } else if (schema_mode) {
      render_schema_json(schema, col_ptr, match_count, reader.size());
    } else if (format == OutputFormat::Csv) {
      render_csv(reader, row_ptr, col_ptr, max_rows, ',');
    } else if (format == OutputFormat::Tsv) {
      render_csv(reader, row_ptr, col_ptr, max_rows, '\t');
    } else if (format == OutputFormat::Json) {
      render_json(reader, schema, row_ptr, col_ptr, max_rows);
    } else {
      // Table mode — decide pager vs dump
      auto [term_h, term_w] = get_terminal_size();
      size_t rows_to_show = std::min(display_total, max_rows);
      // Pager if interactive and rows exceed viewport (header=2 + types + sep +
      // footer + border = ~6 overhead)
      bool should_page = interactive && rows_to_show > (term_h - 6);

      if (should_page) {
        // For pager: if head limiting, create truncated index list
        if (head_count >= 0 && display_total > max_rows) {
          if (!row_ptr) {
            filtered.resize(max_rows);
            std::iota(filtered.begin(), filtered.end(),
                      static_cast<size_t>(0));
          } else {
            filtered.resize(max_rows);
          }
          row_ptr = &filtered;
        }
        run_pager(reader, schema, row_ptr, col_ptr, match_count);
      } else {
        render_table(reader, schema, row_ptr, col_ptr, max_rows, match_count);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
