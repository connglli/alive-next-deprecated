#include "tv-next/diff.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <string>
#include <utility>

namespace alive_tv_next {

namespace {

// Detects binary operations where operands are swapped while preserving the
// opcode, operand multiset, and result name. Isolating commutativity rewrites
// into separate regions limits SMT formula size in downstream verification.
//
// Restricted to binary operators with exactly two operands.
bool isLikelyCommutativity(const llvm::Instruction *src,
                           const llvm::Instruction *tgt) {
  if (src->getOpcode() != tgt->getOpcode())
    return false;
  if (src->getNumOperands() != 2 || tgt->getNumOperands() != 2)
    return false;

  // Only split when result names match. If a result was renamed, retain the
  // instruction in its current group.
  if (src->getName() != tgt->getName())
    return false;

  // Build operand multisets by textual representation.
  auto operandKey = [](const llvm::Value *V) -> std::string {
    if (auto *C = llvm::dyn_cast<llvm::Constant>(V)) {
      std::string s;
      llvm::raw_string_ostream os(s);
      C->printAsOperand(os, /*PrintType=*/false);
      return s;
    }
    return V->hasName() ? V->getName().str() : std::string{};
  };
  std::multiset<std::string> src_ops, tgt_ops;
  for (const llvm::Use &U : src->operands())
    src_ops.insert(operandKey(U.get()));
  for (const llvm::Use &U : tgt->operands())
    tgt_ops.insert(operandKey(U.get()));
  return src_ops == tgt_ops;
}

// Print an instruction to a string, omitting trailing metadata. The
// default Instruction::print() includes metadata like `!dbg !12`; for the
// diff we only care about the operation and its operands.
std::string instAsText(const llvm::Instruction &I) {
  std::string s;
  llvm::raw_string_ostream os(s);
  I.print(os, /*IsForDebug=*/false);

  // Strip trailing metadata: ", !dbg !..." or similar. Trailing-metadata
  // segments start with ", !"; LLVM never emits operand syntax that
  // collides with this, so a single cut is safe.
  auto pos = s.find(", !");
  if (pos != std::string::npos)
    s.resize(pos);

  // Strip leading whitespace from print()'s indentation.
  size_t lead = 0;
  while (lead < s.size() && (s[lead] == ' ' || s[lead] == '\t'))
    ++lead;
  if (lead > 0)
    s.erase(0, lead);

  return s;
}

} // namespace

std::optional<DiffResult> computeDiffRegions(llvm::Function &src,
                                             llvm::Function &tgt) {
  if (src.size() != 1 || tgt.size() != 1) {
    llvm::errs() << "alive-tv-next: requires single-BB functions; "
                 << "@" << src.getName() << " has " << src.size() << " BBs, "
                 << "@" << tgt.getName() << " has " << tgt.size() << " BBs\n";
    return std::nullopt;
  }

  llvm::BasicBlock &src_bb = src.front();
  llvm::BasicBlock &tgt_bb = tgt.front();

  // Collect non-terminator instructions on each side.
  std::vector<llvm::Instruction *> src_insts, tgt_insts;
  for (llvm::Instruction &I : src_bb)
    if (!I.isTerminator())
      src_insts.push_back(&I);
  for (llvm::Instruction &I : tgt_bb)
    if (!I.isTerminator())
      tgt_insts.push_back(&I);

  DiffResult result;

  if (src_insts.size() == tgt_insts.size()) {
    // Position-by-position pairing for matching instruction counts.
    DiffRegion current;
    auto flush_group = [&]() {
      if (!current.positions.empty()) {
        result.regions.push_back(std::move(current));
        current = DiffRegion{};
      }
    };

    for (size_t i = 0; i < src_insts.size(); ++i) {
      if (instAsText(*src_insts[i]) == instAsText(*tgt_insts[i])) {
        ++result.identical_count;
        flush_group();
        continue;
      }

      // Isolate commutativity rewrites into single-instruction regions.
      if (isLikelyCommutativity(src_insts[i], tgt_insts[i])) {
        flush_group();
        current.positions.push_back(
            DiffPosition{i, src_insts[i], tgt_insts[i]});
        flush_group();
        continue;
      }

      current.positions.push_back(DiffPosition{i, src_insts[i], tgt_insts[i]});
    }
    flush_group();

  } else {
    // Pairing and tail aggregation for mismatched instruction counts.
    //
    // Walk src and tgt in lockstep while pairs are structurally aligned:
    // * Textually identical pairs increment identical_count.
    // * Pairs sharing the same non-empty SSA name are grouped into a symmetric
    //   region, with commutativity splitting applied.
    // The first pair breaking both conditions begins an asymmetric region
    // containing all remaining instructions on both sides.
    DiffRegion paired_current;
    auto flush_paired = [&]() {
      if (!paired_current.positions.empty()) {
        result.regions.push_back(std::move(paired_current));
        paired_current = DiffRegion{};
      }
    };

    size_t si = 0, ti = 0;
    for (; si < src_insts.size() && ti < tgt_insts.size(); ++si, ++ti) {
      llvm::Instruction *s = src_insts[si];
      llvm::Instruction *t = tgt_insts[ti];

      if (instAsText(*s) == instAsText(*t)) {
        ++result.identical_count;
        flush_paired();
        continue;
      }

      // Semantic pairing by identical SSA result name.
      if (s->hasName() && t->hasName() && s->getName() == t->getName()) {
        if (isLikelyCommutativity(s, t)) {
          flush_paired();
          paired_current.positions.push_back(DiffPosition{si, s, t});
          flush_paired();
        } else {
          paired_current.positions.push_back(DiffPosition{si, s, t});
        }
        continue;
      }

      // Structural divergence: start asymmetric region from here.
      break;
    }
    flush_paired();

    // Remaining src[si..] and tgt[ti..] form the asymmetric region.
    if (si < src_insts.size() || ti < tgt_insts.size()) {
      DiffRegion ms;
      ms.is_asymmetric = true;
      ms.src_start_idx = si;
      ms.tgt_start_idx = ti;
      for (size_t i = si; i < src_insts.size(); ++i)
        ms.src_region.push_back(src_insts[i]);
      for (size_t i = ti; i < tgt_insts.size(); ++i)
        ms.tgt_region.push_back(tgt_insts[i]);
      result.regions.push_back(std::move(ms));
    }
  }

  // Terminators must match. Disagreement indicates differing control-flow
  // exits and prevents verification.
  llvm::Instruction *src_term = src_bb.getTerminator();
  llvm::Instruction *tgt_term = tgt_bb.getTerminator();
  if (src_term && tgt_term && instAsText(*src_term) != instAsText(*tgt_term)) {
    llvm::errs() << "alive-tv-next: terminator differs between @"
                 << src.getName() << " and @" << tgt.getName()
                 << ":\n  src: " << instAsText(*src_term)
                 << "\n  tgt: " << instAsText(*tgt_term) << "\n";
    return std::nullopt;
  }

  return result;
}

} // namespace alive_tv_next
