# glance

> Fast CSV inspection for the terminal.

Instant schema inference, SQL-like filtering, interactive scrolling, and multiple output formats. Handles multi-million row files in under a second.

## Install

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build --prefix /usr/local
```

Requires: C++20 compiler, CMake 3.20+, macOS or Linux.

## Usage

```bash
glance data.csv                          # table with interactive pager
glance data.csv -n 20                    # first 20 rows
glance data.csv -t 10                    # last 10 rows
glance data.csv --schema                 # inferred types as JSON
glance data.csv --count                  # row count

# Filtering
glance data.csv --where "age > 30"
glance data.csv --where "name contains Al" --where "active == true"
glance data.csv --where "dept == Eng" --where "dept == Sales" --logic or
glance data.csv --where "status == active" -i   # case-insensitive

# Sorting
glance data.csv --sort age
glance data.csv --sort-desc salary

# Column selection
glance data.csv --select "name,age,salary"

# Output formats
glance data.csv --format csv
glance data.csv --format tsv
glance data.csv --format json

# Piping
cat data.csv | glance -
cat data.csv | glance --format json > out.json
glance data.csv --where "age > 30" --format csv > filtered.csv
```

## Example

```
$ glance employees.csv --where "salary > 90000" --sort-desc salary --select "name,salary,department"
┌─────────┬────────────┬─────────────┐
│ name    │ salary     │ department  │
│ text    │ currency   │ text        │
├─────────┼────────────┼─────────────┤
│ Frank   │ $120000.00 │ Management  │
│ Hank    │ $105000.00 │ Sales       │
│ Charlie │ $95000.00  │ Engineering │
│ Eve     │ $91000.75  │ Engineering │
└─────────┴────────────┴─────────────┘
4 rows | 3 cols | 496 B
```

## Interactive Pager

When output exceeds your terminal height, glance launches an interactive pager automatically (like `less`). Disable with `--no-pager`.

| Key | Action |
|---|---|
| `j` / `k` / arrows | Scroll up/down |
| `Space` / `b` | Page down/up |
| `g` / `G` | Jump to top/bottom |
| `h` / `l` / left/right | Scroll columns |
| `/` | Search |
| `n` / `N` | Next/previous match |
| `q` | Quit |

Works with piped input too: `cat huge.csv | glance -` opens the pager reading keys from `/dev/tty`.

## Type Inference

Types are inferred from the first 100 rows, ordered by specificity:

| Type | Examples |
|---|---|
| `bool` | true, false, yes, no, 0, 1 |
| `currency` | $12.99, $1,200.00 |
| `date` | 2024-01-15, 01/15/2024 |
| `int64` | 42, -7, +100 |
| `float64` | 3.14, -0.5, 1e10 |
| `enum` | Low cardinality (< 10% unique) |
| `text` | Everything else |

Numeric types enable proper numeric sorting and comparison in filters.

## Delimiter Detection

Auto-detects: comma, tab, pipe, semicolon. Scores each candidate by count consistency across sampled lines (mean / (1 + stddev)). No flag needed.

## Performance

Benchmarked on 5M rows (307 MB), Apple Silicon, Release build:

| Operation | Time |
|---|---|
| Display + schema (lazy) | 0.09s |
| Full filter + sort | 0.88s |

Key techniques:
- **mmap** for zero-copy file access
- **Lazy parsing**: only parse rows needed for display, NEON-count the rest
- **Flat storage**: single `vector<string_view>` with stride, no per-row allocations
- **ARM NEON SIMD** for newline counting (~20 GB/s)
- **memchr fast-path** for line-end detection

## Options

```
Usage: glance [file.csv | -] [options]

  -n, --head <N>           Show first N rows (default: 50)
  -t, --tail <N>           Show last N rows
  -s, --schema             Output inferred schema as JSON
  -w, --where <expr>       Filter rows (repeatable)
  -i, --ignore-case        Case-insensitive filtering
  --logic <and|or>         Filter logic (default: and)
  --select <col1,col2,...> Show only specified columns
  --sort <col>             Sort by column (ascending)
  --sort-desc <col>        Sort by column (descending)
  --count                  Output only the count of matching rows
  --format <fmt>           Output format: table, csv, tsv, json
  --no-pager               Disable interactive pager
  -h, --help               Show this help

Filter operators: ==, !=, >, <, >=, <=, contains, starts_with, ends_with
```
