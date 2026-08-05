#include "cpu.hpp"
#include <cstdio>

struct TestCase {
    const char* path;
    int expected;
};

int main() {
    TestCase tests[] = {
        {"data/sample/sample.data", 158},
        {"data/testcases/array_test1.data", 123},
        {"data/testcases/array_test2.data", 43},
        {"data/testcases/basicopt1.data", 88},
        {"data/testcases/bulgarian.data", 159},
        {"data/testcases/expr.data", 58},
        {"data/testcases/gcd.data", 178},
        {"data/testcases/hanoi.data", 20},
        {"data/testcases/lvalue2.data", 175},
        {"data/testcases/magic.data", 106},
        {"data/testcases/manyarguments.data", 40},
        {"data/testcases/multiarray.data", 115},
        {"data/testcases/naive.data", 94},
        {"data/testcases/pi.data", 137},
        {"data/testcases/qsort.data", 105},
        {"data/testcases/queens.data", 171},
        {"data/testcases/statement_test.data", 50},
        {"data/testcases/superloop.data", 134},
        {"data/testcases/tak.data", 186},
    };

    int passed = 0, failed = 0;
    printf("Running %d tests...\n", 19);

    for (int i = 0; i < 19; i++) {
        CPU cpu;
        cpu.init(tests[i].path);
        int result = cpu.run();

        printf("[%2d] %-35s got=%-4d expect=%-4d ",
               i + 1, tests[i].path, result, tests[i].expected);

        if (result == tests[i].expected) {
            printf("PASS  cycles=%lu\n",
                   (unsigned long)cpu.clock_cycle());
            passed++;
        } else {
            printf("FAIL  cycles=%lu\n",
                   (unsigned long)cpu.clock_cycle());
            failed++;
        }
        fflush(stdout);
    }

    printf("\n%d/%d passed, %d failed\n", passed, 19, failed);
    return failed > 0 ? 1 : 0;
}
