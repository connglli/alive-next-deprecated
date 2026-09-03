// Precondition synthesis for unconstrained verification failures.
//
// When direct refinement verification fails on a TvUnit, a proposer infers
// candidate preconditions from parent functions.
//
// A proposer produces an AssumedTvUnit containing:
// 1. modified_unit: The original unit with llvm.assume calls injected into
//    both src_fn and tgt_fn.
// 2. assume_checks: Standalone verification units that prove each synthesized
//    condition holds unconditionally under the inputs of the parent function.
// 3. proposer_name: Identifier for diagnostic logging.
//
// All assume_checks must verify before the modified_unit is evaluated.

#pragma once

#include "tv-next/unit.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace alive_tv_next {

// Preconditions and verification checks produced by a proposer.
struct AssumedTvUnit {
  TvUnit modified_unit; // Unit with injected llvm.assume calls.
  std::vector<TvUnit> assume_checks; // Independent validity proofs.
  std::string proposer_name;
};

// Signature for pattern-specific proposer callbacks.
using AssumeProposerFn = std::function<std::optional<AssumedTvUnit>(
    const TvUnit &, llvm::Function &parent_src, llvm::Function &parent_tgt,
    llvm::LLVMContext &)>;

// Evaluates registered pattern matchers, then falls back to range-based
// precondition synthesis.
//
// Returns the first synthesized AssumedTvUnit, or std::nullopt if no
// precondition can be derived.
std::optional<AssumedTvUnit> proposeAssume(const TvUnit &original_unit,
                                           llvm::Function &parent_src,
                                           llvm::Function &parent_tgt,
                                           llvm::LLVMContext &ctx);

} // namespace alive_tv_next
