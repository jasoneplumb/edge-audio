/*
 * T-03 — queue overflow (proves R-05, K-04, E-01).
 * Fixture 1: overflow past capacity → drop-oldest, exactly one QUEUE_OVERFLOW
 * event with the episode total, emitted at episode end; next delivered frame
 * carries discontinuity = true.
 * Fixture 2: stop() mid-episode → the event is emitted before the stream
 * reaches STOPPED (K-04, F-013).
 */
#include "support/sim_adapter.h"
#include "support/test.h"

static ea_stream *open_running_cap2(void)
{
    ea_stream_config cfg = { 16000, 1, 2, 0 };
    ea_stream *s = NULL;
    if (ea_open(&cfg, &ea_sim_adapter, &s) != EA_OK)
        return NULL;
    if (ea_start(s) != EA_OK)
        return NULL;
    return s;
}

static void overflow_drops_oldest_one_event_per_run(void) /* T-03 */
{
    ea_stream *s = open_running_cap2();
    EA_CHECK(s != NULL);
    if (!s)
        return;

    /* f0,f1 fill; f2 drops f0; f3 drops f1 (loss run of 2, still open). */
    EA_CHECK_EQ(ea_sim_push_frames(s, 4, 5), EA_OK);

    ea_event ev[4];
    EA_CHECK_EQ(ea_poll_events(s, ev, 4), 0); /* K-04: not emitted yet */

    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(f.first_sample_index, 2 * 320); /* f2: oldest surviving */
    EA_CHECK(f.discontinuity); /* E-01: next delivered after the drops */
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(f.first_sample_index, 3 * 320);
    EA_CHECK(!f.discontinuity);

    /* Non-dropping push ends the run → exactly one event, episode total. */
    EA_CHECK_EQ(ea_sim_push_frames(s, 1, 5), EA_OK);
    size_t n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 1);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_QUEUE_OVERFLOW);
    EA_CHECK_EQ(ev[0].detail.queue_overflow.frames_dropped, 2);
    EA_CHECK_EQ(ev[0].at_sample, 0); /* C-04: oldest dropped frame's index */
    ea_close(s);
}

static void stop_mid_episode_emits_before_stopped(void) /* T-03, K-04 */
{
    ea_stream *s = open_running_cap2();
    EA_CHECK(s != NULL);
    if (!s)
        return;

    EA_CHECK_EQ(ea_sim_push_frames(s, 3, 5), EA_OK); /* f2 drops f0: run open */
    EA_CHECK_EQ(ea_stop(s), EA_OK);

    ea_event ev[4];
    size_t n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 1); /* emitted before the transition completed */
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_QUEUE_OVERFLOW);
    EA_CHECK_EQ(ev[0].detail.queue_overflow.frames_dropped, 1);

    /* STOPPED: queued frames drain, then end_of_stream (§3.1, §5.1). */
    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_FRAME);
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_FRAME);
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_END_OF_STREAM);
    ea_close(s);
}

int main(void)
{
    EA_RUN(overflow_drops_oldest_one_event_per_run);
    EA_RUN(stop_mid_episode_emits_before_stopped);
    return EA_TEST_SUMMARY();
}
