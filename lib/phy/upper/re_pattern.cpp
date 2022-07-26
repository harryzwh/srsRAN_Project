/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsgnb/phy/upper/re_pattern.h"

using namespace srsgnb;

// Helper macro to assert the RB allocation of the pattern keeping the file and line for code tracing purposes.
#define re_pattern_assert()                                                                                            \
  do {                                                                                                                 \
    srsgnb_assert(rb_begin < MAX_RB, "RB begin ({}) is out-of-range", rb_begin);                                       \
    srsgnb_assert(rb_end > 0 && rb_end <= MAX_RB, "RB end ({}) is out-of-range", rb_end);                              \
    srsgnb_assert(rb_stride > 0, "RB stride ({}) is out-of-range", rb_stride);                                         \
  } while (false)

void re_pattern::get_inclusion_mask(span<bool> mask, unsigned symbol) const
{
  // Verify attributes and inputs.
  re_pattern_assert();
  srsgnb_assert(
      mask.size() >= rb_end, "Provided mask size (%d) is too small. The minimum is %d.", (unsigned)mask.size(), rb_end);

  // Skip if the symbol is not set to true.
  if (!symbols[symbol]) {
    return;
  }

  // For each resource block within the pattern.
  for (unsigned rb = rb_begin; rb < rb_end; rb += rb_stride) {
    // For each subcarrier in the resource block, apply the inclusion operation.
    for (unsigned k = 0; k != NRE; ++k) {
      // The following logical operation shall result in:
      // - true if mask is true or re_mask is true,
      // - otherwise false.
      mask[k + rb * NRE] |= re_mask[k];
    }
  }
}

void re_pattern::get_exclusion_mask(span<bool> mask, unsigned symbol) const
{
  // Verify attributes and inputs.
  re_pattern_assert();
  srsgnb_assert(
      mask.size() >= rb_end, "Provided mask size (%d) is too small. The minimum is %d.", (unsigned)mask.size(), rb_end);

  // Skip if the symbol is not used.
  if (!symbols[symbol]) {
    return;
  }

  // For each resource block within the pattern.
  for (unsigned rb = rb_begin; rb < rb_end; rb += rb_stride) {
    // For each subcarrier in the resource block, apply the exclusion operation
    for (unsigned k = 0; k != NRE; ++k) {
      // The following logical operation shall result in:
      // - true if mask is true and re_mask is false,
      // - otherwise false.
      mask[k + rb * NRE] &= (!re_mask[k]);
    }
  }
}

void re_pattern_list::merge(const re_pattern& pattern)
{
  // Iterate all given patterns.
  for (re_pattern& p : list) {
    // Skip if RB allocation parameters do NOT match.
    if (p.rb_begin != pattern.rb_begin || p.rb_end != pattern.rb_end || p.rb_stride != pattern.rb_stride) {
      continue;
    }

    // Check if symbol mask matches.
    bool lmatch = std::equal(pattern.symbols.begin(), pattern.symbols.end(), p.symbols.begin(), p.symbols.end());

    // Check if RE mask matches.
    bool kmatch = std::equal(pattern.re_mask.begin(), pattern.re_mask.end(), p.re_mask.begin(), p.re_mask.end());

    // If OFDM symbols and subcarriers mask match, it means that the patterns are completely overlapped and no merging
    // is required.
    if (kmatch && lmatch) {
      return;
    }

    // If OFDM symbols mask matches, combine subcarrier mask.
    if (lmatch) {
      for (unsigned k = 0; k != NRE; ++k) {
        p.re_mask[k] |= pattern.re_mask[k];
      }
      return;
    }

    // If subcarriers mask matches, combine OFDM symbols mask.
    if (kmatch) {
      for (unsigned l = 0; l != MAX_NSYMB_PER_SLOT; ++l) {
        p.symbols[l] |= pattern.symbols[l];
      }
      return;
    }
  }

  // If reached here, no pattern was matched. Check if there is free space.
  srsgnb_assert(
      !list.full(), "RE pattern list is full. It seems {} maximum entries are not enough.", (unsigned)MAX_RE_PATTERN);

  // Append pattern.
  list.emplace_back(pattern);
}

bool re_pattern_list::operator==(const re_pattern_list& other) const
{
  // Generates the inclusion mask for each symbol and compare if they are equal.
  for (unsigned symbol = 0; symbol != MAX_NSYMB_PER_SLOT; ++symbol) {
    std::array<bool, MAX_RB* NRE> inclusion_mask = {};
    get_inclusion_mask(inclusion_mask, symbol);

    std::array<bool, MAX_RB* NRE> inclusion_mask_other = {};
    other.get_inclusion_mask(inclusion_mask_other, symbol);

    // Early return false if they are not equal for this symbol.
    if (inclusion_mask != inclusion_mask_other) {
      return false;
    }
  }

  return true;
}

void re_pattern_list::get_inclusion_mask(span<bool> mask, unsigned symbol) const
{
  // Iterate all given patterns.
  for (const re_pattern& p : list) {
    p.get_inclusion_mask(mask, symbol);
  }
}

unsigned re_pattern_list::get_inclusion_count(unsigned                      start_symbol,
                                              unsigned                      nof_symbols,
                                              const bounded_bitset<MAX_RB>& rb_mask) const
{
  unsigned count = 0;

  for (unsigned symbol_idx = start_symbol; symbol_idx != start_symbol + nof_symbols; ++symbol_idx) {
    std::array<bool, MAX_RB* NRE> inclusion_mask = {};

    // Iterate all patterns.
    for (const re_pattern& p : list) {
      p.get_inclusion_mask(inclusion_mask, symbol_idx);
    }

    // Count all the included elements.
    for (unsigned rb_idx = 0; rb_idx != rb_mask.size(); ++rb_idx) {
      // Skip RB if it is not selected.
      if (!rb_mask.test(rb_idx)) {
        continue;
      }

      // Count each positive element in the inclusion mask.
      for (unsigned re_idx = rb_idx * NRE, re_idx_end = (rb_idx + 1) * NRE; re_idx != re_idx_end; ++re_idx) {
        count += inclusion_mask[re_idx] ? 1 : 0;
      }
    }
  }

  return count;
}

void re_pattern_list::get_exclusion_mask(span<bool> mask, unsigned symbol) const
{
  // Iterate all given patterns.
  for (const re_pattern& p : list) {
    p.get_exclusion_mask(mask, symbol);
  }
}
