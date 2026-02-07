#include "include/pager.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

// --- Terminal control ---

static int tty_fd = -1;
static struct termios orig_termios;
static bool raw_mode_active = false;

static volatile sig_atomic_t resize_flag = 1; // start with 1 to trigger initial size read

static void handle_winch(int) { resize_flag = 1; }

static void disable_raw_mode() {
  if (raw_mode_active) {
    tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
    raw_mode_active = false;
  }
  // Leave alternate screen + show cursor
  write(STDOUT_FILENO, "\033[?1049l\033[?25h", 14);
  if (tty_fd >= 0) {
    close(tty_fd);
    tty_fd = -1;
  }
}

static bool enable_raw_mode() {
  tty_fd = open("/dev/tty", O_RDONLY);
  if (tty_fd < 0)
    return false;

  if (tcgetattr(tty_fd, &orig_termios) < 0) {
    close(tty_fd);
    tty_fd = -1;
    return false;
  }

  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  raw.c_iflag &= ~(IXON | ICRNL);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(tty_fd, TCSAFLUSH, &raw);
  raw_mode_active = true;

  // Enter alternate screen + hide cursor
  write(STDOUT_FILENO, "\033[?1049h\033[?25l", 14);

  struct sigaction sa = {};
  sa.sa_handler = handle_winch;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGWINCH, &sa, nullptr);

  return true;
}

static std::pair<size_t, size_t> get_term_size() {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
    return {w.ws_row, w.ws_col};
  return {24, 80};
}

// --- Key constants ---

enum Key {
  KEY_NONE = -1,
  KEY_UP = 1000,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_PGUP,
  KEY_PGDN,
  KEY_HOME,
  KEY_END,
  KEY_BACKSPACE_K = 127,
};

static int read_key() {
  char c;
  ssize_t n = read(tty_fd, &c, 1);
  if (n <= 0)
    return KEY_NONE;

  if (c == '\033') {
    char seq[4];
    if (read(tty_fd, &seq[0], 1) != 1)
      return '\033';
    if (seq[0] == '[') {
      if (read(tty_fd, &seq[1], 1) != 1)
        return '\033';
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(tty_fd, &seq[2], 1) != 1)
          return '\033';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '5':
            return KEY_PGUP;
          case '6':
            return KEY_PGDN;
          case '1':
            return KEY_HOME;
          case '4':
            return KEY_END;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return KEY_UP;
        case 'B':
          return KEY_DOWN;
        case 'C':
          return KEY_RIGHT;
        case 'D':
          return KEY_LEFT;
        case 'H':
          return KEY_HOME;
        case 'F':
          return KEY_END;
        }
      }
    } else if (seq[0] == 'O') {
      if (read(tty_fd, &seq[1], 1) != 1)
        return '\033';
      switch (seq[1]) {
      case 'H':
        return KEY_HOME;
      case 'F':
        return KEY_END;
      }
    }
    return '\033';
  }

  if (c == 3) // Ctrl+C
    return 'q';

  return static_cast<unsigned char>(c);
}

// --- Rendering helpers ---

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

static std::string truncate_str(std::string_view s, size_t max_w) {
  if (s.size() <= max_w)
    return std::string(s);
  if (max_w <= 3)
    return std::string(max_w, '.');
  return std::string(s.substr(0, max_w - 3)) + "...";
}

// --- Pager state ---

struct PagerState {
  size_t scroll_row = 0;
  size_t scroll_col = 0; // first visible column index
  size_t term_rows = 24;
  size_t term_cols = 80;
  size_t data_rows = 0; // total displayable data rows
  bool searching = false;
  std::string search_query;
  std::vector<size_t> search_hits; // display-row indices matching search
  size_t current_hit = SIZE_MAX;
  std::string status_msg;
};

static size_t viewport_rows(const PagerState &st) {
  // Layout: top border(1) + header(1) + types(1) + separator(1) +
  //         data rows + bottom border(1) + status(1) = 6 fixed lines
  return (st.term_rows > 6) ? st.term_rows - 6 : 1;
}

static size_t visible_cols(const std::vector<size_t> &col_widths,
                           size_t start_col, size_t term_w) {
  size_t used = 1; // left border
  size_t count = 0;
  for (size_t c = start_col; c < col_widths.size(); ++c) {
    size_t needed = col_widths[c] + 3; // " content " + border
    if (used + needed > term_w && count > 0)
      break;
    used += needed;
    ++count;
  }
  return (count == 0) ? 1 : count;
}

// --- Search ---

