// RUN: mlir-opt --load-pass-plugin=%mlir_lib_dir/rysev_m_lab04_MLIR%shlibext --pass-pipeline="builtin.module(trace-condition-pass)" %s | FileCheck %s

// -----

// CHECK-LABEL: func @simple_scf_if
func.func @simple_scf_if(%cond : i1) {
  scf.if %cond {
    // CHECK: call @trace_condition_then_begin()
    // CHECK-NEXT: call @trace_condition_then_end()
    scf.yield
  } else {
    // CHECK: call @trace_condition_else_begin()
    // CHECK-NEXT: call @trace_condition_else_end()
    scf.yield
  }
  func.return
}

// -----

// CHECK-LABEL: func @scf_if_without_else
func.func @scf_if_without_else(%cond : i1) {
  scf.if %cond {
    // CHECK: call @trace_condition_then_begin()
    // CHECK-NEXT: call @trace_condition_then_end()
    scf.yield
  }
  func.return
}

// =============================================================================
// Исправленный affine.if с использованием #set
// =============================================================================

#my_set = affine_set<(d0, d1) : (d0 >= 0, d1 >= 0)>

// CHECK-LABEL: func @affine_if
func.func @affine_if(%arg0 : index, %arg1 : index) {
  affine.if #my_set(%arg0, %arg1) {
    // CHECK: call @trace_condition_then_begin()
    // CHECK-NEXT: call @trace_condition_then_end()
    affine.yield
  } else {
    // CHECK: call @trace_condition_else_begin()
    // CHECK-NEXT: call @trace_condition_else_end()
    affine.yield
  }
  func.return
}

// -----

// CHECK-LABEL: func @scf_if_with_ops_inside
func.func @scf_if_with_ops_inside(%cond : i1, %a : i32, %b : i32) -> i32 {
  %res = scf.if %cond -> i32 {
    // CHECK: call @trace_condition_then_begin()
    %x = arith.addi %a, %b : i32
    // CHECK: call @trace_condition_then_end()
    scf.yield %x : i32
  } else {
    // CHECK: call @trace_condition_else_begin()
    %y = arith.subi %a, %b : i32
    // CHECK: call @trace_condition_else_end()
    scf.yield %y : i32
  }
  return %res : i32
}