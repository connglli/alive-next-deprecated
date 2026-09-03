// alive-tv-next: compositional translation validator.
//
// Command-line driver for compositional translation validation. The driver
// loads paired source and target functions, partitions differences into
// isolated regions, verifies each region via Alive2 refinement checks,
// and composes unit verdicts. Command-line flags match alive-tv through
// llvm_util/cmd_args_list.h.

#include "tv-next/compose.h"
#include "tv-next/diff.h"
#include "tv-next/ir_load.h"
#include "tv-next/unit.h"
#include "tv-next/verify.h"

#include "cache/cache.h"
#include "llvm_util/llvm2alive.h"
#include "llvm_util/utils.h"
#include "smt/smt.h"
#include "smt/solver.h"
#include "tools/transform.h"
#include "util/version.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace std;
using namespace tools;
using namespace util;

// Inherit standard Alive2 command-line options (--smt-to,
// --disable-undef-input, --disable-poison-input, and LLVM init flags).
// cmd_args_list.h declares alive_cmdargs, out_file, out, func_names, and opt_*
// command-line options. cmd_args_def.h wires options into util::config and
// smt:: solver globals.
#define LLVM_ARGS_PREFIX ""
#define ARGS_SRC_TGT
#define ARGS_REFINEMENT
#include "llvm_util/cmd_args_list.h"

