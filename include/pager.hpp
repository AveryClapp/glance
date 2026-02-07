#pragma once

#include "csv_reader.hpp"
#include "type_inference.hpp"
#include <cstddef>
#include <vector>

void run_pager(const CsvReader &reader,
               const std::vector<ColumnSchema> &schema,
               const std::vector<size_t> *row_indices,
               const std::vector<size_t> *col_indices,
               size_t total_match_count);
