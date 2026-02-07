#include "include/delim.hpp"
#include <array>
#include <cmath>
#include <vector>

static size_t count_fields(const char *line, size_t len, char delim) {
  size_t count = 1;
  bool in_quotes = false;
  for (size_t i = 0; i < len; ++i) {
    if (line[i] == '"') {
      in_quotes = !in_quotes;
    } else if (!in_quotes && line[i] == delim) {
      ++count;
    }
  }
  return count;
}

char detect_delimiter(const char *data, size_t size, size_t sample_lines) {
  if (!data || size == 0)
    return ',';

  constexpr std::array<char, 4> candidates = {',', '\t', '|', ';'};
  char best = ',';
  double best_score = -1.0;

  // Collect line start/end offsets for up to sample_lines lines
  struct LineSpan {
    size_t start, len;
  };
  std::vector<LineSpan> lines;
  lines.reserve(sample_lines);
  size_t pos = 0;
  while (pos < size && lines.size() < sample_lines) {
    size_t start = pos;
    bool in_quotes = false;
    while (pos < size) {
      if (data[pos] == '"')
        in_quotes = !in_quotes;
      else if (!in_quotes && data[pos] == '\n')
        break;
      ++pos;
    }
    size_t end = pos;
    if (end > start && data[end - 1] == '\r')
      --end;
    if (end > start)
      lines.push_back({start, end - start});
    if (pos < size)
      ++pos; // skip \n
  }

  if (lines.empty())
    return ',';

  for (char c : candidates) {
    std::vector<double> counts;
    counts.reserve(lines.size());
    for (auto &l : lines) {
      counts.push_back(
          static_cast<double>(count_fields(data + l.start, l.len, c)));
    }

    // Need at least 2 fields to be a valid delimiter
    double sum = 0;
    for (double v : counts)
      sum += v;
    double mean = sum / static_cast<double>(counts.size());
    if (mean < 2.0)
      continue;

    double var = 0;
    for (double v : counts)
      var += (v - mean) * (v - mean);
    double stddev = std::sqrt(var / static_cast<double>(counts.size()));

    double score = mean / (1.0 + stddev);
    if (score > best_score) {
      best_score = score;
      best = c;
    }
  }

  return best;
}