namespace {

llvm::cl::opt<std::string> opt_file1(llvm::cl::Positional,
                                     llvm::cl::desc("first_bitcode_file"),
                                     llvm::cl::Required,
                                     llvm::cl::value_desc("filename"),
                                     llvm::cl::cat(alive_cmdargs));

llvm::cl::opt<std::string> opt_file2(llvm::cl::Positional,
                                     llvm::cl::desc("[second_bitcode_file]"),
                                     llvm::cl::Optional,
                                     llvm::cl::value_desc("filename"),
                                     llvm::cl::cat(alive_cmdargs));

llvm::cl::opt<std::string>
    opt_src_fn(LLVM_ARGS_PREFIX "src-fn",
               llvm::cl::desc("Name of src function (without @)"),
               llvm::cl::cat(alive_cmdargs), llvm::cl::init("src"));

llvm::cl::opt<std::string>
    opt_tgt_fn(LLVM_ARGS_PREFIX "tgt-fn",
               llvm::cl::desc("Name of tgt function (without @)"),
               llvm::cl::cat(alive_cmdargs), llvm::cl::init("tgt"));

llvm::cl::opt<bool> opt_alive_tv_next_verbose(
    LLVM_ARGS_PREFIX "tv-verbose",
    llvm::cl::desc("Print per-cut verdicts and diff summary"),
    llvm::cl::cat(alive_cmdargs), llvm::cl::init(false));

llvm::cl::opt<std::string> opt_dump_units(
    LLVM_ARGS_PREFIX "dump-units",
    llvm::cl::desc("Write each generated TvUnit to <DIR>/<name>.ll for "
                   "inspection. Developer/debug flag."),
    llvm::cl::cat(alive_cmdargs), llvm::cl::init(""));

// Build an LLVM-comment block (each line prefixed with '; ') that locates
// the unit inside its parent @src and @tgt: every non-terminator parent
// instruction is listed with its position index 'iN', and lines that fall
// inside the unit's region are marked with '>'. The same text is used as the
// header inside dumped .ll files and as the verbose-mode preamble before
// printing the unit itself to identify the surrounding region context.
std::string buildUnitContextHeader(const std::string &unit_name,
                                   size_t src_start, size_t src_size,
                                   size_t tgt_start, size_t tgt_size,
                                   llvm::Function &parent_src,
                                   llvm::Function &parent_tgt) {
  std::string out;
  llvm::raw_string_ostream os(out);

  auto collect = [](llvm::Function &fn) {
    std::vector<llvm::Instruction *> v;
    for (auto &bb : fn)
      for (auto &I : bb)
        if (!I.isTerminator())
          v.push_back(&I);
    return v;
  };
  std::vector<llvm::Instruction *> src_insts = collect(parent_src);
  std::vector<llvm::Instruction *> tgt_insts = collect(parent_tgt);

  auto rangeStr = [](size_t start, size_t size) {
    if (size == 0)
      return std::string("(empty)");
    return "i" + std::to_string(start) + ".." + "i" +
           std::to_string(start + size - 1);
  };

  os << "; " << unit_name << ": src " << rangeStr(src_start, src_size)
     << ", tgt " << rangeStr(tgt_start, tgt_size) << "\n";

  auto dumpSide = [&](const char *label, llvm::Function &fn,
                      const std::vector<llvm::Instruction *> &insts, size_t lo,
                      size_t hi) {
    os << "; parent @" << fn.getName().str() << " (" << label << "):\n";
    for (size_t i = 0; i < insts.size(); ++i) {
      bool in_unit = i >= lo && i < hi;
      os << ";  " << (in_unit ? '>' : ' ') << " i" << i << ":";
      std::string line;
      llvm::raw_string_ostream lo_os(line);
      insts[i]->print(lo_os);
      // Instruction::print uses leading whitespace for indentation; keep it
      // so the IR reads naturally.
      os << line << "\n";
    }
  };
  dumpSide("src", parent_src, src_insts, src_start, src_start + src_size);
  dumpSide("tgt", parent_tgt, tgt_insts, tgt_start, tgt_start + tgt_size);
  return out;
}

// Verbose rendering: every unit (original, assume-check, modified, identical)
// is shown as a 5-section block (heading, separator, content, separator,
// right-aligned verdict) so reviewers can scan the verdict column and dive
// into the IR when needed.
constexpr unsigned kVerboseWidth = 60;
const std::string kVerboseSep(kVerboseWidth, '-');

std::string rightAlignedVerdict(const std::string &s) {
  if (s.size() >= kVerboseWidth)
    return " " + s;
  return std::string(kVerboseWidth - s.size(), ' ') + s;
}

std::string verdictText(const alive_tv_next::UnitVerdict &v) {
  using S = alive_tv_next::UnitVerdict::Status;
  std::string t;
  switch (v.status) {
  case S::Correct:
    t = "pass";
    break;
  case S::SyntacticallyEqual:
    t = "identical";
    break;
  case S::Unsound:
    t = "FAIL (unsound)";
    break;
  case S::FailedToProve:
    t = "FAIL (failed-to-prove)";
    break;
  case S::TypeCheckerFailed:
    t = "FAIL (type-check)";
    break;
  case S::Error:
    t = "FAIL (error)";
    break;
  }
  if (!v.proposer_name.empty())
    t += " (via " + v.proposer_name + ")";
  return t;
}

// Render the @src/@tgt pair of 'unit' in Alive2's textual Transform form,
// matching the format tools/alive-tv prints, with the leading '----\nName: ...'
// stripped because the outer block already renders the heading and separator.
// Returns an empty string if either side fails to lift.
std::string renderTransformText(const alive_tv_next::TvUnit &unit,
                                llvm::TargetLibraryInfoWrapperPass &TLI) {
  auto fn_src = llvm_util::llvm2alive(*unit.src_fn, TLI.getTLI(*unit.src_fn),
                                      /*IsSrc=*/true);
  if (!fn_src)
    return {};
  auto fn_tgt = llvm_util::llvm2alive(*unit.tgt_fn, TLI.getTLI(*unit.tgt_fn),
                                      /*IsSrc=*/false, fn_src->getGlobalVars());
  if (!fn_tgt)
    return {};
  tools::Transform t;
  t.name = unit.name;
  t.src = std::move(*fn_src);
  t.tgt = std::move(*fn_tgt);
  std::ostringstream oss;
  t.print(oss);
  std::string s = oss.str();
  // Strip leading "\n----...\n" and "Name: ...\n" lines.
  size_t pos = 0;
  while (pos < s.size() && s[pos] == '\n')
    ++pos;
  if (pos < s.size() && s[pos] == '-') {
    size_t eol = s.find('\n', pos);
    if (eol != std::string::npos)
      pos = eol + 1;
  }
  if (s.compare(pos, 6, "Name: ") == 0) {
    size_t eol = s.find('\n', pos);
    if (eol != std::string::npos)
      pos = eol + 1;
  }
  return s.substr(pos);
}

void printVerboseBlock(std::ostream &os, const std::string &heading,
                       const std::string &context_header,
                       const std::string &content,
                       const alive_tv_next::UnitVerdict &verdict) {
  os << "\n" << heading << "\n" << kVerboseSep << "\n";
  if (!context_header.empty())
    os << context_header;
  os << content;
  if (!content.empty() && content.back() != '\n')
    os << "\n";
  os << kVerboseSep << "\n"
     << rightAlignedVerdict(verdictText(verdict)) << "\n";
  if (!verdict.passed && !verdict.error_message.empty())
    os << verdict.error_message << "\n";
}

} // namespace

