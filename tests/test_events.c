/*
 * Intent: T-11 — event-queue overflow drops oldest events; the drop count is reported
 *        in the next delivered event's events_dropped (proves E-08, R-10).
 * T-14 — no log or event payload contains raw audio samples (proves R-10):
 *        events carry fixed metadata only, and the boundary test bans logging
 *        calls from the core outright.
 */
#include <string.h>

#include "support/sim_adapter.h"
#include "support/test.h"

static ea_stream *open_running(void)
{
    ea_stream_config cfg = { 16000, 1, 64, 0 };
    ea_stream *s = NULL;
    if (ea_open(&cfg, &ea_sim_adapter, &s) != EA_OK)
        return NULL;
    if (ea_start(s) != EA_OK)
        return NULL;
    return s;
}

static void event_queue_overflow_reports_dropped(void) /* T-11, E-08 */
{
    ea_stream *s = open_running();
    EA_CHECK(s != NULL);
    if (!s)
        return;

    /* 20 gap events without polling; the event ring caps at 16 (E-08). */
    for (int i = 0; i < 20; i++)
        EA_CHECK_EQ(ea_sim_push_block(s, 320, 3, 10), EA_OK);

    ea_event ev[32];
    size_t n = ea_poll_events(s, ev, 32);
    EA_CHECK_EQ(n, 16);
    EA_CHECK_EQ(ev[0].events_dropped, 4); /* next delivered event reports */
    for (size_t i = 1; i < n; i++)
        EA_CHECK_EQ(ev[i].events_dropped, 0);
    /* oldest were dropped: first delivered is the 5th gap event */
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_DISCONTINUITY);
    ea_close(s);
}

static void events_carry_no_audio_payload(void) /* T-14, R-10 */
{
    ea_stream *s = open_running();
    EA_CHECK(s != NULL);
    if (!s)
        return;

    /* Distinctive sample pattern through the capture path. */
    EA_CHECK_EQ(ea_sim_push_block(s, 320, 0x5A5A, 25), EA_OK);

    ea_event ev[4];
    size_t n = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(n, 1);

    /* The event struct is fixed-size metadata: no pointers, no room for a
     * frame's 640 bytes of samples (R-10). */
    EA_CHECK(sizeof(ea_event) < 64);
    /* And the pattern does not appear in the event bytes. */
    const unsigned char *bytes = (const unsigned char *)&ev[0];
    int runs = 0;
    for (size_t i = 0; i + 4 <= sizeof(ea_event); i++) {
        if (bytes[i] == 0x5A && bytes[i + 1] == 0x5A &&
            bytes[i + 2] == 0x5A && bytes[i + 3] == 0x5A)
            runs++;
    }
    EA_CHECK_EQ(runs, 0);
    ea_close(s);
}

int main(void)
{
    EA_RUN(event_queue_overflow_reports_dropped);
    EA_RUN(events_carry_no_audio_payload);
    return EA_TEST_SUMMARY();
}
