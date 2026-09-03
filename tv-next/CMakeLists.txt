# tv-next: core library for compositional translation validation.
#
# The library provides diffing, unit extraction, backward slicing, range
# analysis, assume synthesis, verification dispatch, and composition
# primitives consumed by the alive-tv-next driver in tools/alive-tv-next.cpp.
#
# Registration occurs in the root CMakeLists.txt when BUILD_LLVM_UTILS or
# BUILD_TV is enabled. Include paths resolve relative to the repository root.

set(TV_NEXT_SRCS
  ir_load.cpp
  diff.cpp
  unit.cpp
  back.cpp
  range.cpp
  proposer.cpp
  verify.cpp
  compose.cpp
)

add_library(tv-next STATIC ${TV_NEXT_SRCS})
target_compile_options(tv-next PRIVATE -fexceptions)
