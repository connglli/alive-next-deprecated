; Example 3: asymmetric vectorization.
;
; Instruction differences:
;   v0: sdiv exact i64 _, 8 -> ashr exact i64 _, 3
;   v3, v4, v6: scalar operations fused into vector operations
;     pre:  3 scalar instructions (sub, sdiv exact, sdiv exact)
;     post: 7 vector instructions (3 inserts, vector sub, vector sdiv,
;           2 extracts)
;   v5: add nsw a, b -> add nsw b, a
;
; Verification:
;   v0 and v5 lift into single-instruction units.
;   The vector region lifts into an asymmetric multi-side TvUnit verified
;   directly through Alive2 refinement queries.

define i64 @src(i64 %p0, i64 %p1, i64 %p2, i64 %p3, i64 %p4, i64 %p5) {
entry:
  %v0 = sdiv exact i64 %p0, 8
  %v1 = sub nsw i64 %v0, %p1
  %v2 = mul nsw i64 %p2, %v1
  %v3 = sub i64 %p3, %p4
  %v4 = sdiv exact i64 %v3, 24
  %v5 = add nsw i64 %v2, %v4
  %v6 = sdiv exact i64 %p5, 24
  %v7 = add nsw i64 %v5, %v6
  ret i64 %v7
}

define i64 @tgt(i64 %p0, i64 %p1, i64 %p2, i64 %p3, i64 %p4, i64 %p5) {
entry:
  %v0 = ashr exact i64 %p0, 3
  %v1 = sub nsw i64 %v0, %p1
  %v2 = mul nsw i64 %p2, %v1
  %0 = insertelement <2 x i64> poison, i64 %p3, i64 0
  %1 = insertelement <2 x i64> %0, i64 %p5, i64 1
  %2 = insertelement <2 x i64> <i64 poison, i64 0>, i64 %p4, i64 0
  %3 = sub <2 x i64> %1, %2
  %4 = sdiv exact <2 x i64> %3, splat (i64 24)
  %5 = extractelement <2 x i64> %4, i64 0
  %v5 = add nsw i64 %5, %v2
  %6 = extractelement <2 x i64> %4, i64 1
  %v7 = add nsw i64 %v5, %6
  ret i64 %v7
}
