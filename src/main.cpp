
#include <cstdio>
#include "cpu.hpp"
int main() {
    CPU cpu;
    cpu.init_stdin();

    int ret = cpu.run();

    printf("%d\n", ret);

    return 0;
}
