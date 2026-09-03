# tv-next

The `tv-next` library implements compositional translation validation for LLVM IR functions. The driver executable is defined in [../tools/alive-tv-next.cpp](../tools/alive-tv-next.cpp). Design rationale and background appear in [../IDEA.md](../IDEA.md) and [../PLAN.md](../PLAN.md).

## Verification pipeline

Translation validation decomposes paired functions into smaller verification units, proves refinement for each unit with Alive2, and aggregates the results into a function-level verdict.

The pipeline executes five steps:
* Parse input files and locate `@src` and `@tgt` functions.
* Compute an instruction-level structural diff and group differences into regions.
* Extract each difference region into an independent `TvUnit` with external values lifted to function parameters.
* Verify refinement for each unit with Alive2, using range-based assume synthesis when an unconstrained check fails.
* Check that all units pass and emit the aggregated verdict.

## Components

The library contains the following modules:
* `ir_load.h`, `ir_load.cpp`: Input loading and module validation for single-file and two-file inputs.
* `diff.h`, `diff.cpp`: Structural diffing across basic blocks, difference clustering, and commutativity isolation.
* `unit.h`, `unit.cpp`: Extraction of difference regions into standalone verification modules.
* `back.h`, `back.cpp`: Backward SSA data-flow slicing within a function.
* `range.h`, `range.cpp`: Straight-line integer range analysis and known-bits propagation.
* `proposer.h`, `proposer.cpp`: Precondition synthesis and standalone validation checks.
* `verify.h`, `verify.cpp`: Alive2 refinement dispatch and precondition retry handling.
* `compose.h`, `compose.cpp`: Aggregation of unit verdicts into a function verdict.

## Building

The library compiles as a static CMake target when `BUILD_TV` or `BUILD_LLVM_UTILS` is set.

```bash
mkdir -p build && cd build
cmake -GNinja -DCMAKE_PREFIX_PATH=$HOME/llvm/build -DBUILD_TV=1 -DCMAKE_BUILD_TYPE=Release ..
ninja alive-tv-next
```

## Running

The `alive-tv-next` driver accepts paired functions either in one file or across two files.

```bash
./alive-tv-next pre.ll post.ll
./alive-tv-next combined.srctgt.ll
./alive-tv-next --tv-verbose combined.srctgt.ll
```

Command-line flags defined in `llvm_util/cmd_args_list.h` are supported, including `--smt-to`, `--disable-undef-input`, and `--dump-cuts`.

When all units verify, the driver outputs an acceptance message and terminates with exit code 0.

When any unit fails refinement, the driver outputs failure diagnostics and terminates with exit code 1.

## Reference tests

Curated reference test cases reside in [../tests/alive-tv-next/](../tests/alive-tv-next/).
