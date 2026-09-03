// Loads paired functions for translation validation from one or two LLVM files.
//
// Supported input forms:
// * Single-file form: file1 contains both functions; file2 is empty.
// * Paired-file form: file1 contains src_fn_name; file2 contains tgt_fn_name.
//
// Verification of modules is performed with llvm::verifyModule. Diagnostics
// are written to llvm::errs().

#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <memory>
#include <optional>
#include <string>

namespace alive_tv_next {

// Paired functions and their owning modules.
//
// module1 owns the IR for src_fn. When file2 is provided, module2 owns the IR
// for tgt_fn. When file2 is empty, module2 is null and module1 owns both
// functions.
struct LoadedSrcTgt {
  std::unique_ptr<llvm::Module> module1;
  std::unique_ptr<llvm::Module> module2; // null = single-file form
  llvm::Function *src_fn = nullptr;
  llvm::Function *tgt_fn = nullptr;
};

// Loads modules from disk, validates IR with llvm::verifyModule, and resolves
// the requested functions. file2 is empty for single-file form.
//
// Returns std::nullopt and writes a diagnostic to llvm::errs() when:
// * File reading or IR parsing fails.
// * llvm::verifyModule reports validation errors.
// * Either requested function name cannot be found.
// * src_fn and tgt_fn have differing function signatures.
std::optional<LoadedSrcTgt> loadSrcTgt(const std::string &file1,
                                       const std::string &file2,
                                       const std::string &src_fn_name,
                                       const std::string &tgt_fn_name,
                                       llvm::LLVMContext &ctx);

} // namespace alive_tv_next
