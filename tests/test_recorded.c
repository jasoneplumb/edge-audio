/*
 * Intent: T-17 presence check (the spec's §9.3 rule for a *(recorded)* test): the
 * recorded soak evidence exists in SPEC_BUILD_REPORT.md with the required
 * fields. The soak itself is a recorded run, not an assertion here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/test.h"

#ifndef EA_SOURCE_DIR
#define EA_SOURCE_DIR ".."
#endif

static char *read_report(void)
{
    FILE *f = fopen(EA_SOURCE_DIR "/SPEC_BUILD_REPORT.md", "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = '\0';
    fclose(f);
    return buf;
}

static void recorded_soak_evidence_present(void) /* T-17 */
{
    char *report = read_report();
    EA_CHECK(report != NULL);
    if (!report)
        return;
    /* The §soak section with its per-constraint outcomes (K-01, K-02, K-03). */
    EA_CHECK(strstr(report, "Soak evidence (T-17") != NULL);
    EA_CHECK(strstr(report, "K-02 placement:") != NULL);
    EA_CHECK(strstr(report, "K-03 host rehearsal:") != NULL);
    EA_CHECK(strstr(report, "device soak:") != NULL);
    free(report);
}

int main(void)
{
    EA_RUN(recorded_soak_evidence_present);
    return EA_TEST_SUMMARY();
}