static void do_search(PagerState &st, const CsvReader &reader,
                      const std::vector<size_t> *row_indices,
                      const std::vector<size_t> *col_indices) {
  st.search_hits.clear();
  st.current_hit = SIZE_MAX;
  if (st.search_query.empty())
    return;

  // Case-insensitive search
  std::string query_lower;
  query_lower.reserve(st.search_query.size());
  for (char c : st.search_query)
    query_lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);

  size_t ncols = reader.column_count();
  for (size_t d = 0; d < st.data_rows; ++d) {
    size_t actual = row_indices ? (*row_indices)[d] : d;
    auto row = reader.row(actual);
    for (size_t ci = 0; ci < ncols; ++ci) {
      if (col_indices) {
        bool visible = false;
        for (auto idx : *col_indices)
          if (idx == ci) {
            visible = true;
            break;
          }
        if (!visible)
          continue;
      }
      std::string val = unquote(row[ci]);
      // to lower
      for (char &ch : val)
        if (ch >= 'A' && ch <= 'Z')
          ch += 32;
      if (val.find(query_lower) != std::string::npos) {
        st.search_hits.push_back(d);
        break;
      }
    }
  }

  if (!st.search_hits.empty()) {
    // Find first hit at or after current scroll position
    st.current_hit = 0;
    for (size_t i = 0; i < st.search_hits.size(); ++i) {
      if (st.search_hits[i] >= st.scroll_row) {
        st.current_hit = i;
        break;
      }
    }
    st.scroll_row = st.search_hits[st.current_hit];
    st.status_msg = "Match " + std::to_string(st.current_hit + 1) + " of " +
                    std::to_string(st.search_hits.size());
  } else {
    st.status_msg = "No matches for '" + st.search_query + "'";
  }
}

// --- Main render ---

