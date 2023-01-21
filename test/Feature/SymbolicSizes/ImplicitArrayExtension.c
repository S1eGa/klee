// RUN: %clang %s -g -emit-llvm %O0opt -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t1.bc 2>&1 | FileCheck %s

#include "klee/klee.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int main() {
  int n = klee_int("n");
  int m = klee_int("m");

  char *s1 = (char *) malloc(n);
  char *s2 = (char *) malloc(m);
  assert(s1 && s2);
  if (n < m) {
    // CHECK: ImplicitArrayExtension.c:[[@LINE+1]]: memory error: out of bound pointer
    s1[1] = 10; // n == 2
    // CHECK-NOT: ImplicitArrayExtension.c:[[@LINE+1]]: memory error: out of bound pointer
    s2[2] = 20; // m > 2
    // CHECK: ImplicitArrayExtension.c:[[@LINE+1]]: ASSERTION FAIL
    assert(0);
  }
}

// CHECK: KLEE: done: completed paths = 1
// CHECK: KLEE: done: partially completed paths = 5
// CHECK: KLEE: done: generated tests = 4