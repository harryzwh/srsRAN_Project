/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "resource_grid_mapper_test_doubles.h"

using namespace srsran;

void resource_grid_mapper_spy::map(const re_buffer_reader& input,
                                   const re_pattern_list&  pattern,
                                   const re_pattern_list&  reserved,
                                   const precoding_configuration& /* precoding */)
{
  unsigned i_re = 0;
  for (unsigned i_symbol = 0; i_symbol != MAX_NSYMB_PER_SLOT; ++i_symbol) {
    // Get the symbol RE mask.
    bounded_bitset<MAX_RB * NRE> symbol_re_mask(MAX_RB * NRE);
    pattern.get_inclusion_mask(symbol_re_mask, i_symbol);
    reserved.get_exclusion_mask(symbol_re_mask, i_symbol);

    // Find the highest used subcarrier. Skip symbol if no active subcarrier.
    int i_highest_subc = symbol_re_mask.find_highest();
    if (i_highest_subc < 0) {
      continue;
    }

    // Resize the mask to the highest subcarrier, ceiling to PRB.
    symbol_re_mask.resize(divide_ceil(i_highest_subc, NRE) * NRE);

    // Count the number of mapped RE.
    unsigned nof_re = symbol_re_mask.count();

    for (unsigned i_layer = 0, nof_layers = input.get_nof_slices(); i_layer != nof_layers; ++i_layer) {
      // Map each layer without precoding.
      span<const cf_t> layer_data = input.get_slice(i_layer);
      span<const cf_t> unmapped =
          rg_writer_spy.put(i_layer, i_symbol, 0, symbol_re_mask, layer_data.subspan(i_re, nof_re));
      srsran_assert(unmapped.empty(), "Not all REs have been mapped to the grid. {} remaining.", unmapped.size());
    }

    // Advance RE counter.
    i_re += nof_re;
  }

  srsran_assert(i_re == input.get_nof_re(),
                "The nuber of mapped RE (i.e., {}) does not match the number of input RE (i.e., {}).",
                i_re,
                input.get_nof_re());
}

void resource_grid_mapper_spy::map(const re_buffer_reader& input,
                                   const re_pattern_list&  pattern,
                                   const precoding_configuration& /* precoding */)
{
  // Map with an empty list of reserved RE patterns.
  map(input, pattern, re_pattern_list(), precoding_configuration());
}
