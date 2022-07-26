/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "srsgnb/adt/span.h"
#include <fstream>
#include <vector>

namespace srsgnb {

template <typename T>
class file_sink
{
private:
  std::ofstream binary_file;

public:
  /// Default constructor. It does not open file.
  file_sink() = default;

  /// Constructs a file sink using a file name.
  file_sink(std::string file_name) : binary_file(file_name, std::ios::out | std::ios::binary)
  {
    SRSGNB_ALWAYS_ASSERT__(binary_file.is_open(), "Failed to open file.");
  }

  /// Checks if the file is open.
  bool is_open() const { return binary_file.is_open(); }

  /// Writes
  void write(span<const T> data)
  {
    SRSGNB_ALWAYS_ASSERT__(binary_file.is_open(), "File not opened.");

    binary_file.write(reinterpret_cast<const char*>(data.data()), sizeof(T) * data.size());
  }
};

} // namespace srsgnb
