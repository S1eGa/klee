#include <stdio.h>

char *gDest;

char *HelpTestBad8(int len)
{
#if 0
        if (len > 0) {
                return gDest;
        }
#endif
        return NULL;
}

void TestBad8(int len)
{
        // char *buf = HelpTestBad8(len);
        char *buf = (char *)malloc(10);
        buf[0] = 'a'; // CHECK: KLEE: WARNING: True Positive at: /mnt/d/wsl-ubuntu/test2/./test.c:19
}

// RUN: %clang %s -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --execution-mode=error-guided --mock-external-calls --posix-runtime --libc=klee --skip-not-lazy-and-symbolic-pointers --check-out-of-memory --max-time=120s --analysis-reproduce=%s.json %t1.bc
// RUN: FileCheck -input-file=%t.klee-out/warnings.txt %s