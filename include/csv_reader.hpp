#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

std::string unquote(std::string_view field);

class CsvReader {
private:
  int csv_fd = -1;
  size_t file_size_ = 0;
  void *addr = nullptr;

  std::string stdin_buf_; // buffer for stdin data

  std::vector<std::string_view> headers_;
  std::vector<std::string_view> fields_; // flat row-major, stride = ncols_
  size_t ncols_ = 0;
  size_t parsed_rows_ = 0;
  size_t total_rows_ = 0;

  void handle_mmap();
  void read_stdin();
  size_t parse_header(char delimiter);
  void append_row_fields(const char *base, size_t start, size_t end,
                         char delim);
  size_t count_rows_from(size_t offset) const;

public:
  CsvReader() = delete;
  CsvReader(const char *file_name);
  ~CsvReader();
  CsvReader(const CsvReader &) = delete;
  CsvReader &operator=(const CsvReader &) = delete;

  void parse(char delimiter);
  void parse_head(char delimiter, size_t max_rows);

  const char *data() const { return static_cast<const char *>(addr); }
  size_t size() const { return file_size_; }
  size_t row_count() const { return parsed_rows_; }
  size_t total_rows() const { return total_rows_; }
  size_t column_count() const { return ncols_; }
  const std::vector<std::string_view> &headers() const { return headers_; }

  std::span<const std::string_view> row(size_t i) const {
    return {fields_.data() + i * ncols_, ncols_};
  }
};
