// Constructs a TvUnit function pair from a DiffRegion.
//
// Clones instructions belonging to the region into a dedicated module.
// Operands defined outside the region are unioned by SSA name and converted to
// function parameters. Operands defined within the region are rewired to their
// cloned counterparts. Each function returns the value produced by its exit
// instruction.

#pragma once

#include "tv-next/diff.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <memory>
#include <optional>
#include <string>

namespace alive_tv_next {

// A standalone function pair ready for refinement verification in Alive2.
//
// module owns the lifted IR and can be freed independently after verification.
struct TvUnit {
  std::unique_ptr<llvm::Module> module;
  llvm::Function *src_fn = nullptr; // named "src" in module
  llvm::Function *tgt_fn = nullptr; // named "tgt" in module
  std::string name;                 // identifier for diagnostics
};

// Constructs a TvUnit from a DiffRegion.
//
// Returns std::nullopt and writes a diagnostic to llvm::errs() when:
// * The region is empty.
// * Any instruction in the region is a terminator.
// * The exit instruction has void result type.
// * The exit instruction types differ between src and tgt.
// * Any non-constant external operand lacks an SSA name.
// * External operands with matching names have differing types in src and tgt.
std::optional<TvUnit> buildTvUnit(const DiffRegion &region,
                                  llvm::Module &parent_module,
                                  llvm::LLVMContext &ctx,
                                  const std::string &diag_name);

} // namespace alive_tv_next
