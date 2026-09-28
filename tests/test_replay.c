/*
 * Intent: T-04 — record a frame stream from the simulated adapter; the replay tool
 * reproduces three arbitrary sample windows bit-exactly, positioned only by
 * first_sample_index (never wall clock); a window overlapping a recorded gap
 * is SPANS_GAP, never zero-filled (proves R-07, R-08, I-005).
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "../tools/replay/replay_lib.h"
#include "support/recorder.h"
#include "support/sim_adapter.h"
#include "support/test.h"

#define FRAME 320
#define REC_PATH "test_replay.earc"

static ea_status push_ramp_frame(ea_stream *s, uint64_t start,
                                 uint64_t gap_before)
{
    int16_t buf[FRAME];
    for (uint32_t i = 0; i < FRAME; i++)
        buf[i] = (int16_t)((start + i) & 0x7fff);
    return ea_capture_push_block(s, buf, FRAME, gap_before);
}

static void record_stream_with_gap(void)
{
    ea_stream_config cfg = { 16000, 1, 16, 0 };
    ea_stream *s = NULL;
    EA_CHECK_EQ(ea_open(&cfg, &ea_sim_adapter, &s), EA_OK);
    if (!s)
        return;
    EA_CHECK_EQ(ea_start(s), EA_OK);

    /* frames at [0,640) then a gap of 160, then [800, 1440) */
    EA_CHECK_EQ(push_ramp_frame(s, 0, 0), EA_OK);
    EA_CHECK_EQ(push_ramp_frame(s, FRAME, 0), EA_OK);
    EA_CHECK_EQ(push_ramp_frame(s, 800, 160), EA_OK);
    EA_CHECK_EQ(push_ramp_frame(s, 800 + FRAME, 0), EA_OK);

    ea_stream_info info;
    EA_CHECK_EQ(ea_stream_info_get(s, &info), EA_OK);
    ea_recorder *r = ea_recorder_open(REC_PATH, &info, FRAME);
    EA_CHECK(r != NULL);
    if (!r)
        return;

    ea_frame f;
    ea_pop_kind kind;
    int recorded = 0;
    for (;;) {
        EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
        if (kind != EA_POP_FRAME)
            break;
        /* Randomized wall clock proves positioning ignores it (R-07). */
        EA_CHECK_EQ(ea_recorder_write(r, &f, (uint64_t)rand()), 0);
        recorded++;
    }
    EA_CHECK_EQ(recorded, 4);
    ea_recorder_close(r);
    ea_close(s);
}

static void check_window(uint64_t first, uint32_t count) /* T-04, I-005 */
{
    int16_t *out = malloc(count * sizeof(int16_t));
    EA_CHECK_EQ(ea_replay_window(REC_PATH, first, count, out), EA_REPLAY_OK);
    int mismatches = 0;
    for (uint32_t i = 0; i < count; i++)
        if (out[i] != (int16_t)((first + i) & 0x7fff))
            mismatches++;
    EA_CHECK_EQ(mismatches, 0); /* bit-exact */
    free(out);
}

static void three_windows_bit_exact(void) /* T-04, R-08 */
{
    check_window(0, 100);        /* inside the first frame */
    check_window(300, 200);      /* straddles a frame boundary */
    check_window(900, 220);      /* straddles a boundary after the gap */
}

static void gap_window_is_spans_gap_never_zero_filled(void) /* T-04, D-14 */
{
    int16_t out[400];
    EA_CHECK_EQ(ea_replay_window(REC_PATH, 600, 300, out),
                EA_REPLAY_SPANS_GAP); /* overlaps [640, 800) */
    EA_CHECK_EQ(ea_replay_window(REC_PATH, 1400, 100, out),
                EA_REPLAY_OUT_OF_RANGE); /* beyond newest 1440 */
}

static void cli_exit_codes(void) /* T-04: the §5-adjacent tool surface */
{
    EA_CHECK_EQ(system("./replay " REC_PATH
                       " --window 0,320 > replay_out.bin"),
                0);
    /* the written bytes equal the recorded first frame */
    FILE *f = fopen("replay_out.bin", "rb");
    EA_CHECK(f != NULL);
    if (!f)
        return;
    int mismatches = 0;
    for (int i = 0; i < 320; i++) {
        int16_t v;
        if (fread(&v, sizeof(v), 1, f) != 1 || v != (int16_t)i)
            mismatches++;
    }
    fclose(f);
    EA_CHECK_EQ(mismatches, 0);

    int rc = system("./replay " REC_PATH
                    " --window 600,300 > /dev/null 2> /dev/null");
    EA_CHECK_EQ(rc == -1 ? -1 : WEXITSTATUS(rc), 3); /* SPANS_GAP */
}

int main(void)
{
    EA_RUN(record_stream_with_gap);
    EA_RUN(three_windows_bit_exact);
    EA_RUN(gap_window_is_spans_gap_never_zero_filled);
    EA_RUN(cli_exit_codes);
    return EA_TEST_SUMMARY();
}
