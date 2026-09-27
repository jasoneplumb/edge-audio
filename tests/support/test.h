/* tests/support/test.h — minimal assert harness (W0). One executable per §9 group. */
#ifndef EA_TEST_H
#define EA_TEST_H

#include <stdio.h>
#include <string.h>

static int ea_test_failures;
static int ea_test_checks;

#define EA_CHECK(cond)                                                        \
    do {                                                                      \
        ea_test_checks++;                                                     \
        if (!(cond)) {                                                        \
            ea_test_failures++;                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define EA_CHECK_EQ(a, b)                                                     \
    do {                                                                      \
        ea_test_checks++;                                                     \
        long long ea_a = (long long)(a), ea_b = (long long)(b);               \
        if (ea_a != ea_b) {                                                   \
            ea_test_failures++;                                               \
            fprintf(stderr, "FAIL %s:%d: %s == %s (%lld != %lld)\n",          \
                    __FILE__, __LINE__, #a, #b, ea_a, ea_b);                  \
        }                                                                     \
    } while (0)

#define EA_RUN(fn)                                                            \
    do {                                                                      \
        fprintf(stderr, "RUN  %s\n", #fn);                                    \
        fn();                                                                 \
    } while (0)

#define EA_TEST_SUMMARY()                                                     \
    (fprintf(stderr, "%d checks, %d failures\n", ea_test_checks,              \
             ea_test_failures),                                               \
     ea_test_failures ? 1 : 0)

#endif /* EA_TEST_H */
