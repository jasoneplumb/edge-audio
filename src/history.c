/*
 * Bounded history ring (realizes R-04, C-03, I-004; E-02 reasons incl.
 * SPANS_GAP per D-14; O-1 gated by history_capacity, D-07).
 *
 * Sample index i lives at ring slot (i mod capacity); sequential writes evict
 * oldest-first by overwrite. A gap ledger records uncaptured intervals so a
 * read can never serve stale slot bytes as captured audio (I-004). Reason
 * precedence: OUT_OF_RANGE (beyond newest) > SPANS_GAP > EVICTED. If the
 * ledger overflows, everything at or below the discarded gap becomes EVICTED
 * (conservative, never bit-wrong).
 */
#include <string.h>

#include "internal.h"

#define EA_HIST_MAX_GAPS 16u

typedef struct {
    uint64_t start;
    uint64_t len;
} ea_gap;

struct ea_history {
    int16_t *data;       /* capacity * channels interleaved samples */
    uint32_t capacity;   /* samples per channel (C-06, D-07) */
    uint32_t channels;
    uint64_t next_index; /* newest captured-or-lost index + 1 */
    uint64_t evict_floor; /* indices below this are EVICTED (ledger overflow) */
    ea_gap gaps[EA_HIST_MAX_GAPS];
    uint32_t gap_count;
};

struct ea_history *ea_history_create(uint32_t capacity_samples,
                                     uint32_t channels)
{
    struct ea_history *h = ea_port_alloc(sizeof(*h), EA_MEM_HOT);
    if (h == NULL)
        return NULL;
    h->data = ea_port_alloc((size_t)capacity_samples * channels *
                                sizeof(int16_t),
                            EA_MEM_HISTORY); /* K-02: MAY be PSRAM */
    if (h->data == NULL) {
        ea_port_free(h);
        return NULL;
    }
    h->capacity = capacity_samples;
    h->channels = channels;
    return h;
}

void ea_history_destroy(struct ea_history *h)
{
    if (h == NULL)
        return;
    ea_port_free(h->data);
    ea_port_free(h);
}

static uint64_t oldest_retained(const struct ea_history *h)
{
    uint64_t by_capacity =
        (h->next_index > h->capacity) ? h->next_index - h->capacity : 0;
    return (h->evict_floor > by_capacity) ? h->evict_floor : by_capacity;
}

static void prune_gaps(struct ea_history *h)
{
    uint64_t oldest = oldest_retained(h);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < h->gap_count; i++) {
        if (h->gaps[i].start + h->gaps[i].len > oldest)
            h->gaps[kept++] = h->gaps[i];
    }
    h->gap_count = kept;
}

void ea_history_write(struct ea_history *h, uint64_t first_index,
                      const int16_t *interleaved, uint32_t samples_per_channel)
{
    if (h == NULL)
        return;
    for (uint32_t k = 0; k < samples_per_channel; k++) {
        uint64_t i = first_index + k;
        size_t slot = (size_t)(i % h->capacity) * h->channels;
        memcpy(h->data + slot, interleaved + (size_t)k * h->channels,
               (size_t)h->channels * sizeof(int16_t));
    }
    h->next_index = first_index + samples_per_channel;
    prune_gaps(h);
}

void ea_history_note_gap(struct ea_history *h, uint64_t first_lost,
                         uint64_t lost_samples)
{
    if (h == NULL)
        return;
    if (h->gap_count == EA_HIST_MAX_GAPS) {
        /* Ledger full: discard the oldest gap and evict everything at or
         * below it, so nothing stale can ever be served (I-004). */
        uint64_t floor = h->gaps[0].start + h->gaps[0].len;
        if (floor > h->evict_floor)
            h->evict_floor = floor;
        memmove(&h->gaps[0], &h->gaps[1],
                (EA_HIST_MAX_GAPS - 1) * sizeof(ea_gap));
        h->gap_count--;
    }
    h->gaps[h->gap_count].start = first_lost;
    h->gaps[h->gap_count].len = lost_samples;
    h->gap_count++;
    h->next_index = first_lost + lost_samples;
}

ea_status ea_history_read(struct ea_history *h, uint64_t first_index,
                          uint32_t sample_count, int16_t *out,
                          ea_unavail_reason *reason)
{
    if (h == NULL || sample_count == 0) {
        *reason = EA_UNAVAIL_OUT_OF_RANGE;
        return EA_ERR_UNAVAILABLE;
    }
    uint64_t end = first_index + sample_count;
    if (end > h->next_index) {
        *reason = EA_UNAVAIL_OUT_OF_RANGE;
        return EA_ERR_UNAVAILABLE;
    }
    for (uint32_t i = 0; i < h->gap_count; i++) {
        uint64_t gs = h->gaps[i].start, ge = gs + h->gaps[i].len;
        if (first_index < ge && end > gs) { /* overlap (D-14) */
            *reason = EA_UNAVAIL_SPANS_GAP;
            return EA_ERR_UNAVAILABLE;
        }
    }
    if (first_index < oldest_retained(h)) {
        *reason = EA_UNAVAIL_EVICTED;
        return EA_ERR_UNAVAILABLE;
    }
    /* Serve the exact interval, bit-identical (I-004). */
    for (uint32_t k = 0; k < sample_count; k++) {
        uint64_t i = first_index + k;
        size_t slot = (size_t)(i % h->capacity) * h->channels;
        memcpy(out + (size_t)k * h->channels, h->data + slot,
               (size_t)h->channels * sizeof(int16_t));
    }
    return EA_OK;
}
