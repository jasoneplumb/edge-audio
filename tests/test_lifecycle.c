/*
 * Intent: T-08 — every operation × state cell of the §5.1 validity matrix: ✓ cells
 *        succeed, — cells return INVALID_STATE without changing stream state
 *        (proves R-11, E-09).
 * T-10 — adapter fault: FAULTED state, ADAPTER_FAULT event, queued frames
 *        drain, then end_of_stream (proves E-06).
 */
#include "support/sim_adapter.h"
#include "support/test.h"

static ea_stream *open_stream(void)
{
    ea_stream_config cfg = { 16000, 1, 4, 0 };
    ea_stream *s = NULL;
    ea_open(&cfg, &ea_sim_adapter, &s);
    return s;
}

static void matrix_configured(void) /* T-08, E-09 */
{
    ea_stream *s = open_stream();
    EA_CHECK(s != NULL);
    if (!s)
        return;

    ea_frame f;
    ea_pop_kind kind;
    ea_event ev[2];
    ea_stream_info info;
    ea_unavail_reason why;
    int16_t buf[16];

    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_ERR_INVALID_STATE);       /* — */
    EA_CHECK_EQ(ea_read_history(s, 1, 0, 16, buf, &why),
                EA_ERR_INVALID_STATE);                             /* — */
    EA_CHECK_EQ(ea_stop(s), EA_ERR_INVALID_STATE);                 /* — */
    EA_CHECK_EQ(ea_poll_events(s, ev, 2), 0);                      /* ✓ */
    EA_CHECK_EQ(ea_stream_info_get(s, &info), EA_OK);              /* ✓ */
    EA_CHECK_EQ(ea_start(s), EA_OK);                               /* ✓ */
    ea_close(s);
}

static void matrix_running(void) /* T-08, E-09 */
{
    ea_stream *s = open_stream();
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(ea_start(s), EA_OK);

    ea_frame f;
    ea_pop_kind kind;
    ea_event ev[2];
    ea_stream_info info;

    EA_CHECK_EQ(ea_start(s), EA_ERR_INVALID_STATE); /* — : start twice */
    /* state unchanged by the rejected start: capture still works */
    EA_CHECK_EQ(ea_sim_push_frames(s, 1, 3), EA_OK);
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);       /* ✓ */
    EA_CHECK_EQ(kind, EA_POP_FRAME);
    EA_CHECK_EQ(ea_poll_events(s, ev, 2), 0);       /* ✓ */
    EA_CHECK_EQ(ea_stream_info_get(s, &info), EA_OK); /* ✓ */
    EA_CHECK_EQ(ea_stop(s), EA_OK);                 /* ✓ */
    ea_close(s);
}

static void matrix_stopped_and_faulted(void) /* T-08, E-09 */
{
    /* STOPPED */
    ea_stream *s = open_stream();
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(ea_start(s), EA_OK);
    EA_CHECK_EQ(ea_stop(s), EA_OK);

    ea_frame f;
    ea_pop_kind kind;
    ea_event ev[2];
    ea_stream_info info;

    EA_CHECK_EQ(ea_start(s), EA_ERR_INVALID_STATE); /* — */
    EA_CHECK_EQ(ea_stop(s), EA_ERR_INVALID_STATE);  /* — : stop twice */
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);       /* ✓ (drain) */
    EA_CHECK_EQ(kind, EA_POP_END_OF_STREAM);
    EA_CHECK_EQ(ea_poll_events(s, ev, 2), 0);       /* ✓ */
    EA_CHECK_EQ(ea_stream_info_get(s, &info), EA_OK); /* ✓ */
    /* capture push in STOPPED is an adapter defect: rejected */
    EA_CHECK_EQ(ea_sim_push_frames(s, 1, 3), EA_ERR_INVALID_STATE);
    ea_close(s);

    /* FAULTED */
    s = open_stream();
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(ea_start(s), EA_OK);
    EA_CHECK_EQ(ea_capture_fault(s, 42), EA_OK);
    EA_CHECK_EQ(ea_start(s), EA_ERR_INVALID_STATE); /* — */
    EA_CHECK_EQ(ea_stop(s), EA_ERR_INVALID_STATE);  /* — */
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);       /* ✓ (drain) */
    EA_CHECK_EQ(kind, EA_POP_END_OF_STREAM);
    EA_CHECK_EQ(ea_stream_info_get(s, &info), EA_OK); /* ✓ */
    ea_close(s);
}

static void fault_drains_then_end_of_stream(void) /* T-10, E-06 */
{
    ea_stream *s = open_stream();
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(ea_start(s), EA_OK);
    EA_CHECK_EQ(ea_sim_push_frames(s, 2, 9), EA_OK);
    EA_CHECK_EQ(ea_capture_fault(s, 7), EA_OK);

    ea_event ev[4];
    size_t n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 1);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_ADAPTER_FAULT);
    EA_CHECK_EQ(ev[0].detail.adapter_fault.code, 7);
    EA_CHECK_EQ(ev[0].at_sample, 2 * 320); /* next index the clock would assign */

    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_FRAME); /* already-queued frames drain */
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_FRAME);
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_END_OF_STREAM);
    ea_close(s);
}

/* A partial (sub-K-06) accumulation at stop() or fault can never become a
 * frame; its loss is announced as a DISCONTINUITY event (I-002, E-03 —
 * PR #3 review finding 3: the behavior existed but was untested). */
static void partial_accumulation_announced_on_stop_and_fault(void)
{
    /* stop() case */
    ea_stream *s = open_stream();
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(ea_start(s), EA_OK);
    EA_CHECK_EQ(ea_sim_push_block(s, 100, 5, 0), EA_OK); /* partial acc */
    EA_CHECK_EQ(ea_stop(s), EA_OK);

    ea_event ev[4];
    size_t n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 1);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_DISCONTINUITY);
    EA_CHECK_EQ(ev[0].at_sample, 0); /* first discarded index */
    EA_CHECK_EQ(ev[0].detail.discontinuity.gap_samples, 100);

    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_END_OF_STREAM); /* nothing was ever queued */
    ea_close(s);

    /* fault case: DISCONTINUITY for the partial, then ADAPTER_FAULT at the
     * post-gap clock position (E-06, C-04) */
    s = open_stream();
    EA_CHECK(s != NULL);
    if (!s)
        return;
    EA_CHECK_EQ(ea_start(s), EA_OK);
    EA_CHECK_EQ(ea_sim_push_block(s, 100, 5, 0), EA_OK);
    EA_CHECK_EQ(ea_capture_fault(s, 3), EA_OK);
    n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 2);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_DISCONTINUITY);
    EA_CHECK_EQ(ev[0].detail.discontinuity.gap_samples, 100);
    EA_CHECK_EQ(ev[1].kind, EA_EVENT_ADAPTER_FAULT);
    EA_CHECK_EQ(ev[1].at_sample, 100);
    ea_close(s);
}

int main(void)
{
    EA_RUN(matrix_configured);
    EA_RUN(matrix_running);
    EA_RUN(matrix_stopped_and_faulted);
    EA_RUN(fault_drains_then_end_of_stream);
    EA_RUN(partial_accumulation_announced_on_stop_and_fault);
    return EA_TEST_SUMMARY();
}
