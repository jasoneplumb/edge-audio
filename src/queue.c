/*
 * Bounded frame queue (realizes C-02, R-05, K-04, E-01; guards I-001, I-002).
 * This file is the single loss funnel (plan §6): all drop-oldest accounting,
 * episode state and QUEUE_OVERFLOW emission live here and nowhere else.
 *
 * K-04: an overflow episode begins with the first push that drops after a
 * push that did not, ends at the next push that does not drop, and emits
 * exactly one event at episode end with the episode's total frames_dropped;
 * stop()/fault/close with an open episode emit before the state transition
 * completes (via ea_queue_flush_loss).
 */
#include <string.h>

#include "internal.h"

void ea_queue_init(ea_queue *q, ea_slot_hdr *hdrs, int16_t *samples,
                   uint32_t capacity)
{
    memset(q, 0, sizeof(*q));
    q->hdrs = hdrs;
    q->samples = samples;
    q->capacity = capacity;
}

static void emit_overflow_event(struct ea_stream *s)
{
    ea_event e = { 0 };
    e.stream_id = s->stream_id;
    e.kind = EA_EVENT_QUEUE_OVERFLOW;
    e.at_sample = s->q.loss_run_at_sample; /* C-04: oldest dropped frame */
    e.detail.queue_overflow.frames_dropped = s->q.loss_run_frames;
    ea_events_push(&s->ev, &e);
}

void ea_queue_flush_loss(struct ea_stream *s)
{
    if (s->q.loss_run_open) {
        emit_overflow_event(s);
        s->q.loss_run_open = false;
        s->q.loss_run_frames = 0;
    }
}

void ea_queue_push(struct ea_stream *s, uint64_t first_index, bool disc,
                   bool clipped, bool silent, const int16_t *frame_samples)
{
    ea_queue *q = &s->q;

    if (q->count == q->capacity) {
        /* Drop-oldest (D-03, E-01). */
        uint64_t dropped_index = q->hdrs[q->head].first_index;
        if (!q->loss_run_open) {
            q->loss_run_open = true;      /* episode begins (K-04) */
            q->loss_run_frames = 0;
            q->loss_run_at_sample = dropped_index;
        }
        q->loss_run_frames++;
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        /* E-01: the next delivered frame carries discontinuity = true. */
        if (q->count > 0)
            q->hdrs[q->head].discontinuity = true;
        else
            disc = true; /* the incoming frame is the next delivered one */
    } else if (q->loss_run_open) {
        /* A push that does not drop ends the episode (K-04, D-15). */
        emit_overflow_event(s);
        q->loss_run_open = false;
        q->loss_run_frames = 0;
    }

    uint32_t slot = (q->head + q->count) % q->capacity;
    q->hdrs[slot].first_index = first_index;
    q->hdrs[slot].discontinuity = disc;
    q->hdrs[slot].clipped = clipped;
    q->hdrs[slot].silent = silent;
    memcpy(q->samples + (size_t)slot * s->frame_total, frame_samples,
           (size_t)s->frame_total * sizeof(int16_t));
    q->count++;
}

bool ea_queue_pop(struct ea_stream *s, ea_slot_hdr *hdr_out, int16_t *samples_out)
{
    ea_queue *q = &s->q;
    if (q->count == 0)
        return false;
    *hdr_out = q->hdrs[q->head];
    memcpy(samples_out, q->samples + (size_t)q->head * s->frame_total,
           (size_t)s->frame_total * sizeof(int16_t));
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return true; /* I-001: strictly increasing delivery order */
}
