/*
 * Intent: edge-audio internals (W2; W3 adds the history module).
 * The single mutex per stream guards queue, events, history and lifecycle;
 * capture-side critical sections are short and never wait on consumers
 * (I-003 — no inference/UI/network/logging/storage on this path, R-10).
 */
#ifndef EA_INTERNAL_H
#define EA_INTERNAL_H

#include "edge_audio/edge_audio.h"
#include "edge_audio/port.h"

typedef enum {
    EA_ST_CONFIGURED = 0,
    EA_ST_RUNNING,
    EA_ST_STOPPED,
    EA_ST_FAULTED
} ea_state;

#define EA_EVENT_RING_CAP 16u /* bounded event queue (E-08); freedom, documented */

typedef struct {
    uint64_t first_index;
    bool discontinuity;
    bool clipped;
    bool silent;
} ea_slot_hdr;

typedef struct {
    ea_slot_hdr *hdrs;    /* capacity entries */
    int16_t *samples;     /* capacity * frame_total interleaved samples */
    uint32_t capacity;    /* C-02: fixed at open (D-03) */
    uint32_t head;        /* oldest */
    uint32_t count;
    /* K-04 loss-run accounting lives in queue.c only (plan §6 funnel). */
    bool loss_run_open;
    uint32_t loss_run_frames;
    uint64_t loss_run_at_sample;
} ea_queue;

typedef struct {
    ea_event buf[EA_EVENT_RING_CAP];
    uint32_t head;
    uint32_t count;
    uint32_t dropped_pending; /* E-08: reported on next delivered event */
} ea_events;

struct ea_history; /* W3 */

struct ea_stream {
    ea_mutex lock;
    ea_state state;
    ea_stream_config cfg;
    const ea_adapter_desc *adapter;
    uint32_t stream_id;      /* C-01: boot-scoped monotonic */
    uint32_t frame_samples;  /* per channel (K-06) */
    uint32_t frame_total;    /* frame_samples * channels */
    uint64_t clock;          /* next per-channel index to assign (R-01) */
    /* frame accumulator */
    int16_t *acc;
    uint32_t acc_count;      /* per-channel samples accumulated */
    uint64_t acc_start;      /* index of acc[0] */
    bool next_frame_discontinuity; /* set by gaps (E-03) */
    ea_queue q;
    ea_events ev;
    struct ea_history *hist; /* NULL when history_capacity == 0 (O-1) */
    int16_t *pop_copy;       /* frame view returned by ea_pop (C-01) */
};

/* queue.c */
void ea_queue_init(ea_queue *q, ea_slot_hdr *hdrs, int16_t *samples,
                   uint32_t capacity);
void ea_queue_push(struct ea_stream *s, uint64_t first_index, bool disc,
                   bool clipped, bool silent, const int16_t *frame_samples);
bool ea_queue_pop(struct ea_stream *s, ea_slot_hdr *hdr_out, int16_t *samples_out);
void ea_queue_flush_loss(struct ea_stream *s); /* K-04: run open at stop/fault/close */

/* events.c */
void ea_events_push(ea_events *ev, const ea_event *e);
size_t ea_events_poll(ea_events *ev, ea_event *out, size_t max_events);

/* history.c (W3) */
struct ea_history *ea_history_create(uint32_t capacity_samples,
                                     uint32_t channels);
void ea_history_destroy(struct ea_history *h);
void ea_history_write(struct ea_history *h, uint64_t first_index,
                      const int16_t *interleaved, uint32_t samples_per_channel);
void ea_history_note_gap(struct ea_history *h, uint64_t first_lost,
                         uint64_t lost_samples);
ea_status ea_history_read(struct ea_history *h, uint64_t first_index,
                          uint32_t sample_count, int16_t *out,
                          ea_unavail_reason *reason);

#endif /* EA_INTERNAL_H */
