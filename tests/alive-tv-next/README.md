# Alive-tv-next reference tests

Curated test cases for compositional translation validation in [../../tv-next/](../../tv-next/). Each file defines a source function `@src` and a target function `@tgt` exercising difference extraction, unit lifting, and precondition synthesis.

## Test mapping

| File | Transformation category | Verification mechanism |
|---|---|---|
| `e1.srctgt.ll` | Single-instruction substitution | Structural diff, single-instruction unit lifting |
| `e1alt.srctgt.ll` | Single-instruction substitution | Add commutativity, identity cast matching |
| `e2.srctgt.ll` | Multi-instruction sequence | Contiguous diff grouping, commutativity splitting |
| `varB.srctgt.ll` | Multi-instruction sequence | Multi-instruction grouping, strength reduction |
| `varA.srctgt.ll` | Context-dependent rewrite | Arithmetic extension analysis, overflow flag assume synthesis |
| `e4.srctgt.ll` | Context-dependent rewrite | Backward slice range analysis, freeze elimination |
| `e3.srctgt.ll` | Asymmetric transformation | Asymmetric prefix diff, multi-side vector region lifting |

## Execution with alive-tv-next

The test cases verify through the `alive-tv-next` driver:

```bash
./build/alive-tv-next --disable-undef-input --smt-to=60000 tests/alive-tv-next/e1.srctgt.ll
```

Whole-function queries on these test cases exceed SMT solver timeouts in whole-function checkers. The compositional pipeline decomposes each function into localized units verified through Alive2 refinement queries.

## Test conventions

* Each test file defines a pre-transformation function `@src` and a post-transformation function `@tgt`.
* Header comments document instruction differences and derived preconditions.
* Function attributes from optimization passes are preserved from the source corpus.

## Provenance

Test cases originate from optimization pipelines described in [../../IDEA.md](../../IDEA.md) and [../../PLAN.md](../../PLAN.md). Cases `e1`, `e1alt`, `e2`, `e3`, `varA`, and `varB` derive from compiler corpus extracts. Case `e4` is a minimal representation of freeze elimination guarded by bitmask ranges.
