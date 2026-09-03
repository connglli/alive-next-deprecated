; Variant B: multi-instruction strength reduction and flag relaxation.
;
; Instruction differences:
;   v0, v1: add nsw and mul nsw -> shl and add
;     pre:  v0 = add nsw p0, 1 ; v1 = mul nsw 4, v0
;     post: v0 = shl p0, 2     ; v1 = add v0, 4
;   v3: mul nsw 4, x -> shl nsw x, 2
;
; Verification:
;   v0 and v1 are grouped into a multi-instruction TvUnit.
;   v3 is lifted into a single-instruction TvUnit.

define i64 @src(i64 %p0, i64 %p1, i64 %p2) {
entry:
  %v0 = add nsw i64 %p0, 1
  %v1 = mul nsw i64 4, %v0
  %v2 = sdiv i64 %p1, %v1
  %v3 = mul nsw i64 4, %v2
  %v4 = sub nsw i64 %p2, %v3
  ret i64 %v4
}

define i64 @tgt(i64 %p0, i64 %p1, i64 %p2) {
entry:
  %v0 = shl i64 %p0, 2
  %v1 = add i64 %v0, 4
  %v2 = sdiv i64 %p1, %v1
  %v3 = shl nsw i64 %v2, 2
  %v4 = sub nsw i64 %p2, %v3
  ret i64 %v4
}
