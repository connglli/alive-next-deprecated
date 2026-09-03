// Structural diffing between paired @src and @tgt functions.
//
// Identifies instruction-level differences between single-basic-block
// functions and clusters differences into DiffRegions for verification.
//
// Symmetric regions pair instructions at matching positions. Asymmetric
// regions group differing instruction sequences with mismatched counts.

#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"

#include <optional>
#include <vector>

namespace alive_tv_next {

// Instruction-level difference between src and tgt at matching block offset.
struct DiffPosition {
  // Index of the instruction in the basic block, 0-based, excluding
  // terminators.
  size_t inst_idx;

  // Pointers to the differing instructions in their respective modules.
  llvm::Instruction *src_inst;
  llvm::Instruction *tgt_inst;
};

// A contiguous sequence of differences lifted and verified as a single unit.
//
// In a symmetric region (is_asymmetric is false), positions is non-empty and
// contains paired instructions. The exit instruction is positions.back().
//
// In an asymmetric region (is_asymmetric is true), positions is empty.
// src_region and tgt_region store the differing instruction sequences on each
// side. The exit instructions are src_region.back() and tgt_region.back(),
// which must share the same type.
struct DiffRegion {
  std::vector<DiffPosition> positions;

  bool is_asymmetric = false;
  std::vector<llvm::Instruction *> src_region;
  std::vector<llvm::Instruction *> tgt_region;
  size_t src_start_idx = 0;
  size_t tgt_start_idx = 0;
};

// Results of structural diffing across two functions.
struct DiffResult {
  std::vector<DiffRegion> regions;
  // Number of non-terminator instruction pairs that matched textually.
  size_t identical_count = 0;
};

// Computes instruction differences and groups them into DiffRegions.
//
// Both functions must contain exactly one basic block, and their terminators
// must match textually modulo trailing metadata. Returns std::nullopt and
// writes a diagnostic to llvm::errs() if either condition is violated.
std::optional<DiffResult> computeDiffRegions(llvm::Function &src,
                                             llvm::Function &tgt);

} // namespace alive_tv_next
