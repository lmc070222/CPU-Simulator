
#include <cstdio>
#include "cpu.hpp"
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <datafile>\n", argv[0]);
        return 1;
    }

    CPU cpu;
    cpu.init(argv[1]);

    int ret = cpu.run();

    printf("Return value: %d\n", ret);
    printf("Clock cycles: %lu\n", (unsigned long)cpu.clock_cycle());
    printf("Branch prediction accuracy: %.2f%%\n",
           cpu.branch_accuracy() * 100.0f);

    return 0;
}
