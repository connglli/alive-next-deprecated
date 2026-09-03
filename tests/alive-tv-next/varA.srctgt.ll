; Variant A: multiplication flag addition with overflow precondition.
;
; Instruction differences:
;   v2: mul i64 v0, v1 -> mul nsw i64 v1, v0
;
; Precondition synthesis:
;   Adding nsw introduces poison on signed overflow.
;   Operands v0 and v1 trace to sext from i32, bounding each operand in
;   [-2^31, 2^31 - 1]. The product is bounded in magnitude by 2^62 and cannot
;   overflow i64.
;   The synthesized llvm.smul.with.overflow predicate is verified through a
;   standalone check before injection into the re-verified unit.

define ptr @src(i32 %p0, i32 %p1, i64 %p2, ptr %p3) {
entry:
  %v0 = sext i32 %p0 to i64
  %v1 = sext i32 %p1 to i64
  %v2 = mul i64 %v0, %v1
  %v3 = mul i64 %v2, %p2
  %v4 = getelementptr inbounds i8, ptr %p3, i64 %v3
  ret ptr %v4
}

define ptr @tgt(i32 %p0, i32 %p1, i64 %p2, ptr %p3) {
entry:
  %v0 = sext i32 %p0 to i64
  %v1 = sext i32 %p1 to i64
  %v2 = mul nsw i64 %v1, %v0
  %v3 = mul i64 %v2, %p2
  %v4 = getelementptr inbounds i8, ptr %p3, i64 %v3
  ret ptr %v4
}
