; RUN: opt -load-pass-plugin=%llvmshlibdir/rysev_m_lab2_LLVM_IR%pluginext -passes=decompose-frem -S %s | FileCheck %s

define float @test_frem_float(float %a, float %b) {
; CHECK-LABEL: @test_frem_float(
; CHECK-NEXT:    [[DIV:%.*]] = fdiv float [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[TRUNC:%.*]] = call float @llvm.trunc.f32(float [[DIV]])
; CHECK-NEXT:    [[MUL:%.*]] = fmul float [[TRUNC]], [[B]]
; CHECK-NEXT:    [[REM:%.*]] = fsub float [[A]], [[MUL]]
; CHECK-NEXT:    ret float [[REM]]
;
  %rem = frem float %a, %b
  ret float %rem
}

define double @test_frem_double(double %a, double %b) {
; CHECK-LABEL: @test_frem_double(
; CHECK-NEXT:    [[DIV:%.*]] = fdiv double [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[TRUNC:%.*]] = call double @llvm.trunc.f64(double [[DIV]])
; CHECK-NEXT:    [[MUL:%.*]] = fmul double [[TRUNC]], [[B]]
; CHECK-NEXT:    [[REM:%.*]] = fsub double [[A]], [[MUL]]
; CHECK-NEXT:    ret double [[REM]]
;
  %rem = frem double %a, %b
  ret double %rem
}

define i32 @test_srem_i32(i32 %a, i32 %b) {
; CHECK-LABEL: @test_srem_i32(
; CHECK-NEXT:    [[DIV:%.*]] = sdiv i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[MUL:%.*]] = mul i32 [[DIV]], [[B]]
; CHECK-NEXT:    [[REM:%.*]] = sub i32 [[A]], [[MUL]]
; CHECK-NEXT:    ret i32 [[REM]]
;
  %rem = srem i32 %a, %b
  ret i32 %rem
}

define i32 @test_urem_i32(i32 %a, i32 %b) {
; CHECK-LABEL: @test_urem_i32(
; CHECK-NEXT:    [[DIV:%.*]] = udiv i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[MUL:%.*]] = mul i32 [[DIV]], [[B]]
; CHECK-NEXT:    [[REM:%.*]] = sub i32 [[A]], [[MUL]]
; CHECK-NEXT:    ret i32 [[REM]]
;
  %rem = urem i32 %a, %b
  ret i32 %rem
}

define <2 x float> @test_frem_vec2(<2 x float> %a, <2 x float> %b) {
; CHECK-LABEL: @test_frem_vec2(
; CHECK-NEXT:    [[DIV:%.*]] = fdiv <2 x float> [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[TRUNC:%.*]] = call <2 x float> @llvm.trunc.v2f32(<2 x float> [[DIV]])
; CHECK-NEXT:    [[MUL:%.*]] = fmul <2 x float> [[TRUNC]], [[B]]
; CHECK-NEXT:    [[REM:%.*]] = fsub <2 x float> [[A]], [[MUL]]
; CHECK-NEXT:    ret <2 x float> [[REM]]
;
  %rem = frem <2 x float> %a, %b
  ret <2 x float> %rem
}
