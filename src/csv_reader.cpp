#include "include/csv_reader.hpp"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

// --- Utility ---

std::string unquote(std::string_view field) {
  if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
    field.remove_prefix(1);
    field.remove_suffix(1);
    std::string result;
    result.reserve(field.size());
    for (size_t i = 0; i < field.size(); ++i) {
      if (field[i] == '"' && i + 1 < field.size() && field[i + 1] == '"') {
        result += '"';
        ++i;
      } else {
        result += field[i];
      }
    }
    return result;
  }
  return std::string(field);
}

// --- NEON-accelerated newline counting ---

static size_t count_newlines(const char *data, size_t len) {
  size_t count = 0;
  size_t i = 0;

#ifdef __ARM_NEON
  uint8x16_t nl = vdupq_n_u8('\n');
  uint8x16_t ones = vdupq_n_u8(1);

  while (i + 16 <= len) {
    uint8x16_t acc = vdupq_n_u8(0);
    size_t remaining = (len - i) / 16;
    size_t batch = std::min(remaining, static_cast<size_t>(255));
    for (size_t j = 0; j < batch; ++j) {
      uint8x16_t chunk =
          vld1q_u8(reinterpret_cast<const uint8_t *>(data + i));
      acc = vaddq_u8(acc, vandq_u8(vceqq_u8(chunk, nl), ones));
      i += 16;
    }
    count += vaddvq_u8(acc);
  }
#endif

  for (; i < len; ++i)
    if (data[i] == '\n')
      ++count;

  return count;
}

// --- Fast line-end finder (memchr fast-path, quote-aware fallback) ---

static size_t find_line_end(const char *base, size_t total, size_t start) {
  // Fast: find next \n
  const void *nl = memchr(base + start, '\n', total - start);
  size_t nl_pos =
      nl ? static_cast<size_t>(static_cast<const char *>(nl) - base) : total;

  // Fast: any quote before that newline?
  const void *q = memchr(base + start, '"', nl_pos - start);
  if (!q)
    return nl_pos; // No quotes → newline is a row boundary

  // Slow path: quote-aware scan
  bool in_quotes = false;
  for (size_t i = start; i < total; ++i) {
    if (base[i] == '"')
      in_quotes = !in_quotes;
    else if (!in_quotes && base[i] == '\n')
      return i;
  }
  return total;
}

// --- Field parsing (directly into flat storage) ---

static std::vector<std::string_view>
parse_line_fields(const char *base, size_t start, size_t end, char delim) {
  std::vector<std::string_view> fields;
  if (start == end)
    return fields;

  size_t i = start;
  while (i < end) {
    if (base[i] == '"') {
      size_t fs = i++;
      while (i < end) {
        if (base[i] == '"') {
          if (i + 1 < end && base[i + 1] == '"')
            i += 2;
          else
            break;
        } else
          ++i;
      }
      if (i < end)
        ++i; // closing quote
      fields.emplace_back(base + fs, i - fs);
      if (i < end && base[i] == delim)
        ++i;
    } else {
      size_t fs = i;
      while (i < end && base[i] != delim)
        ++i;
      fields.emplace_back(base + fs, i - fs);
      if (i < end)
        ++i;
    }
  }
  if (end > start && base[end - 1] == delim)
    fields.emplace_back(base + end, 0);

  return fields;
}

// --- CsvReader implementation ---

CsvReader::CsvReader(const char *file_name) {
  if (std::strcmp(file_name, "-") == 0) {
    read_stdin();
    return;
  }

  this->csv_fd = open(file_name, O_RDONLY);
  if (this->csv_fd < 0)
    throw std::runtime_error("Failed to open csv file");

  struct stat sbuf;
  if (fstat(this->csv_fd, &sbuf) < 0) {
    close(this->csv_fd);
    throw std::runtime_error("Failed to get length of the file");
  }
  this->file_size_ = static_cast<size_t>(sbuf.st_size);

  if (this->file_size_ > 0)
    handle_mmap();
}

void CsvReader::read_stdin() {
  // Read all of stdin into a buffer
  constexpr size_t chunk = 1 << 16; // 64KB
  char buf[chunk];
  ssize_t n;
  while ((n = ::read(STDIN_FILENO, buf, chunk)) > 0)
    stdin_buf_.append(buf, static_cast<size_t>(n));
  if (stdin_buf_.empty())
    throw std::runtime_error("No data on stdin");
  file_size_ = stdin_buf_.size();
  addr = stdin_buf_.data();
}

void CsvReader::handle_mmap() {
  this->addr =
      mmap(nullptr, this->file_size_, PROT_READ, MAP_PRIVATE, this->csv_fd, 0);
  if (this->addr == MAP_FAILED) {
    close(this->csv_fd);
    throw std::runtime_error("Failed to MMAP file");
  }
}

