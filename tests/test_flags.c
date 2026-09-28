/*
 * Intent: T-06 — §7.1 flag fixtures (proves R-03, K-05).
 * Known fixtures: a full-scale frame sets clipped; a -70 dBFS frame sets
 * silent; a -40 dBFS frame sets neither; the L_rms = 0 degenerate case is
 * silent (§7.1). DC-valued frames make the RMS exact:
 * dBFS(v) = 20*log10(|v|/32768): v=10 -> -70.3 dBFS, v=328 -> -40.0 dBFS.
 */
#include "support/test.h"
#include "edge_audio/edge_audio.h"

#define N 320 /* K-06: 20 ms at 16 kHz */

static void fill_dc(int16_t *buf, uint32_t n, int16_t v)
{
    for (uint32_t i = 0; i < n; i++)
        buf[i] = v;
}

static void full_scale_frame_sets_clipped_not_silent(void) /* T-06, K-05, D-06 */
{
    int16_t buf[N];
    fill_dc(buf, N, 32767);
    EA_CHECK(ea_flags_clipped(buf, N, 1));
    EA_CHECK(!ea_flags_silent(buf, N, 1));

    fill_dc(buf, N, 1000);
    buf[N / 2] = -32768; /* any single extreme sample clips (D-06) */
    EA_CHECK(ea_flags_clipped(buf, N, 1));
}

static void minus70_dbfs_frame_is_silent(void) /* T-06, K-05, D-05 */
{
    int16_t buf[N];
    fill_dc(buf, N, 10); /* -70.3 dBFS < -60 */
    EA_CHECK(ea_flags_silent(buf, N, 1));
    EA_CHECK(!ea_flags_clipped(buf, N, 1));
}

static void minus40_dbfs_frame_sets_neither(void) /* T-06, K-05 */
{
    int16_t buf[N];
    fill_dc(buf, N, 328); /* -40.0 dBFS: not silent, not clipped */
    EA_CHECK(!ea_flags_silent(buf, N, 1));
    EA_CHECK(!ea_flags_clipped(buf, N, 1));
}

static void all_zero_frame_is_silent_degenerate(void) /* §7.1 degenerate, K-05 */
{
    int16_t buf[N] = {0};
    EA_CHECK(ea_flags_silent(buf, N, 1));
    EA_CHECK(!ea_flags_clipped(buf, N, 1));
}

static void multichannel_population_is_whole_frame(void) /* §7.1 population, K-05 */
{
    /* 2 channels: one loud channel keeps the whole frame above -60 dBFS. */
    int16_t buf[2 * N];
    for (uint32_t i = 0; i < N; i++) {
        buf[2 * i] = 3277; /* ~-20 dBFS channel */
        buf[2 * i + 1] = 0;
    }
    EA_CHECK(!ea_flags_silent(buf, N, 2));
}

int main(void)
{
    EA_RUN(full_scale_frame_sets_clipped_not_silent);
    EA_RUN(minus70_dbfs_frame_is_silent);
    EA_RUN(minus40_dbfs_frame_sets_neither);
    EA_RUN(all_zero_frame_is_silent_degenerate);
    EA_RUN(multichannel_population_is_whole_frame);
    return EA_TEST_SUMMARY();
}
