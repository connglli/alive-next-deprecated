; Example 1': single-instruction substitution with identity cast.
;
; Instruction differences:
;   v2: sdiv exact i64 _, 8  -> ashr exact i64 _, 3
;   v4: mul nsw i64 a, b     -> mul nsw i64 b, a
;   v6: sdiv exact i64 _, 16 -> ashr exact i64 _, 4
;   v7: add nsw i64 a, b     -> add nsw i64 b, a
;
; Verification:
;   The ptrtoint at v0 matches textually and satisfies identity matching.
;   The four differences lift into single-instruction TvUnit instances.

define i64 @src(ptr %p0, i64 %p1, i64 %p2, i64 %p3, i64 %p4, i64 %p5) {
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

define i64 @tgt(ptr %p0, i64 %p1, i64 %p2, i64 %p3, i64 %p4, i64 %p5) {
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
