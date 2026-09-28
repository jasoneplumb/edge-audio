/*
 * Intent: T-13 — static boundary check: no source file outside the adapter
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

/* Failure path for the Intent-header convention gate (issue #1, not a spec
 * id): a scratch root with a headerless source file must make the checker
 * exit nonzero, so a regression in its find/grep logic cannot pass silently. */
static void checker_fires_on_missing_intent_header(void)
{
    int rc = system("rm -rf bn_scratch && mkdir -p bn_scratch/src && "
                    "printf 'int x;\\n' > bn_scratch/src/bad.c && "
                    "sh " EA_SOURCE_DIR "/tools/check_boundary.sh bn_scratch"
                    " > /dev/null");
    EA_CHECK(rc != 0);
    system("rm -rf bn_scratch");
}

int main(void)
{
    EA_RUN(boundary_script_reports_clean);
    EA_RUN(checker_fires_on_missing_intent_header);
    return EA_TEST_SUMMARY();
}
