/*
 * Intent: Stream lifecycle and capture core (realizes R-01, R-02 core side, R-03,
 * R-10, R-11; §3.1 state machine; §5.1 operations and validity matrix;
 * C-01, C-06, C-07; E-03, E-05, E-06, E-07, E-09; guards I-001..I-003).
 *
 * The sample-count clock is the only timing source (R-07): gaps advance it by
 * the adapter-reported amount (E-03, D-09), and a partial frame accumulation
 * that a gap interrupts cannot form a K-06-sized frame, so those samples are
 * lost and announced together with the gap (I-002).
 */
#include <stdatomic.h>
#include <string.h>

#include "internal.h"

static atomic_uint_fast32_t g_next_stream_id = 1; /* C-01: boot-scoped */

ea_status ea_open(const ea_stream_config *cfg, const ea_adapter_desc *adapter,
                  ea_stream **out)
{
    if (out == NULL)
        return EA_ERR_INVALID_CONFIG;
    *out = NULL;
    ea_status st = ea_config_validate(cfg, adapter); /* E-04 */
    if (st != EA_OK)
        return st;

    struct ea_stream *s = ea_port_alloc(sizeof(*s), EA_MEM_HOT);
    if (s == NULL)
        return EA_ERR_INVALID_CONFIG;
    s->cfg = *cfg;
    s->adapter = adapter;
    s->state = EA_ST_CONFIGURED;
    s->stream_id =
        (uint32_t)atomic_fetch_add(&g_next_stream_id, 1); /* monotonic */
    s->frame_samples = EA_FRAME_SAMPLES(cfg->sample_rate_hz); /* K-06 */
    s->frame_total = s->frame_samples * cfg->channels;

    s->acc = ea_port_alloc((size_t)s->frame_total * sizeof(int16_t), EA_MEM_HOT);
    s->pop_copy =
        ea_port_alloc((size_t)s->frame_total * sizeof(int16_t), EA_MEM_HOT);
    ea_slot_hdr *hdrs =
        ea_port_alloc((size_t)cfg->queue_capacity * sizeof(ea_slot_hdr),
                      EA_MEM_HOT);
    int16_t *slots = ea_port_alloc((size_t)cfg->queue_capacity *
                                       s->frame_total * sizeof(int16_t),
                                   EA_MEM_HOT);
    if (!s->acc || !s->pop_copy || !hdrs || !slots) {
        ea_port_free(s->acc);
        ea_port_free(s->pop_copy);
        ea_port_free(hdrs);
        ea_port_free(slots);
        ea_port_free(s);
        return EA_ERR_INVALID_CONFIG;
    }
    ea_queue_init(&s->q, hdrs, slots, cfg->queue_capacity);
    memset(&s->ev, 0, sizeof(s->ev));
    s->hist = (cfg->history_capacity > 0)
                  ? ea_history_create(cfg->history_capacity, cfg->channels)
                  : NULL; /* O-1 gate (R-04, D-07) */
    if (cfg->history_capacity > 0 && s->hist == NULL) {
        /* A requested capability that cannot be provisioned fails the open
         * (E-04 principle: no stream exists, no resources retained) rather
         * than silently degrading to a history-disabled stream (C-06). */
        ea_port_free(hdrs);
        ea_port_free(slots);
        ea_port_free(s->acc);
        ea_port_free(s->pop_copy);
        ea_port_free(s);
        return EA_ERR_INVALID_CONFIG;
    }
    ea_mutex_init(&s->lock);
    *out = s;
    return EA_OK;
}

ea_status ea_start(ea_stream *s)
{
    if (s == NULL)
        return EA_ERR_INVALID_STATE;
    ea_mutex_lock(&s->lock);
    if (s->state != EA_ST_CONFIGURED) { /* §5.1 matrix (E-09) */
        ea_mutex_unlock(&s->lock);
        return EA_ERR_INVALID_STATE;
    }
    s->state = EA_ST_RUNNING;
    s->clock = 0; /* §3.1: sample clock begins at index 0 */
    s->acc_start = 0;
    s->acc_count = 0;
    s->next_frame_discontinuity = false;
    ea_mutex_unlock(&s->lock);
    return EA_OK;
}

/* Announce and discard a partial accumulation that can never become a
 * K-06-sized frame (gap arrival, stop, fault) — loss is never hidden (I-002). */
