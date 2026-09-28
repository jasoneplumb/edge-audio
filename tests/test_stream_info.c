/*
 * Intent: T-18 — stream_info returns the configured sample_rate_hz/channels,
 * PCM_S16LE by construction, and empty transform_versions on a profile with
 * no transforms; values identical across every state the §5.1 matrix marks
 * valid (proves C-07, R-11).
 */
#include "support/sim_adapter.h"
#include "support/test.h"

static void check_info(ea_stream *s, uint32_t expect_id)
{
    ea_stream_info info;
    EA_CHECK_EQ(ea_stream_info_get(s, &info), EA_OK);
    EA_CHECK_EQ(info.stream_id, expect_id);
    EA_CHECK_EQ(info.sample_rate_hz, 16000);
    EA_CHECK_EQ(info.channels, 1);
    EA_CHECK_EQ(info.transform_version_count, 0); /* C-07, K-01, R-09 */
}

static void info_constant_across_states(void) /* T-18, C-07 */
{
    ea_stream_config cfg = { 16000, 1, 4, 0 };
    ea_stream *s = NULL;
    EA_CHECK_EQ(ea_open(&cfg, &ea_sim_adapter, &s), EA_OK);
    EA_CHECK(s != NULL);
    if (!s)
        return;

    ea_stream_info first;
    EA_CHECK_EQ(ea_stream_info_get(s, &first), EA_OK);
    EA_CHECK(first.stream_id > 0);

    check_info(s, first.stream_id);            /* CONFIGURED */
    EA_CHECK_EQ(ea_start(s), EA_OK);
    check_info(s, first.stream_id);            /* RUNNING */
    EA_CHECK_EQ(ea_stop(s), EA_OK);
    check_info(s, first.stream_id);            /* STOPPED */
    ea_close(s);

    /* FAULTED */
    s = NULL;
    EA_CHECK_EQ(ea_open(&cfg, &ea_sim_adapter, &s), EA_OK);
    if (!s)
        return;
    ea_stream_info second;
    EA_CHECK_EQ(ea_stream_info_get(s, &second), EA_OK);
    EA_CHECK(second.stream_id != first.stream_id); /* C-01: monotonic ids */
    EA_CHECK_EQ(ea_start(s), EA_OK);
    EA_CHECK_EQ(ea_capture_fault(s, 1), EA_OK);
    check_info(s, second.stream_id);           /* FAULTED */
    ea_close(s);
}

int main(void)
{
    EA_RUN(info_constant_across_states);
    return EA_TEST_SUMMARY();
}
