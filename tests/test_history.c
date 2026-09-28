/*
 * Intent: T-05 — history: exact-interval read is bit-identical; evicted, out-of-range
 *        and gap-spanning intervals return UNAVAILABLE with the correct
 *        reason, including SPANS_GAP (proves R-04, I-004, E-02).
 * T-12 — restart after close: new stream_id, clock at 0, and a history read
 *        citing the prior stream_id returns NEVER_CAPTURED (proves E-07).
 */
#include <stdlib.h>

#include "support/sim_adapter.h"
#include "support/test.h"

#define FRAME 320 /* K-06 @ 16 kHz */

static ea_stream *open_running_hist(uint32_t hist_cap)
{
    ea_stream_config cfg = { 16000, 1, 8, hist_cap };
    ea_stream *s = NULL;
    if (ea_open(&cfg, &ea_sim_adapter, &s) != EA_OK)
        return NULL;
    if (ea_start(s) != EA_OK)
        return NULL;
    return s;
}

/* Push one frame whose sample value equals its per-channel index. */
static ea_status push_ramp_frame(ea_stream *s, uint64_t start,
                                 uint64_t gap_before)
{
    int16_t buf[FRAME];
    for (uint32_t i = 0; i < FRAME; i++)
        buf[i] = (int16_t)((start + i) & 0x7fff);
    return ea_capture_push_block(s, buf, FRAME, gap_before);
}

static uint32_t sid(ea_stream *s)
{
    ea_stream_info info;
    ea_stream_info_get(s, &info);
    return info.stream_id;
}

static void exact_interval_is_bit_identical(void) /* T-05, I-004, C-03 */
{
    ea_stream *s = open_running_hist(2 * FRAME);
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(push_ramp_frame(s, 0, 0), EA_OK);
    EA_CHECK_EQ(push_ramp_frame(s, FRAME, 0), EA_OK);

    int16_t out[50];
    ea_unavail_reason why;
    EA_CHECK_EQ(ea_read_history(s, sid(s), 100, 50, out, &why), EA_OK);
    int mismatches = 0;
    for (int i = 0; i < 50; i++)
        if (out[i] != (int16_t)(100 + i))
            mismatches++;
    EA_CHECK_EQ(mismatches, 0); /* bit-identical (I-004) */
    ea_close(s);
}

static void evicted_out_of_range_and_gap_reasons(void) /* T-05, E-02, D-14 */
{
    ea_stream *s = open_running_hist(2 * FRAME); /* retains 640 samples */
    EA_CHECK(s != NULL);
    if (!s)
        return;
    for (int f = 0; f < 4; f++)
        EA_CHECK_EQ(push_ramp_frame(s, (uint64_t)f * FRAME, 0), EA_OK);
    /* retained: [640, 1280) */

    int16_t out[320];
    ea_unavail_reason why = 0;

    EA_CHECK_EQ(ea_read_history(s, sid(s), 0, FRAME, out, &why),
                EA_ERR_UNAVAILABLE);
    EA_CHECK_EQ(why, EA_UNAVAIL_EVICTED);

    EA_CHECK_EQ(ea_read_history(s, sid(s), 1200, 200, out, &why),
                EA_ERR_UNAVAILABLE); /* end 1400 > newest 1280 */
    EA_CHECK_EQ(why, EA_UNAVAIL_OUT_OF_RANGE);

    EA_CHECK_EQ(ea_read_history(s, sid(s), 900, 200, out, &why), EA_OK);
    EA_CHECK_EQ(out[0], (int16_t)900);

    /* gap of 100 at [1280, 1380), then a frame at [1380, 1700) */
    EA_CHECK_EQ(push_ramp_frame(s, 1380, 100), EA_OK);
    EA_CHECK_EQ(ea_read_history(s, sid(s), 1300, 200, out, &why),
                EA_ERR_UNAVAILABLE); /* straddles the gap */
    EA_CHECK_EQ(why, EA_UNAVAIL_SPANS_GAP);
    EA_CHECK_EQ(ea_read_history(s, sid(s), 1300, 50, out, &why),
                EA_ERR_UNAVAILABLE); /* entirely inside the gap */
    EA_CHECK_EQ(why, EA_UNAVAIL_SPANS_GAP);
    EA_CHECK_EQ(ea_read_history(s, sid(s), 1380, 100, out, &why), EA_OK);
    EA_CHECK_EQ(out[0], (int16_t)1380);

    /* history disabled: O-1 gate — nothing readable */
    ea_close(s);
    s = open_running_hist(0);
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(push_ramp_frame(s, 0, 0), EA_OK);
    EA_CHECK_EQ(ea_read_history(s, sid(s), 0, 10, out, &why),
                EA_ERR_UNAVAILABLE);
    EA_CHECK_EQ(why, EA_UNAVAIL_NEVER_CAPTURED);
    ea_close(s);
}

static void restart_resets_clock_and_isolates_history(void) /* T-12, E-07 */
{
    ea_stream *s1 = open_running_hist(2 * FRAME);
    EA_CHECK(s1 != NULL);
    if (!s1)
        return;
    uint32_t id1 = sid(s1);
    EA_CHECK_EQ(push_ramp_frame(s1, 0, 0), EA_OK);
    ea_close(s1);

    ea_stream *s2 = open_running_hist(2 * FRAME);
    EA_CHECK(s2 != NULL);
    if (!s2)
        return;
    EA_CHECK(sid(s2) != id1); /* new stream_id */
    EA_CHECK_EQ(push_ramp_frame(s2, 0, 0), EA_OK);

    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s2, &f, &kind), EA_OK);
    EA_CHECK_EQ(f.first_sample_index, 0); /* clock restarted at 0 */

    int16_t out[10];
    ea_unavail_reason why = 0;
    EA_CHECK_EQ(ea_read_history(s2, id1, 0, 10, out, &why),
                EA_ERR_UNAVAILABLE); /* prior stream unreadable (C-03) */
    EA_CHECK_EQ(why, EA_UNAVAIL_NEVER_CAPTURED);
    ea_close(s2);
}

int main(void)
{
    EA_RUN(exact_interval_is_bit_identical);
    EA_RUN(evicted_out_of_range_and_gap_reasons);
    EA_RUN(restart_resets_clock_and_isolates_history);
    return EA_TEST_SUMMARY();
}
