; Example 2: multi-instruction sequence rewrite.
;
; Instruction differences:
;   v1: sdiv exact i64 _, 8 -> ashr exact i64 _, 3
;   v3, v4: zext and sub nsw -> sext and add nsw
;     pre:  v3 = zext i1 v2 to i64 ; v4 = sub nsw v1, v3
;     post: v3.neg = sext i1 v2 to i64 ; v4 = add nsw v1, v3.neg
;     Equivalence: sub nsw x, (zext b) is equivalent to add nsw x, (sext b)
;     for b: i1 and x: i64.
;   v5: mul nsw i64 a, b -> mul nsw i64 b, a
;
; Verification:
;   v1 is lifted as a single-instruction unit.
;   v3 and v4 are grouped into a contiguous multi-instruction TvUnit.
;   v5 matches commutativity and forms an isolated single-instruction TvUnit.

define i64 @src(i64 %p0, i64 %p1, ptr %p2, i64 %p3, i64 %p4) {
entry:
  %v0 = sub i64 %p0, %p1
  %v1 = sdiv exact i64 %v0, 8
  %v2 = icmp ne ptr %p2, null
  %v3 = zext i1 %v2 to i64
  %v4 = sub nsw i64 %v1, %v3
  %v5 = mul nsw i64 %p3, %v4
  %v6 = add nsw i64 %v5, %p4
  ret i64 %v6
}

define i64 @tgt(i64 %p0, i64 %p1, ptr %p2, i64 %p3, i64 %p4) {
entry:
  %v0 = sub i64 %p0, %p1
  %v1 = ashr exact i64 %v0, 3
  %v2 = icmp ne ptr %p2, null
  %v3.neg = sext i1 %v2 to i64
  %v4 = add nsw i64 %v1, %v3.neg
  %v5 = mul nsw i64 %v4, %p3
  %v6 = add nsw i64 %v5, %p4
  ret i64 %v6
}
