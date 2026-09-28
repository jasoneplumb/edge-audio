/* Intent: W0 smoke: the library links and reports a version. Not a spec test. */
#include "support/test.h"
#include "edge_audio/version.h"

static void links_and_reports_version(void)
{
    EA_CHECK(ea_version() != NULL);
    EA_CHECK(strlen(ea_version()) > 0);
}

int main(void)
{
    EA_RUN(links_and_reports_version);
    return EA_TEST_SUMMARY();
}
