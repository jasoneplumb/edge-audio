/*
 * Capture-core tests.
 * T-01 — frame shape, K-06 sizing, C-01 index equation (proves R-01, R-02,
 *        I-001, K-06).
 * T-02 — injected gap: discontinuity flag + DISCONTINUITY event with
 *        at_sample = first lost index and gap_samples = injected size
 *        (proves I-002, E-03).
 * T-09 — empty adapter block: no frame, no clock advance (proves E-05).
 */
#include "support/sim_adapter.h"
#include "support/test.h"

static ea_stream *open_running(uint32_t queue_cap)
{
    ea_stream_config cfg = { 16000, 1, queue_cap, 0 };
    ea_stream *s = NULL;
    if (ea_open(&cfg, &ea_sim_adapter, &s) != EA_OK)
        return NULL;
    if (ea_start(s) != EA_OK)
        return NULL;
    return s;
}

static void frames_satisfy_shape_and_index_equation(void) /* T-01 */
{
    ea_stream *s = open_running(8);
    EA_CHECK(s != NULL);
    if (!s)
        return;

    /* Feed 1000 samples in odd-sized blocks: 3 full frames + 40 leftover. */
    EA_CHECK_EQ(ea_sim_push_block(s, 100, 1000, 0), EA_OK);
    EA_CHECK_EQ(ea_sim_push_block(s, 500, 1000, 0), EA_OK);
    EA_CHECK_EQ(ea_sim_push_block(s, 400, 1000, 0), EA_OK);

    uint64_t expect_index = 0;
    for (int i = 0; i < 3; i++) {
        ea_frame f;
        ea_pop_kind kind;
        EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
        EA_CHECK_EQ(kind, EA_POP_FRAME);
        EA_CHECK_EQ(f.samples_per_channel, 320); /* K-06: 20 ms @ 16 kHz */
        EA_CHECK_EQ(f.sample_rate_hz, 16000);
        EA_CHECK_EQ(f.channels, 1);
        EA_CHECK_EQ(f.first_sample_index, expect_index); /* C-01, I-001 */
        EA_CHECK(!f.discontinuity);
        EA_CHECK(f.samples != NULL);
        EA_CHECK_EQ(f.samples[0], 1000); /* R-02: canonical PCM_S16LE */
        expect_index += f.samples_per_channel;
    }
    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_EMPTY); /* leftover 40 samples not yet a frame */
    ea_close(s);
}

static void gap_sets_discontinuity_and_event(void) /* T-02, E-03, I-002 */
{
    ea_stream *s = open_running(8);
    EA_CHECK(s != NULL);
    if (!s)
        return;

    EA_CHECK_EQ(ea_sim_push_frames(s, 1, 5), EA_OK); /* frame 0: 0..319 */
    EA_CHECK_EQ(ea_sim_push_block(s, 320, 5, 500), EA_OK); /* gap of 500 */

    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK(!f.discontinuity);

    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_FRAME);
    EA_CHECK(f.discontinuity); /* E-03: next frame flags the loss */
    EA_CHECK_EQ(f.first_sample_index, 320 + 500); /* clock advanced by gap */

    ea_event ev[4];
    size_t n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 1);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_DISCONTINUITY);
    EA_CHECK_EQ(ev[0].at_sample, 320); /* C-04: first lost sample index */
    EA_CHECK_EQ(ev[0].detail.discontinuity.gap_samples, 500); /* C-05 */
    ea_close(s);
}

static void gap_mid_accumulation_announces_partial_too(void) /* T-02, E-03, I-002 */
{
    ea_stream *s = open_running(8);
    EA_CHECK(s != NULL);
    if (!s)
        return;

    EA_CHECK_EQ(ea_sim_push_frames(s, 1, 5), EA_OK); /* 0..319 */
    EA_CHECK_EQ(ea_sim_push_block(s, 100, 5, 0), EA_OK); /* partial acc 320..419 */
    EA_CHECK_EQ(ea_sim_push_block(s, 320, 5, 50), EA_OK); /* gap mid-frame */

    /* The 100 accumulated samples cannot form a K-06 frame across the gap;
     * they are lost and announced with the gap (I-002). */
    ea_event ev[4];
    size_t n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 1);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_DISCONTINUITY);
    EA_CHECK_EQ(ev[0].at_sample, 320); /* first lost index = start of partial */
    EA_CHECK_EQ(ev[0].detail.discontinuity.gap_samples, 150); /* 100 + 50 */

    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK); /* frame 0 */
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK(f.discontinuity);
    EA_CHECK_EQ(f.first_sample_index, 470); /* 320 + 100 + 50 */
    ea_close(s);
}

static void empty_block_is_discarded(void) /* T-09, E-05 */
{
    ea_stream *s = open_running(8);
    EA_CHECK(s != NULL);
    if (!s)
        return;

    EA_CHECK_EQ(ea_sim_push_block(s, 0, 0, 0), EA_OK); /* empty block */
    EA_CHECK_EQ(ea_sim_push_frames(s, 1, 7), EA_OK);

    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_FRAME);
    EA_CHECK_EQ(f.first_sample_index, 0); /* E-05: no clock advance */
    EA_CHECK(!f.discontinuity);
    ea_close(s);
}

int main(void)
{
    EA_RUN(frames_satisfy_shape_and_index_equation);
    EA_RUN(gap_sets_discontinuity_and_event);
    EA_RUN(gap_mid_accumulation_announces_partial_too);
    EA_RUN(empty_block_is_discarded);
    return EA_TEST_SUMMARY();
}
