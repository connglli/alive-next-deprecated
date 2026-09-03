# Compositional verification specification and roadmap

This document specifies the verification architecture, test cases, and capability stages for the compositional translation validation system in [./tv-next/](./tv-next/). The design rationale is described in [./IDEA.md](./IDEA.md).

## Verification objectives

The translation validator verifies functional equivalence for LLVM optimizations by decomposing functions into localized difference regions.

The validation pipeline satisfies two operational criteria:
* Every reference test case in [./tests/alive-tv-next/](./tests/alive-tv-next/) verifies end-to-end without whole-function SMT timeouts.
* Verification soundness rests exclusively on SMT solver checks dispatched through Alive2 APIs. All auxiliary analyses and proposal heuristics are treated as untrusted generators.

## Reference test suite

The test suite in [./tests/alive-tv-next/](./tests/alive-tv-next/) exercises four classes of transformation complexity:

| Test file | Transformation category | Core mechanism |
|---|---|---|
| `e1.srctgt.ll` | Single-instruction substitution | Structural diff, single-instruction unit lifting |
| `e1alt.srctgt.ll` | Single-instruction substitution | Add commutativity, identity cast matching |
| `e2.srctgt.ll` | Multi-instruction sequence | Contiguous diff grouping, commutativity splitting |
| `varB.srctgt.ll` | Multi-instruction sequence | Multi-instruction grouping, strength reduction |
| `varA.srctgt.ll` | Context-dependent rewrite | Arithmetic extension analysis, overflow flag assume synthesis |
| `e4.srctgt.ll` | Context-dependent rewrite | Backward slice range analysis, freeze elimination |
| `e3.srctgt.ll` | Asymmetric transformation | Asymmetric prefix diff, multi-side vector region lifting |

### Reference example: `e1alt.srctgt.ll`

The `e1alt.srctgt.ll` test case pairs integer arithmetic transformations with an identity `ptrtoint` cast and operand commutativity.

Source function:

```llvm
define i64 @src(ptr %p0, i64 %p1, i64 %p2, i64 %p3, i64 %p4, i64 %p5) #0 {
entry:
  %v0 = ptrtoint ptr %p0 to i64
  %v1 = sub i64 %p1, %v0
  %v2 = sdiv exact i64 %v1, 8
  %v3 = sub nsw i64 %v2, %p2
  %v4 = mul nsw i64 %p3, %v3
  %v5 = sub i64 %p4, %p5
  %v6 = sdiv exact i64 %v5, 16
  %v7 = add nsw i64 %v4, %v6
  ret i64 %v7
}
```

Target function:

```llvm
define i64 @tgt(ptr %p0, i64 %p1, i64 %p2, i64 %p3, i64 %p4, i64 %p5) #0 {
entry:
  %v0 = ptrtoint ptr %p0 to i64
  %v1 = sub i64 %p1, %v0
  %v2 = ashr exact i64 %v1, 3
  %v3 = sub nsw i64 %v2, %p2
  %v4 = mul nsw i64 %v3, %p3
  %v5 = sub i64 %p4, %p5
  %v6 = ashr exact i64 %v5, 4
  %v7 = add nsw i64 %v6, %v4
  ret i64 %v7
}
```

Instruction differences:

| Value | Source instruction | Target instruction | Classification |
|---|---|---|---|
| `%v2` | `sdiv exact i64 %v1, 8` | `ashr exact i64 %v1, 3` | Exact division power-of-two reduction |
| `%v4` | `mul nsw i64 %p3, %v3` | `mul nsw i64 %v3, %p3` | Multiplication commutativity |
| `%v6` | `sdiv exact i64 %v5, 16` | `ashr exact i64 %v5, 4` | Exact division power-of-two reduction |
| `%v7` | `add nsw i64 %v4, %v6` | `add nsw i64 %v6, %v4` | Addition commutativity |

The `ptrtoint` instruction producing `%v0` is identical in source and target, satisfying the identity-matching fast path without unit extraction.

## Verification architecture

The verification pipeline executes in process using Alive2 C++ APIs:

```
[pre.ll, post.ll] or [combined.srctgt.ll]
       |
       v
   LLVM IR parsing (LLVM C++ API)
       |
       v
   llvm2alive (Alive2 API) -> IR::Function for src and tgt
       |
       v
   Diff and region grouping (single-instruction, multi-instruction, asymmetric)
       |
       +--> Unconstrained unit verification (Alive2 TransformVerify)
       |
       +--> Precondition synthesis on verification failure
       |      |
       |      +--> Static range analysis (computeRanges)
       |      |
       |      +--> Flag obligation synthesis (no-violation predicates)
       |      |
       |      +--> Standalone assume verification (validates preconditions)
       |
       v
   Unit re-verification with injected assumptions
       |
       v
   Composition checker (dependency validation and identity matching)
       |
       v
   Function verdict
```