static void discard_partial_acc(struct ea_stream *s, uint64_t extra_gap)
{
    uint64_t lost = (uint64_t)s->acc_count + extra_gap;
    if (lost == 0)
        return;
    uint64_t first_lost = s->acc_start;
    ea_event e = { 0 };
    e.stream_id = s->stream_id;
    e.kind = EA_EVENT_DISCONTINUITY;
    e.at_sample = first_lost; /* C-04: first lost sample index */
    e.detail.discontinuity.gap_samples = lost; /* C-05-derived (E-03) */
    ea_events_push(&s->ev, &e);
    if (s->hist)
        ea_history_note_gap(s->hist, first_lost, lost);
    s->acc_start = first_lost + lost; /* D-09: clock stays time-aligned */
    s->acc_count = 0;
    s->clock = s->acc_start;
    s->next_frame_discontinuity = true; /* E-03 */
}

static void emit_frame(struct ea_stream *s)
{
    bool clipped =
        ea_flags_clipped(s->acc, s->frame_samples, s->cfg.channels); /* R-03 */
    bool silent = ea_flags_silent(s->acc, s->frame_samples, s->cfg.channels);
    bool disc = s->next_frame_discontinuity;
    s->next_frame_discontinuity = false;
    ea_queue_push(s, s->acc_start, disc, clipped, silent, s->acc);
    if (s->hist) /* O-1: non-blocking ring write on this path (I-003) */
        ea_history_write(s->hist, s->acc_start, s->acc, s->frame_samples);
    s->acc_start += s->frame_samples; /* C-01 index equation */
    s->acc_count = 0;
    s->clock = s->acc_start;
}

ea_status ea_capture_push_block(ea_stream *s, const int16_t *interleaved,
                                uint32_t samples_per_channel,
                                uint64_t gap_samples_before)
{
    if (s == NULL)
        return EA_ERR_INVALID_STATE;
    ea_mutex_lock(&s->lock);
    if (s->state != EA_ST_RUNNING) {
        ea_mutex_unlock(&s->lock);
        return EA_ERR_INVALID_STATE;
    }
    if (samples_per_channel == 0) { /* E-05: discarded, no clock advance */
        ea_mutex_unlock(&s->lock);
        return EA_OK;
    }
    if (gap_samples_before > 0)
        discard_partial_acc(s, gap_samples_before); /* E-03 */

    uint32_t consumed = 0;
    while (consumed < samples_per_channel) {
        uint32_t take = s->frame_samples - s->acc_count;
        uint32_t remaining = samples_per_channel - consumed;
        if (take > remaining)
            take = remaining;
        memcpy(s->acc + (size_t)s->acc_count * s->cfg.channels,
               interleaved + (size_t)consumed * s->cfg.channels,
               (size_t)take * s->cfg.channels * sizeof(int16_t));
        s->acc_count += take;
        consumed += take;
        s->clock = s->acc_start + s->acc_count;
        if (s->acc_count == s->frame_samples)
            emit_frame(s);
    }
    ea_mutex_unlock(&s->lock);
    return EA_OK;
}

ea_status ea_capture_fault(ea_stream *s, uint32_t code)
{
    if (s == NULL)
        return EA_ERR_INVALID_STATE;
    ea_mutex_lock(&s->lock);
    if (s->state != EA_ST_RUNNING) {
        ea_mutex_unlock(&s->lock);
        return EA_ERR_INVALID_STATE;
    }
    discard_partial_acc(s, 0);
    ea_queue_flush_loss(s); /* K-04: open run emits before the transition */
    ea_event e = { 0 };
    e.stream_id = s->stream_id;
    e.kind = EA_EVENT_ADAPTER_FAULT;
    e.at_sample = s->clock; /* C-04: next index the clock would assign */
    e.detail.adapter_fault.code = code;
    ea_events_push(&s->ev, &e);
    s->state = EA_ST_FAULTED; /* E-06 */
    ea_mutex_unlock(&s->lock);
    return EA_OK;
}

ea_status ea_stop(ea_stream *s)
{
    if (s == NULL)
        return EA_ERR_INVALID_STATE;
    ea_mutex_lock(&s->lock);
    if (s->state != EA_ST_RUNNING) { /* §5.1 matrix (E-09) */
        ea_mutex_unlock(&s->lock);
        return EA_ERR_INVALID_STATE;
    }
    discard_partial_acc(s, 0);
    ea_queue_flush_loss(s); /* K-04 before the transition completes */
    s->state = EA_ST_STOPPED; /* §3.1: queued frames remain drainable */
    ea_mutex_unlock(&s->lock);
    return EA_OK;
}