// Required by cmd_args_def.h cache initialization branch. The symbol is
// referenced even when Redis support is disabled, so it must be in scope.
unique_ptr<Cache> cache;

int main(int argc, char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::InitLLVM X(argc, argv);
  llvm::EnableDebugBuffering = true;
  llvm::LLVMContext Context;

  std::string usage =
      "alive-tv-next: compositional translation validator\n"
      "version: ";
  usage += util::alive_version;
  usage += R"EOF(

Usage:
  alive-tv-next [alive-tv flags...] pre.ll [post.ll]
  alive-tv-next [alive-tv flags...] combined.srctgt.ll

Input is a function pair (@src and @tgt), either as two files or one file
containing both functions. Preconditions required for rewrites are derived
and validated prior to assumption injection.
)EOF";

  llvm::cl::HideUnrelatedOptions(alive_cmdargs);
  llvm::cl::ParseCommandLineOptions(argc, argv, usage);

  // Load input functions.
  auto loaded = alive_tv_next::loadSrcTgt(opt_file1, opt_file2, opt_src_fn,
                                          opt_tgt_fn, Context);
  if (!loaded) {
    return 1;
  }

  // Wire command-line options into util::config and smt:: globals, and
  // configure output streams and report directories. Must execute after CLI
  // parsing and module loading, before code consults config or out.
