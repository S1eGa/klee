// RUN: %clang -emit-llvm -g -c %s -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee -write-sarifs --use-sym-size-alloc --use-sym-size-li --skip-not-symbolic-objects --posix-runtime --libc=uclibc -cex-cache-validity-cores --output-dir=%t.klee-out %t.bc > %t.log
// RUN: python3 %S/../../checker.py %t.klee-out/report.sarif %S/pattern.sarif

/***************************************************************************************
 *    Title: GSAC
 *    Author: https://github.com/GSACTech
 *    Date: 2023
 *    Code version: 1.0
 *    Availability: https://github.com/GSACTech/contest
 *
 ***************************************************************************************/

/*
 * Based on CVE-2022-23462
 */

#include <stdio.h>
#include <stdlib.h>

#define IWNUMBUF_SIZE 32
char buf[IWNUMBUF_SIZE];

void iwjson_ftoa(long double val, size_t *out_len, size_t buf_size) {
  // Length of 'buf' is 32. But size of 'maxlen' in 'snprintf' is 64
  int len = snprintf(buf, buf_size, "%.8Lf", val); // buffer overflow
  if (len <= 0) {
    buf[0] = '\0';
    *out_len = 0;
    return;
  }
  while (len > 0 && buf[len - 1] == '0') {
    buf[len - 1] = '\0';
    len--;
  }
  if ((len > 0) && (buf[len - 1] == '.')) {
    buf[len - 1] = '\0';
    len--;
  }
  *out_len = (size_t)len;
}

int main() {
  size_t *out_len = malloc(sizeof(size_t));
  *out_len = 50;
  iwjson_ftoa(12345678912345678912345.1234, out_len, 64);
  free(out_len);
}
