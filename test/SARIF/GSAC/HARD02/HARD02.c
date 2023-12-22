// RUN: %clang -emit-llvm -g -c %s -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee -write-sarifs --use-sym-size-alloc --use-sym-size-li --skip-not-symbolic-objects --posix-runtime --libc=uclibc -cex-cache-validity-cores --output-dir=%t.klee-out %t.bc > %t.log
// RUN: %checker %t.klee-out/report.sarif %S/pattern.sarif

/***************************************************************************************
 *    Title: GSAC
 *    Author: https://github.com/GSACTech
 *    Date: 2023
 *    Code version: 1.0
 *    Availability: https://github.com/GSACTech/contest
 *
 ***************************************************************************************/

/*
 * Based on CVE-2022-26768
 */
#include "HARD02.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char initialLogFileName[FILENAMESIZE] = "";

void EXPORT_CALL lou_logFile(const char *fileName) {
  if (fileName == NULL || fileName[0] == 0)
    return;
  if (initialLogFileName[0] == 0)
    // Size of 'fileName' is greater than 'FILENAMESIZE'
    strcpy(initialLogFileName, fileName); // buffer-overflow
  logFile = fopen(fileName, "a");
  if (logFile == NULL && initialLogFileName[0] != 0)
    logFile = fopen(initialLogFileName, "a");
  if (logFile == NULL) {
    fprintf(stderr, "Cannot open log file %s\n", fileName);
    logFile = stderr;
  }
}

int main() {
  const int sz = 260;
  char *fileName = malloc(sz + 1); // Allocate 261 bytes of memory
  memset(fileName, 's', sz);
  fileName[sz] = '\0';
  lou_logFile(fileName);
  free(fileName);
}