#define ARGS_MODULE_VAR loaded->module1
#include "llvm_util/cmd_args_def.h"

  // Initialize Alive2 globals once with the input module's data layout.
  auto &DL = loaded->module1->getDataLayout();
  llvm::Triple targetTriple(loaded->module1->getTargetTriple());
  llvm::TargetLibraryInfoWrapperPass TLI(targetTriple);
  llvm_util::initializer llvm_util_init(*out, DL);
  smt::smt_initializer smt_init;

  // Compute difference regions. Contiguous differing positions are grouped
  // and lifted into standalone TvUnit instances.
  auto diff =
      alive_tv_next::computeDiffRegions(*loaded->src_fn, *loaded->tgt_fn);
  if (!diff) {
    *out << "alive-tv-next: could not compute difference regions\n";
    return 1;
  }
  if (opt_alive_tv_next_verbose) {
    size_t total_diffs = 0, asymmetric_regions = 0;
    for (const auto &g : diff->regions) {
      if (g.is_asymmetric)
        ++asymmetric_regions;
      else
        total_diffs += g.positions.size();
    }
    *out << "alive-tv-next: " << diff->identical_count
         << " identical position(s), " << total_diffs
         << " diff position(s) across " << diff->regions.size() << " region(s)";
    if (asymmetric_regions)
      *out << " (" << asymmetric_regions << " asymmetric)";
    *out << "\n";
  }

  // Build and verify each region as a single TvUnit.
  std::vector<alive_tv_next::UnitVerdict> verdicts;
  verdicts.reserve(diff->regions.size());

  // Collect parent non-terminators in order. Used for verbose rendering of
  // identical positions that share source and target content.
  auto collectInsts = [](llvm::Function &fn) {
    std::vector<llvm::Instruction *> v;
    for (auto &bb : fn)
      for (auto &I : bb)
        if (!I.isTerminator())
          v.push_back(&I);
    return v;
  };
  std::vector<llvm::Instruction *> src_insts = collectInsts(*loaded->src_fn);

  // Unit numbering covers every position (both identical and differing) so
  // reviewers can match 'unit@N' against the parent instruction stream.
  // The progress callback uses 'current_context_header' (rebound per region)
  // so derivative units (assume-check, range-assume) reuse their parent unit's
  // context.
  size_t unit_counter = 0;
  size_t next_src_pos = 0;
  std::string current_context_header;

  auto flushIdentical = [&](size_t up_to) {
    while (next_src_pos < up_to && next_src_pos < src_insts.size()) {
      ++unit_counter;
      if (opt_alive_tv_next_verbose) {
        std::string content;
        llvm::raw_string_ostream cos(content);
        src_insts[next_src_pos]->print(cos);
        cos << "\n";
        alive_tv_next::UnitVerdict v;
        v.status = alive_tv_next::UnitVerdict::Status::SyntacticallyEqual;
        v.passed = true;
        printVerboseBlock(*out, "unit@" + std::to_string(unit_counter),
                          /*context_header=*/"", content, v);
      }
      ++next_src_pos;
    }
    next_src_pos = std::max(next_src_pos, up_to);
  };

  alive_tv_next::UnitProgressFn progress =
      [&](const alive_tv_next::TvUnit &u, const alive_tv_next::UnitVerdict &v) {
        if (!opt_alive_tv_next_verbose)
          return;
        printVerboseBlock(*out, u.name, current_context_header,
                          renderTransformText(u, TLI), v);
      };

  for (const alive_tv_next::DiffRegion &region : diff->regions) {
    // Unify symmetric and asymmetric region indexing into one (start, size)
    // pair per side. The textual src/tgt position range only appears in
    // the per-unit comment header: the unit name itself is just an index.
    size_t src_start, src_size, tgt_start, tgt_size;
    if (region.is_asymmetric) {
      src_start = region.src_start_idx;
      src_size = region.src_region.size();
      tgt_start = region.tgt_start_idx;
      tgt_size = region.tgt_region.size();
    } else {
      src_start = tgt_start = region.positions.front().inst_idx;
      src_size = tgt_size = region.positions.size();
    }

    // Emit identical positions preceding this region.
    flushIdentical(src_start);
    next_src_pos = src_start + src_size;

    ++unit_counter;
    std::string diag_name = "unit@" + std::to_string(unit_counter);

    auto unit = alive_tv_next::buildTvUnit(region, *loaded->module1, Context,
                                           diag_name);
    if (!unit) {
      // Build failure: surface as a failed verdict so verification fails.
      alive_tv_next::UnitVerdict v;
      v.name = diag_name;
      v.passed = false;
      v.status = alive_tv_next::UnitVerdict::Status::Error;
      v.error_message = "could not lift region into a TvUnit";
      if (opt_alive_tv_next_verbose)
        printVerboseBlock(*out, diag_name, /*context_header=*/"",
                          /*content=*/"", v);
      verdicts.push_back(std::move(v));
      continue;
    }

    current_context_header =
        buildUnitContextHeader(diag_name, src_start, src_size, tgt_start,
                               tgt_size, *loaded->src_fn, *loaded->tgt_fn);

    auto verdict = alive_tv_next::verifyTvUnit(
        *unit, TLI, smt_init, loaded->src_fn, loaded->tgt_fn, opt_dump_units,
        current_context_header, progress);
    verdicts.push_back(std::move(verdict));
  }

  // Emit identical positions following the last region.
  flushIdentical(src_insts.size());

  // Aggregate unit verdicts.
  auto result = alive_tv_next::composeVerdicts(std::move(verdicts),
                                               diff->identical_count);

  if (result.passed) {
    *out << "Transformation seems to be correct!\n";
    return 0;
  }
  *out << "Transformation doesn't verify\n";
  if (!result.error_message.empty())
    *out << result.error_message;
  return 1;
}