static void render(const PagerState &st, const CsvReader &reader,
                   const std::vector<ColumnSchema> &schema,
                   const std::vector<size_t> *row_indices,
                   const std::vector<size_t> *col_indices,
                   const std::vector<size_t> &col_widths,
                   size_t total_match_count) {
  // Build output in a buffer for flicker-free rendering
  std::string buf;
  buf.reserve(st.term_rows * st.term_cols * 2);

  buf += "\033[H"; // cursor home

  auto &headers = reader.headers();
  size_t ncols_total = col_indices ? col_indices->size() : reader.column_count();
  size_t vis = visible_cols(col_widths, st.scroll_col, st.term_cols);
  size_t end_col = std::min(st.scroll_col + vis, ncols_total);

  // Helper to get the actual column index
  auto actual_col = [&](size_t display_c) -> size_t {
    return col_indices ? (*col_indices)[display_c] : display_c;
  };

  auto get_width = [&](size_t display_c) -> size_t {
    return col_widths[display_c];
  };

  // --- Horizontal line ---
  auto hline = [&](const char *left, const char *mid, const char *right) {
    buf += left;
    for (size_t c = st.scroll_col; c < end_col; ++c) {
      size_t w = get_width(c) + 2;
      for (size_t i = 0; i < w; ++i)
        buf += "\xe2\x94\x80"; // ─
      if (c + 1 < end_col)
        buf += mid;
    }
    buf += right;
    buf += "\033[K\n"; // clear to end of line
  };

  // --- Print a row of cells ---
  auto print_cells = [&](auto get_val, bool bold) {
    buf += "\xe2\x94\x82"; // │
    for (size_t c = st.scroll_col; c < end_col; ++c) {
      std::string val = get_val(c);
      std::string display = truncate_str(val, get_width(c));
      size_t pad = get_width(c) - display.size();
      buf += " ";
      if (bold)
        buf += "\033[1m";
      buf += display;
      if (bold)
        buf += "\033[0m";
      buf.append(pad, ' ');
      buf += " \xe2\x94\x82"; // │
    }
    buf += "\033[K\n";
  };

  size_t vp = viewport_rows(st);
  size_t vis_end = std::min(st.scroll_row + vp, st.data_rows);

  // Top border
  hline("\xe2\x94\x8c", "\xe2\x94\xac", "\xe2\x94\x90"); // ┌ ┬ ┐

  // Header row
  print_cells(
      [&](size_t c) { return unquote(headers[actual_col(c)]); }, true);

  // Type row
  print_cells(
      [&](size_t c) {
        size_t ac = actual_col(c);
        return std::string(
            (ac < schema.size()) ? type_name(schema[ac].type) : "text");
      },
      false);

  // Separator
  hline("\xe2\x94\x9c", "\xe2\x94\xbc", "\xe2\x94\xa4"); // ├ ┼ ┤

  // Data rows
  size_t lines_drawn = 0;
  for (size_t r = st.scroll_row; r < vis_end; ++r) {
    size_t actual = row_indices ? (*row_indices)[r] : r;
    auto row = reader.row(actual);

    // Check if this row is a search hit
    bool is_hit = false;
    if (!st.search_hits.empty() && st.current_hit < st.search_hits.size() &&
        st.search_hits[st.current_hit] == r)
      is_hit = true;

    buf += "\xe2\x94\x82"; // │
    for (size_t c = st.scroll_col; c < end_col; ++c) {
      size_t ac = actual_col(c);
      std::string val = (ac < row.size()) ? unquote(row[ac]) : "";
      // Replace newlines with spaces for display
      for (char &ch : val)
        if (ch == '\n' || ch == '\r')
          ch = ' ';
      std::string display = truncate_str(val, get_width(c));
      size_t pad = get_width(c) - display.size();
      buf += " ";
      if (is_hit)
        buf += "\033[33m"; // yellow for search hits
      buf += display;
      if (is_hit)
        buf += "\033[0m";
      buf.append(pad, ' ');
      buf += " \xe2\x94\x82"; // │
    }
    buf += "\033[K\n";
    ++lines_drawn;
  }

  // Fill empty viewport rows
  for (size_t i = lines_drawn; i < vp; ++i) {
    buf += "\xe2\x94\x82"; // │
    for (size_t c = st.scroll_col; c < end_col; ++c) {
      buf.append(get_width(c) + 2, ' ');
      buf += "\xe2\x94\x82"; // │
    }
    buf += "\033[K\n";
  }

  // Bottom border
  hline("\xe2\x94\x94", "\xe2\x94\xb4", "\xe2\x94\x98"); // └ ┴ ┘

  // Status bar
  buf += "\033[7m"; // reverse video

  std::string left;
  if (st.searching) {
    left = "/" + st.search_query + "\xe2\x96\x8b"; // cursor block
  } else if (!st.status_msg.empty()) {
    left = st.status_msg;
  } else {
    left = " rows " + std::to_string(st.scroll_row + 1) + "-" +
           std::to_string(vis_end) + " of " +
           format_count(total_match_count);
  }

  size_t display_ncols =
      col_indices ? col_indices->size() : reader.column_count();
  std::string right = std::to_string(display_ncols) + " cols | " +
                       format_size(reader.size()) +
                       " | \xe2\x86\x91\xe2\x86\x93 scroll  "  // ↑↓
                       "\xe2\x86\x90\xe2\x86\x91 cols  "        // ←→
                       "/ search  q quit";
  // Fix: arrows use multi-byte, but we need to track display width
  // For simplicity, just truncate if needed
  size_t avail = (st.term_cols > right.size()) ? st.term_cols : st.term_cols;
  buf += " ";
  buf += left;
  // Pad between left and right
  // Approximate: status bar fills term_cols
  size_t left_display_len = left.size() + 1;
  size_t right_display_len = right.size() + 1;
  if (left_display_len + right_display_len < st.term_cols) {
    buf.append(st.term_cols - left_display_len - right_display_len, ' ');
    buf += right;
  }
  buf += " \033[0m\033[K";

  write(STDOUT_FILENO, buf.data(), buf.size());
}

// --- Public entry point ---

