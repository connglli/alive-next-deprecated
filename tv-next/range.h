// Straight-line integer range analysis and known-bits propagation.
//
// Performs a single forward pass over an acyclic instruction sequence in
// program order. Fixed-point iteration is omitted because instructions form a
// DAG in SSA representation.
//
// KnownRange stores closed unsigned intervals, closed signed intervals, and
// known-zero and known-one bitmasks in the value's native bitwidth. Transfer
// functions compute bounds per opcode and flag combination.
//
// When the upper bound of an unsigned interval has its most significant bit
// clear, the signed interval matches the unsigned interval. When the lower
// bound of a signed interval is non-negative, the unsigned interval matches the
// signed interval.
//
// Undef and poison status are tracked separately:
// * undef_free: the value cannot evaluate to undef.
// * poison_free: the value cannot evaluate to poison.
// When both flags hold, well_defined() returns true and the freeze instruction
// is an identity operation on the value.
//
// Alive2 independently verifies all preconditions derived from these ranges.

#pragma once

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/KnownBits.h"

#include <map>
#include <optional>
#include <utility>

namespace alive_tv_next {

struct KnownRange {
  // Closed intervals in the value bitwidth. Either or both may be absent.
  std::optional<std::pair<llvm::APInt, llvm::APInt>> u; // Unsigned [lo, hi].
  std::optional<std::pair<llvm::APInt, llvm::APInt>> s; // Signed   [lo, hi].

  // Per-bit known-zero and known-one masks.
  std::optional<llvm::KnownBits> bits;

  // Well-definedness indicators.
  bool undef_free = false;
  bool poison_free = false;

  // True when the value is guaranteed free of undef and poison.
  bool well_defined() const {
    return undef_free && poison_free;
  }
};

// Maps values to their computed range and bit information. Absent values have
// no inferred constraints.
using RangeMap = std::map<const llvm::Value *, KnownRange>;

// Computes range information for an instruction sequence in program order.
//
// Constant operands are handled during the pass. seed provides initial
// ranges for function arguments or external values.
RangeMap computeRanges(llvm::ArrayRef<llvm::Instruction *> insts,
                       const RangeMap &seed = {});

} // namespace alive_tv_next
