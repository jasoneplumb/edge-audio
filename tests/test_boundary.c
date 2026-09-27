/*
 * T-13 — static boundary check: no source file outside the adapter
 * references hardware-specific headers or symbols; also enforces the
 * no-wall-clock and no-logging core rules and the K-04 loss funnel
 * (proves R-06, I-006).
 */
#include <stdlib.h>

#include "support/test.h"

#ifndef EA_SOURCE_DIR
#define EA_SOURCE_DIR ".."
#endif

static void boundary_script_reports_clean(void) /* T-13, R-06, I-006 */
{
    int rc = system("sh " EA_SOURCE_DIR "/tools/check_boundary.sh "
                    EA_SOURCE_DIR);
    EA_CHECK_EQ(rc, 0);
}

int main(void)
{
    EA_RUN(boundary_script_reports_clean);
    return EA_TEST_SUMMARY();
}
