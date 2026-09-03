// Aggregates per-unit verification verdicts into a slice-level verdict.
//
// Refinement holds for the composite function when every unit verification
// succeeds and unchanged positions match identically.

#pragma once

#include "verify.h"

#include <string>
#include <vector>

namespace alive_tv_next {

// Composite verification result.
struct ComposeResult {
  bool passed = false;               // True when all units passed.
  size_t identical_positions = 0;    // Count of identical instruction pairs.
  std::vector<UnitVerdict> verdicts; // Individual unit outcomes.
  std::string error_message;         // Formatted diagnostics on failure.
};

// Aggregates unit verdicts into a ComposeResult.
//
// Sets passed to true if and only if every verdict in verdicts has passed.
// When any unit fails, error_message contains a summary of failing unit
// statuses and diagnostic messages.
ComposeResult composeVerdicts(std::vector<UnitVerdict> verdicts,
                              size_t identical_positions);

} // namespace alive_tv_next
