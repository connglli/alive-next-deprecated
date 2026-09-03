// Backward SSA data-flow slicing within a single function.
//
// Traces data-flow dependencies of a named value backwards along def-use
// edges, collecting contributing instructions in program order and leaf
// function arguments sorted by argument index.
//
// The resulting instruction sequence is an acyclic graph in SSA form, suitable
// for range analysis or standalone assume-check construction.

#pragma once

#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"

#include <optional>
#include <string>
#include <vector>

namespace alive_tv_next {

struct BackwardSlice {
  std::vector<llvm::Instruction *> insts; // Program order within fn.
  std::vector<llvm::Argument *> arg_roots; // Function arguments at leaf nodes.
};

// Collects the backward slice of the instruction named name in fn.
//
// Returns std::nullopt if no instruction named name exists in fn.
std::optional<BackwardSlice> collectBackwardSlice(const std::string &name,
                                                  const llvm::Function &fn);

} // namespace alive_tv_next
