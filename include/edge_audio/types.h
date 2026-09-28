/*
 * Intent: edge-audio public types (W1).
 * Pins the spec's data contracts: C-01 AudioFrame, C-04 Event, C-06 StreamConfig,
 * C-07 StreamInfo, and the error categories of E-02, E-04, E-09.
 * Format is always PCM_S16LE (D-08); frame duration is fixed at 20 ms (K-06, D-02).
 */
#ifndef EDGE_AUDIO_TYPES_H
#define EDGE_AUDIO_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Error categories (§5.2): E-04 invalid config, E-09 invalid state, E-02 unavailable. */
typedef enum {
    EA_OK = 0,
    EA_ERR_INVALID_CONFIG, /* E-04 */
    EA_ERR_INVALID_STATE,  /* E-09 */
    EA_ERR_UNAVAILABLE     /* E-02 (history reads) */
} ea_status;

/* C-03 UNAVAILABLE reasons, including SPANS_GAP (D-14). */
typedef enum {
    EA_UNAVAIL_EVICTED = 1,
    EA_UNAVAIL_NEVER_CAPTURED,
    EA_UNAVAIL_OUT_OF_RANGE,
    EA_UNAVAIL_SPANS_GAP
} ea_unavail_reason;

/* C-06 — StreamConfig. Format and frame duration are not fields (D-08, D-02). */
typedef struct {
    uint32_t sample_rate_hz;   /* profile-constrained (§7); K-06 integrality applies */
    uint8_t  channels;
    uint32_t queue_capacity;   /* frames (D-03) */
    uint32_t history_capacity; /* samples per channel; 0 = history disabled (O-1, D-07) */
} ea_stream_config;

/* C-01 — canonical audio frame. samples[] is interleaved int16 PCM_S16LE. */
typedef struct {
    uint32_t       stream_id;           /* boot-scoped, monotonic (C-01) */
    uint64_t       first_sample_index;  /* per-channel sample frames; R-01, R-07 */
    uint32_t       sample_rate_hz;      /* fixed per stream (R-11) */
    uint8_t        channels;            /* fixed per stream (R-11) */
    const int16_t *samples;             /* interleaved; valid until the next pop */
    uint32_t       samples_per_channel; /* == 0.02 * sample_rate_hz (K-06) */
    bool           discontinuity;       /* samples lost immediately before (I-002) */
    bool           clipped;             /* §7.1 clipping rule (K-05) */
    bool           silent;              /* §7.1 silence rule (K-05) */
    bool           has_wall_clock;      /* wall clock is metadata only (R-07) */
    uint64_t       wall_clock_us;
} ea_frame;

/* C-04 — structured event, per-kind at_sample and detail. */
typedef enum {
    EA_EVENT_QUEUE_OVERFLOW = 1,
    EA_EVENT_DISCONTINUITY,
    EA_EVENT_ADAPTER_FAULT
} ea_event_kind;

typedef struct {
    uint32_t      stream_id;
    ea_event_kind kind;
    uint64_t      at_sample;      /* per-kind definition in C-04 */
    uint32_t      events_dropped; /* events lost since last delivered one (E-08) */
    union {
        struct { uint32_t frames_dropped; } queue_overflow; /* episode total (K-04) */
        struct { uint64_t gap_samples; } discontinuity;     /* adapter-reported (C-05) */
        struct { uint32_t code; } adapter_fault;            /* opaque adapter value */
    } detail;
} ea_event;

/* C-07 — StreamInfo. transform_versions is empty when no replay-relevant
 * front-end transform is active (R-09; always empty on the SoundGuardian
 * profile, K-01). */
typedef struct {
    const char *name;
    const char *version;
} ea_transform_version;

typedef struct {
    uint32_t stream_id;
    uint32_t sample_rate_hz;
    uint8_t  channels;
    /* format is PCM_S16LE by construction (C-01, D-08) */
    const ea_transform_version *transform_versions;
    size_t                      transform_version_count;
} ea_stream_info;

/* §5.1 pop result: frame | empty | end_of_stream (E-06). */
typedef enum {
    EA_POP_FRAME = 0,
    EA_POP_EMPTY,
    EA_POP_END_OF_STREAM
} ea_pop_kind;

/* K-06: per-channel samples per frame for a valid rate (20 ms). */
#define EA_FRAME_SAMPLES(rate_hz) ((uint32_t)((rate_hz) / 50u))

#endif /* EDGE_AUDIO_TYPES_H */
