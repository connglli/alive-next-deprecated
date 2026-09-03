; Example 4: freeze elimination with range precondition.
;
; Instruction differences:
;   v2: freeze instruction eliminated; v3 reads v1 directly.
;
; Precondition synthesis:
;   shl i64 %p1, %v0 is poison-free when %v0 < 64.
;   Static range analysis over %v0 = and i64 %p0, 31 establishes %v0 in [0, 31].
;   The synthesized predicate icmp ult i64 %v0, 64 is verified through a
;   standalone check before injection into the re-verified unit.

define i64 @src(i64 %p0, i64 %p1, i64 %p2) {
entry:
  %v0 = and i64 %p0, 31
  %v1 = shl i64 %p1, %v0
  %v2 = freeze i64 %v1
  %v3 = mul nsw i64 %v2, %p2
  ret i64 %v3
}

define i64 @tgt(i64 %p0, i64 %p1, i64 %p2) {
entry:
  %v0 = and i64 %p0, 31
  %v1 = shl i64 %p1, %v0
  %v3 = mul nsw i64 %v1, %p2
  ret i64 %v3
}
