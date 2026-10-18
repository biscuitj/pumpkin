#include "test.h"

int main() {
    int run = 0;
    for (const auto& c : testing::cases()) {
        testing::current() = c.name;
        const int before = testing::failures();
        c.fn();
        ++run;
        if (testing::failures() == before) printf("  ok   %s\n", c.name);
    }
    printf("\n%d tests, %d failed checks\n", run, testing::failures());
    return testing::failures() ? 1 : 0;
}
