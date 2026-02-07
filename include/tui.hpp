#pragma once

#include "type_inference.hpp"
#include <cstddef>
#include <vector>

class CsvReader;

std::pair<size_t, size_t> get_terminal_size();

void render_table(const CsvReader &reader,
                  const std::vector<ColumnSchema> &schema,
                  const std::vector<size_t> *row_indices,
                  const std::vector<size_t> *col_indices, size_t max_rows,
                  size_t total_match_count);

void render_schema_json(const std::vector<ColumnSchema> &schema,
                        const std::vector<size_t> *col_indices,
                        size_t row_count, size_t file_size);

void render_csv(const CsvReader &reader,
                const std::vector<size_t> *row_indices,
                const std::vector<size_t> *col_indices, size_t max_rows,
                char delimiter);

void render_json(const CsvReader &reader,
                 const std::vector<ColumnSchema> &schema,
                 const std::vector<size_t> *row_indices,
                 const std::vector<size_t> *col_indices, size_t max_rows);
