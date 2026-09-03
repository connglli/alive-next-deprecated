// Per-unit refinement verification using Alive2.
//
// Verification executes the following steps:
// 1. Translates src_fn and tgt_fn to Alive2 IR using llvm2alive.
// 2. Constructs a Transform and checks for syntactic equality.
// 3. Preprocesses and runs TransformVerify::verify.
// 4. Converts Alive2 Errors into a UnitVerdict.
//
// When initial verification yields Unsound or FailedToProve and parent function
// contexts are provided, verifyTvUnit invokes proposeAssume. If a candidate
// precondition is proposed, all standalone assume checks are verified first.
// If every check passes, the unit is re-verified with injected assumptions.
// If the re-verification passes, the verdict upgrades to Correct. If any step
// fails, the failure verdict is returned with diagnostics.

#pragma once

#include "unit.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include "smt/smt.h"

#include <functional>
#include <string>

namespace alive_tv_next {

// Verification verdict for a single TvUnit.
struct UnitVerdict {
  std::string name;          // Unit identifier.
  bool passed = false;       // True when refinement holds.
  std::string error_message; // Populated on failure or error.
  std::string proposer_name; // Name of the proposer if assumptions were used.
  enum class Status {
    Correct,
    Unsound,
    FailedToProve,
    TypeCheckerFailed,
    Error,
    SyntacticallyEqual,
  } status = Status::Error;
};

// Callback invoked after each unit verification attempt.
using UnitProgressFn = std::function<void(const TvUnit &, const UnitVerdict &)>;

// Verifies refinement of a single TvUnit using Alive2.
//
// Preconditions: tli and smt_init must be initialized.
//
// When parent_src and parent_tgt are non-null, failed verdicts trigger assume
// proposal and retry.
//
// When dump_dir is non-empty, writes evaluated units to dump_dir/<name>.ll,
// prepending context_header if non-empty.
//
// When progress is set, invokes the callback after each verification run.
UnitVerdict verifyTvUnit(TvUnit &unit, llvm::TargetLibraryInfoWrapperPass &tli,
                         smt::smt_initializer &smt_init,
                         llvm::Function *parent_src = nullptr,
                         llvm::Function *parent_tgt = nullptr,
                         const std::string &dump_dir = "",
                         const std::string &context_header = "",
                         const UnitProgressFn &progress = {});

} // namespace alive_tv_next