void run_pager(const CsvReader &reader,
               const std::vector<ColumnSchema> &schema,
               const std::vector<size_t> *row_indices,
               const std::vector<size_t> *col_indices,
               size_t total_match_count) {
  if (!enable_raw_mode()) {
    // Fallback: can't enter raw mode, just dump
    return;
  }

  PagerState st;
  st.data_rows = row_indices ? row_indices->size() : reader.row_count();

  // Compute display column count and column widths
  size_t ncols_display = col_indices ? col_indices->size() : reader.column_count();
  auto &headers = reader.headers();

  auto actual_col = [&](size_t display_c) -> size_t {
    return col_indices ? (*col_indices)[display_c] : display_c;
  };

  // Compute column widths from headers + first 1000 rows
  std::vector<size_t> col_widths(ncols_display, 0);
  for (size_t c = 0; c < ncols_display; ++c) {
    size_t ac = actual_col(c);
    std::string hdr = unquote(headers[ac]);
    col_widths[c] = hdr.size();
    std::string_view tn =
        (ac < schema.size()) ? type_name(schema[ac].type) : "text";
    col_widths[c] = std::max(col_widths[c], tn.size());
  }

  size_t sample = std::min(st.data_rows, static_cast<size_t>(1000));
  for (size_t r = 0; r < sample; ++r) {
    size_t actual = row_indices ? (*row_indices)[r] : r;
    auto row = reader.row(actual);
    for (size_t c = 0; c < ncols_display; ++c) {
      size_t ac = actual_col(c);
      if (ac < row.size()) {
        std::string val = unquote(row[ac]);
        col_widths[c] = std::max(col_widths[c], val.size());
      }
    }
  }

  // Cap widths
  for (auto &w : col_widths)
    w = std::min(w, static_cast<size_t>(60));

  bool running = true;
  while (running) {
    if (resize_flag) {
      resize_flag = 0;
      auto [rows, cols] = get_term_size();
      st.term_rows = rows;
      st.term_cols = cols;

      // Cap column widths to terminal
      size_t total_padding = ncols_display * 3 + 1;
      if (total_padding < st.term_cols) {
        size_t available = st.term_cols - total_padding;
        size_t total_content = 0;
        for (auto w : col_widths)
          total_content += w;
        if (total_content > available) {
          size_t max_per = std::max(static_cast<size_t>(5), available / ncols_display);
          for (auto &w : col_widths)
            w = std::min(w, max_per);
        }
      }
    }

    size_t vp = viewport_rows(st);

    // Clamp scroll
    if (st.data_rows <= vp)
      st.scroll_row = 0;
    else if (st.scroll_row > st.data_rows - vp)
      st.scroll_row = st.data_rows - vp;
    if (st.scroll_col >= ncols_display)
      st.scroll_col = (ncols_display > 0) ? ncols_display - 1 : 0;

    render(st, reader, schema, row_indices, col_indices, col_widths,
           total_match_count);

    int key = read_key();
    if (key == KEY_NONE)
      continue;

    if (st.searching) {
      if (key == '\033' || key == 3) {
        // Cancel search
        st.searching = false;
        st.search_query.clear();
        st.status_msg.clear();
      } else if (key == '\r' || key == '\n') {
        // Execute search
        st.searching = false;
        do_search(st, reader, row_indices, col_indices);
      } else if (key == KEY_BACKSPACE_K || key == 8) {
        if (!st.search_query.empty())
          st.search_query.pop_back();
      } else if (key >= 32 && key < 127) {
        st.search_query += static_cast<char>(key);
      }
      continue;
    }

    st.status_msg.clear();

    switch (key) {
    case 'q':
    case '\033':
      running = false;
      break;

    case 'k':
    case KEY_UP:
      if (st.scroll_row > 0)
        --st.scroll_row;
      break;

    case 'j':
    case KEY_DOWN:
    case '\r':
    case '\n':
      if (st.scroll_row + vp < st.data_rows)
        ++st.scroll_row;
      break;

    case ' ':
    case KEY_PGDN:
      st.scroll_row = std::min(st.scroll_row + vp,
                               (st.data_rows > vp) ? st.data_rows - vp : 0);
      break;

    case 'b':
    case KEY_PGUP:
      st.scroll_row = (st.scroll_row >= vp) ? st.scroll_row - vp : 0;
      break;

    case 'g':
    case KEY_HOME:
      st.scroll_row = 0;
      break;

    case 'G':
    case KEY_END:
      st.scroll_row = (st.data_rows > vp) ? st.data_rows - vp : 0;
      break;

    case 'h':
    case KEY_LEFT:
      if (st.scroll_col > 0)
        --st.scroll_col;
      break;

    case 'l':
    case KEY_RIGHT:
      if (st.scroll_col + 1 < ncols_display)
        ++st.scroll_col;
      break;

    case '/':
      st.searching = true;
      st.search_query.clear();
      // Show cursor during search
      write(STDOUT_FILENO, "\033[?25h", 6);
      break;

    case 'n':
      if (!st.search_hits.empty()) {
        st.current_hit = (st.current_hit + 1) % st.search_hits.size();
        st.scroll_row = st.search_hits[st.current_hit];
        st.status_msg =
            "Match " + std::to_string(st.current_hit + 1) + " of " +
            std::to_string(st.search_hits.size());
      }
      break;

    case 'N':
      if (!st.search_hits.empty()) {
        st.current_hit = (st.current_hit == 0) ? st.search_hits.size() - 1
                                                : st.current_hit - 1;
        st.scroll_row = st.search_hits[st.current_hit];
        st.status_msg =
            "Match " + std::to_string(st.current_hit + 1) + " of " +
            std::to_string(st.search_hits.size());
      }
      break;
    }

    // Hide cursor after search
    if (!st.searching)
      write(STDOUT_FILENO, "\033[?25l", 6);
  }

  disable_raw_mode();
}