ea_status ea_pop(ea_stream *s, ea_frame *out, ea_pop_kind *kind)
{
    if (s == NULL || out == NULL || kind == NULL)
        return EA_ERR_INVALID_STATE;
    ea_mutex_lock(&s->lock);
    if (s->state == EA_ST_CONFIGURED) { /* §5.1 matrix (E-09) */
        ea_mutex_unlock(&s->lock);
        return EA_ERR_INVALID_STATE;
    }
    ea_slot_hdr hdr;
    if (ea_queue_pop(s, &hdr, s->pop_copy)) {
        memset(out, 0, sizeof(*out));
        out->stream_id = s->stream_id;
        out->first_sample_index = hdr.first_index;
        out->sample_rate_hz = s->cfg.sample_rate_hz;
        out->channels = s->cfg.channels;
        out->samples = s->pop_copy; /* valid until the next pop (C-01) */
        out->samples_per_channel = s->frame_samples;
        out->discontinuity = hdr.discontinuity;
        out->clipped = hdr.clipped;
        out->silent = hdr.silent;
        *kind = EA_POP_FRAME;
    } else {
        *kind = (s->state == EA_ST_RUNNING) ? EA_POP_EMPTY
                                            : EA_POP_END_OF_STREAM; /* E-06 */
    }
    ea_mutex_unlock(&s->lock);
    return EA_OK;
}

ea_status ea_read_history(ea_stream *s, uint32_t stream_id,
                          uint64_t first_sample_index, uint32_t sample_count,
                          int16_t *out, ea_unavail_reason *reason)
{
    if (s == NULL || out == NULL || reason == NULL)
        return EA_ERR_INVALID_STATE;
    ea_mutex_lock(&s->lock);
    if (s->state == EA_ST_CONFIGURED) { /* §5.1 matrix (E-09) */
        ea_mutex_unlock(&s->lock);
        return EA_ERR_INVALID_STATE;
    }
    ea_status st;
    if (stream_id != s->stream_id || s->hist == NULL) {
        /* C-03: a stale stream_id — or disabled history — never captured
         * anything readable here (E-07). */
        *reason = EA_UNAVAIL_NEVER_CAPTURED;
        st = EA_ERR_UNAVAILABLE;
    } else {
        st = ea_history_read(s->hist, first_sample_index, sample_count, out,
                             reason);
    }
    ea_mutex_unlock(&s->lock);
    return st;
}

size_t ea_poll_events(ea_stream *s, ea_event *out, size_t max_events)
{
    if (s == NULL || out == NULL)
        return 0;
    ea_mutex_lock(&s->lock);
    size_t n = ea_events_poll(&s->ev, out, max_events); /* all states valid */
    ea_mutex_unlock(&s->lock);
    return n;
}

ea_status ea_stream_info_get(const ea_stream *s, ea_stream_info *out)
{
    if (s == NULL || out == NULL)
        return EA_ERR_INVALID_STATE;
    /* Constant after open (C-07, D-11); valid in every state (§5.1). */
    out->stream_id = s->stream_id;
    out->sample_rate_hz = s->cfg.sample_rate_hz;
    out->channels = s->cfg.channels;
    out->transform_versions = s->adapter->transform_versions; /* R-09 (O-2) */
    out->transform_version_count = s->adapter->transform_version_count;
    return EA_OK;
}

void ea_close(ea_stream *s)
{
    if (s == NULL)
        return;
    ea_mutex_lock(&s->lock);
    if (s->state == EA_ST_RUNNING) {
        /* §3.1: close from RUNNING implies stop first (K-04, F-014). */
        discard_partial_acc(s, 0);
        ea_queue_flush_loss(s);
        s->state = EA_ST_STOPPED;
    }
    ea_mutex_unlock(&s->lock);
    ea_mutex_destroy(&s->lock);
    if (s->hist)
        ea_history_destroy(s->hist); /* E-07: prior history unreadable */
    ea_port_free(s->q.hdrs);
    ea_port_free(s->q.samples);
    ea_port_free(s->acc);
    ea_port_free(s->pop_copy);
    ea_port_free(s);
}
