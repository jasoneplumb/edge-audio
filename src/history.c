/* Bounded history ring — W3 RED stub: nothing is retained, every read is
 * UNAVAILABLE, so the W3 tests (T-05, T-12) fail on assertions first. */
#include "internal.h"

struct ea_history {
    int unused;
};

struct ea_history *ea_history_create(uint32_t capacity_samples,
                                     uint32_t channels)
{
    (void)capacity_samples;
    (void)channels;
    return NULL;
}

void ea_history_destroy(struct ea_history *h)
{
    (void)h;
}

void ea_history_write(struct ea_history *h, uint64_t first_index,
                      const int16_t *interleaved, uint32_t samples_per_channel)
{
    (void)h;
    (void)first_index;
    (void)interleaved;
    (void)samples_per_channel;
}

void ea_history_note_gap(struct ea_history *h, uint64_t first_lost,
                         uint64_t lost_samples)
{
    (void)h;
    (void)first_lost;
    (void)lost_samples;
}

ea_status ea_history_read(struct ea_history *h, uint64_t first_index,
                          uint32_t sample_count, int16_t *out,
                          ea_unavail_reason *reason)
{
    (void)h;
    (void)first_index;
    (void)sample_count;
    (void)out;
    *reason = EA_UNAVAIL_NEVER_CAPTURED;
    return EA_ERR_UNAVAILABLE;
}
