# glance

> Fast CSV inspection for the terminal.

## Design

`glance` uses SIMD-accelerated parsing and memory-mapped I/O to provide instant access to large CSV files. Type inference and intelligent formatting make data immediately readable.

### Architecture

**Parser**: AVX2 intrinsics for delimiter detection, stateful quote handling  
**Memory**: mmap for zero-copy file access, lazy evaluation  
**Analysis**: Single-pass type inference on sampled rows  
**Render**: Terminal-width aware formatting with type-based styling

### Technical approach

- SIMD vectorization finds delimiters in 32-byte chunks
- Memory mapping eliminates buffer copies
- Streaming parser loads only visible rows
- Type inference samples first N rows (configurable)
- Smart delimiter detection via consistency scoring

## Usage

```bash
glance data.csv                    # Basic view
glance data.csv --head 100         # First 100 rows
glance data.csv --stats            # Column statistics
glance data.csv --schema           # Inferred schema as JSON
```

## Example output

```
┌──────────────┬─────────────┬──────────────┬────────────┐
│ Date         │ Product     │ Quantity     │ Revenue    │
│ date         │ enum        │ int64        │ float64    │
├──────────────┼─────────────┼──────────────┼────────────┤
│ 2024-01-15   │ Widget-A    │ 142          │ 1420.00    │
│ 2024-01-15   │ Widget-B    │ 89           │ 2670.00    │
│ 2024-01-16   │ Widget-A    │ 201          │ 2010.00    │
└──────────────┴─────────────┴──────────────┴────────────┘

1.2M rows, 4 columns, 87MB
```

## Type system

Inferred types from most to least specific:

- `int64`: Numeric values within 64-bit integer range
- `float64`: Numeric values requiring floating point
- `date`: ISO 8601 or common date formats
- `currency`: Numeric with currency symbols
- `bool`: true/false, yes/no, 0/1
- `enum`: Low cardinality categorical (< 10% unique values)
- `text`: Everything else

## Implementation

**Language**: C++20  
**SIMD**: AVX2 (AVX-512 on supported CPUs)  
**Platform**: Linux/macOS (POSIX mmap)  
**Build**: CMake, requires AVX2 support

## Performance targets

- Sub-100ms first render for files under 100MB
- Constant memory usage regardless of file size
- Linear scaling with row count for analysis passes

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/glance test.csv
```

Requires: C++20 compiler with AVX2 support, CMake 3.20+

---