Individual difference regions are lifted into standalone `TvUnit` instances. Verification is dispatched directly to `tools::TransformVerify::verify()` without spawning subprocesses or re-parsing IR.

## Capability stages

### Single-instruction region verification

The diff engine pairs instructions between single-basic-block functions. Instruction pairs with identical text modulo metadata are classified as unchanged. Each differing pair is lifted into a `TvUnit` where external inputs become function parameters and the differing instruction produces the function return value.

### Multi-instruction region grouping

Adjacent instruction differences whose def-use chains interlock are grouped into contiguous multi-instruction regions. Instructions matching commutativity patterns (identical opcode, identical operand multiset, and identical result name) are isolated into dedicated single-instruction regions to prevent over-grouping.

### Precondition synthesis

When unconstrained refinement fails, candidate preconditions are derived from the containing functions:

* A modified unit is constructed with `@llvm.assume` calls injected prior to the transformed instructions.
* Standalone assume-check units verify that each synthesized precondition holds unconditionally under the arguments of the parent function.
* The modified unit is accepted only when all standalone checks pass and the re-verified unit satisfies refinement.

### Asymmetric region handling

Functions with unequal instruction counts are partitioned by walking matching prefixes in lockstep. Textually identical instructions and same-named instruction pairs are extracted first. The remaining differing instructions on both sides are gathered into an asymmetric `DiffRegion` whose return values share identical types.

### Range analysis helper

The `computeRanges` analysis performs a forward data-flow pass over straight-line integer instructions within a function slice. The analysis derives unsigned intervals, signed intervals, and known bitmasks for arithmetic, logical, shift, and cast instructions.

The range analysis is untrusted. Inferred value bounds and poison-free invariants are converted into explicit boolean predicates and verified through standalone refinement queries before assumption injection.

### Oracle proposal interface

External oracles or heuristic models may propose candidate cut boundaries and precondition expressions. All oracle proposals are verified through standalone Alive2 checks prior to acceptance.

## Repository structure

Components of the compositional translation validator reside in the following files:

```
tv-next/
  CMakeLists.txt      # Builds the static library tv-next
  ir_load.h, .cpp     # Module loading and signature validation
  diff.h, .cpp        # Structural diffing, region grouping, commutativity isolation
  unit.h, .cpp        # TvUnit extraction, parameter lifting, SSA rewiring
  back.h, .cpp        # Backward data-flow slicing
  range.h, .cpp       # Straight-line range analysis and known-bits propagation
  proposer.h, .cpp    # Precondition synthesis and standalone check construction
  verify.h, .cpp      # Alive2 refinement dispatch and retry loop
  compose.h, .cpp     # Verdict aggregation and identity validation
tools/
  alive-tv-next.cpp   # Executable entry point and command-line driver
tests/
  alive-tv-next/      # Curated reference test cases
```

## Command line interface

The `alive-tv-next` driver accepts paired functions in two files or a single combined file:

```bash
alive-tv-next [flags] pre.ll post.ll
alive-tv-next [flags] combined.srctgt.ll
```

### Supported flags

Command-line flags defined in `llvm_util/cmd_args_list.h` are supported directly:

| Flag | Purpose |
|---|---|
| `--smt-to=N` | SMT solver timeout in milliseconds |
| `--disable-undef-input` | Treat function arguments as concrete values |
| `--disable-poison-input` | Treat function arguments as poison-free values |
| `--src-fn=NAME` | Name of the source function |
| `--tgt-fn=NAME` | Name of the target function |
| `--dump-cuts=DIR` | Serialize generated units to LLVM IR files in DIR |
| `--tv-verbose` | Output detailed progress for each unit and assume check |

## Alive2 API integration

The `tv-next` library links directly against Alive2 source components:

| Symbol | Definition file | Function in compositional pipeline |
|---|---|---|
| `llvm_util::llvm2alive` | `llvm_util/llvm2alive.h` | Translates LLVM Function to Alive2 IR::Function |
| `tools::Transform` | `tools/transform.h` | Container for paired src and tgt functions |
| `tools::TransformVerify::verify` | `tools/transformverify.h` | Executes refinement check and returns util::Errors |
| `smt::smt_initializer` | `smt/smt.h` | Manages solver lifecycle and context resets |

## Soundness invariants and risk analysis

Compositional refinement guarantees whole-function refinement when three invariants are maintained:

* Operand-chain consistency: Downstream units must consume values identical to pre-transformation values or values verified as equivalent by prior units.
* Assume-scoping validity: Assumptions verified for a unit are valid only within the dominance scope of the supporting program point.
* Strict identity matching: Unchanged instructions must match textually modulo metadata and canonical SSA renaming.

Failure to uphold any invariant invalidates the transitivity of refinement. Auxiliary analyses such as range computation are strictly separated from soundness decisions: if an auxiliary analysis derives an incorrect bound, the standalone assume check fails, preventing unsound assumption injection.
