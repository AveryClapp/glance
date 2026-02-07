#pragma once

#include <cstddef>

char detect_delimiter(const char *data, size_t size,
                      size_t sample_lines = 20);
