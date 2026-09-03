# Compositional verification with an oracle

This document specifies the design rationale for compositional translation validation in [./tv-next/](./tv-next/). Implementation phases, milestones, and test suites are specified in [./PLAN.md](./PLAN.md).

## Problem statement

Alive2 verifies LLVM optimizations by encoding a refinement check as an SMT query and dispatching it to Z3. Three factors constrain refinement solving:

* Solver execution time scales super-linearly with formula size. Functions with nested nonlinear operations on 64-bit integers often exceed standard solver timeouts because the formula size exceeds solver capacity.
* Whole-function query framing couples unrelated instructions into a single monolithic SMT query. Solver tractability requires isolating localized arithmetic sequences or branch paths.
* Features outside the formal model, such as target-specific intrinsics or unsupported vector types, yield unknown verdicts rather than timeouts.

Compositional verification addresses whole-function query framing and formula size limits by decomposing functions into smaller verification units.

## Compositional verification framework

Verification decomposes a function pair into localized units structured around pre- and postconditions:

* The function is partitioned into difference regions whose SMT queries remain tractable.
* Each region is verified under an entry precondition P, yielding an exit postcondition Q.
* Units compose transitively: if refinement holds for unit A and unit B, refinement holds for their composition.
* The function-level verdict is the logical conjunction of all unit verdicts.

Applying this framework to LLVM IR requires addressing two design requirements:

* Region boundary selection: Cut points must align with data-flow boundaries to limit parameter interfaces.
* Precondition synthesis: Context-dependent rewrites require preconditions over external inputs, such as value intervals, non-zero conditions, or absence of poison.

## Precondition synthesis and validation

Heuristic proposal mechanisms generate candidate regions and preconditions, while the SMT solver validates every proposal:

Candidate preconditions and region boundaries may be proposed by static analyses, pattern matchers, or external models.

All proposals are untrusted. Every candidate precondition is verified by a standalone refinement query before being injected into a unit. Unsound or unprovable proposals fail the standalone check and are discarded without affecting verification soundness. Soundness is established exclusively by the SMT solver.

## Complexity and soundness requirements

Partitioning transforms whole-function queries into independent sub-problems:

* Total solver workload scales linearly with the number of units rather than exponentially with function size.
* Local invariants suffice for most context-dependent rewrites. Bounded ranges from bitwise masks, absence of overflow from sign extension, and non-zero divisors can be derived from local backward slices.

Soundness of composition depends on three invariants:

* Operand dependency consistency: Every input to a downstream unit must be identical to its pre-transformation counterpart or proven equivalent by a preceding unit.
* Lifting fidelity: Operands defined outside the region must be lifted with types and definedness semantics matching the parent function.
* Precondition validity: Any assumption injected into a unit must hold unconditionally under the inputs of the containing function.

## Target transformation classes

The test cases in [./PLAN.md](./PLAN.md) span four transformation classes of increasing structural complexity:

* Single-instruction replacements: Instruction differences pair one-to-one, such as exact division by a power of two replaced with an exact arithmetic shift right, or operand commutativity.
* Multi-instruction sequences: Consecutive differences span multiple dependent instructions, requiring multi-instruction region grouping to capture the rewrite.
* Unequal-count sequences: Transformations alter instruction counts across source and target, such as vectorization where multiple scalar operations map to vector instructions.
* Context-dependent rewrites: Transformations that require preconditions to hold, such as proving absence of overflow before adding nsw flags, or proving shift amounts remain within bitwidth before eliminating a freeze instruction.

## Scope boundaries

| In scope | Out of scope |
|---|---|
| Structural diff, region lifting, and per-unit Alive2 dispatch | Subprocess-driven invocation of external tools |
| Multi-line and asymmetric difference regions | Inter-procedural transformations |
| Range-based and pattern-based precondition synthesis | Unchecked external preconditions |
| Refinement verification via Alive2 C++ APIs | Modifying Alive2 SMT encoding or memory models |

Input functions are provided as paired files or single files with `@src` and `@tgt` definitions. Preconditions are synthesized from the surrounding IR rather than accepted from external user inputs. Synthesized preconditions are injected into unit modules using the `@llvm.assume(i1)` intrinsic.

## Reference documentation

Detailed test cases, component interfaces, and verification workflows are documented in [./PLAN.md](./PLAN.md).