CsvReader::~CsvReader() {
  if (!stdin_buf_.empty()) {
    // stdin data owned by stdin_buf_, no munmap needed
    addr = nullptr;
  }
  if (addr && addr != MAP_FAILED)
    munmap(addr, file_size_);
  if (csv_fd >= 0)
    close(csv_fd);
}

size_t CsvReader::parse_header(char delimiter) {
  if (!addr || file_size_ == 0)
    return 0;

  const char *base = data();
  size_t line_end = find_line_end(base, file_size_, 0);
  size_t actual_end = line_end;
  if (actual_end > 0 && base[actual_end - 1] == '\r')
    --actual_end;

  headers_ = parse_line_fields(base, 0, actual_end, delimiter);
  ncols_ = headers_.size();

  return (line_end < file_size_) ? line_end + 1 : file_size_;
}

void CsvReader::append_row_fields(const char *base, size_t start, size_t end,
                                  char delim) {
  size_t fields_added = 0;
  size_t i = start;

  while (i < end && fields_added < ncols_) {
    if (base[i] == '"') {
      size_t fs = i++;
      while (i < end) {
        if (base[i] == '"') {
          if (i + 1 < end && base[i + 1] == '"')
            i += 2;
          else
            break;
        } else
          ++i;
      }
      if (i < end)
        ++i;
      fields_.emplace_back(base + fs, i - fs);
      ++fields_added;
      if (i < end && base[i] == delim)
        ++i;
    } else {
      size_t fs = i;
      while (i < end && base[i] != delim)
        ++i;
      fields_.emplace_back(base + fs, i - fs);
      ++fields_added;
      if (i < end)
        ++i;
    }
  }

  // Trailing delimiter → one more empty field
  if (fields_added < ncols_ && end > start && base[end - 1] == delim) {
    fields_.emplace_back();
    ++fields_added;
  }

  // Pad ragged rows
  while (fields_added < ncols_) {
    fields_.emplace_back();
    ++fields_added;
  }
}

size_t CsvReader::count_rows_from(size_t offset) const {
  if (offset >= file_size_)
    return 0;

  const char *d = data() + offset;
  size_t len = file_size_ - offset;
  if (len == 0)
    return 0;

  // Fast path: no quotes in remainder → pure NEON newline count
  if (!memchr(d, '"', len)) {
    size_t nl = count_newlines(d, len);
    // If file doesn't end with \n, there's one more row
    if (d[len - 1] != '\n')
      ++nl;
    return nl;
  }

  // Slow path: quote-aware
  size_t count = 0;
  bool in_quotes = false;
  for (size_t i = 0; i < len; ++i) {
    if (d[i] == '"')
      in_quotes = !in_quotes;
    else if (!in_quotes && d[i] == '\n')
      ++count;
  }
  if (len > 0 && d[len - 1] != '\n')
    ++count;
  return count;
}

void CsvReader::parse(char delimiter) {
  headers_.clear();
  fields_.clear();
  parsed_rows_ = 0;
  total_rows_ = 0;
  ncols_ = 0;

  size_t pos = parse_header(delimiter);
  if (ncols_ == 0)
    return;

  const char *base = data();
  size_t total = file_size_;

  // Pre-estimate rows to avoid reallocation
  size_t est_line_len = (pos > 0) ? pos : 50;
  size_t est_rows = (total > pos) ? (total - pos) / est_line_len + 1 : 0;
  fields_.reserve(est_rows * ncols_);

  while (pos < total) {
    size_t line_end = find_line_end(base, total, pos);
    size_t actual_end = line_end;
    if (actual_end > pos && base[actual_end - 1] == '\r')
      --actual_end;

    if (actual_end == pos) {
      pos = (line_end < total) ? line_end + 1 : total;
      continue;
    }

    append_row_fields(base, pos, actual_end, delimiter);
    ++parsed_rows_;
    pos = (line_end < total) ? line_end + 1 : total;
  }

  total_rows_ = parsed_rows_;
}

void CsvReader::parse_head(char delimiter, size_t max_rows) {
  headers_.clear();
  fields_.clear();
  parsed_rows_ = 0;
  total_rows_ = 0;
  ncols_ = 0;

  size_t pos = parse_header(delimiter);
  if (ncols_ == 0)
    return;

  const char *base = data();
  size_t total = file_size_;

  fields_.reserve(max_rows * ncols_);

  while (pos < total && parsed_rows_ < max_rows) {
    size_t line_end = find_line_end(base, total, pos);
    size_t actual_end = line_end;
    if (actual_end > pos && base[actual_end - 1] == '\r')
      --actual_end;

    if (actual_end == pos) {
      pos = (line_end < total) ? line_end + 1 : total;
      continue;
    }

    append_row_fields(base, pos, actual_end, delimiter);
    ++parsed_rows_;
    pos = (line_end < total) ? line_end + 1 : total;
  }

  // Count remaining rows without parsing them
  total_rows_ = parsed_rows_ + count_rows_from(pos);
}
