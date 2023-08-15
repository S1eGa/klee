// RUN: %clang %s -emit-llvm %O0opt -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --debug-validate-solver --debug-assignment-validating-solver %t1.bc 2>&1 | FileCheck %s

#include "ExerciseSolver.c.inc"

// CHECK: KLEE: done: completed paths = 9
// CHECK: KLEE: done: partially completed paths = 5