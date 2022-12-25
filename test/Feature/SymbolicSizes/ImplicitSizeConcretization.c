// RUN: %clang %s -g -emit-llvm %O0opt -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t1.bc 2>&1 | FileCheck %s

#include "klee/klee.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>


int main() {
  int n = klee_int("n");
  if (n >= 5) {
    char *c1 = (char *) malloc(n);
    char *c2 = (char *) malloc(9 - n);
    // CHECK: ImplicitSizeConcretization.c:[[@LINE+1]]: memory error: out of bound pointer
    c2[3] = 10;
    if (n >= 6) {
      assert(0);
      // CHECK-NOT: ASSERTION FAIL
    }
  }
}

// CHECK: KLEE: done: completed paths = 2
// CHECK: KLEE: done: partially completed paths = 1
// CHECK: KLEE: done: generated tests = 3
