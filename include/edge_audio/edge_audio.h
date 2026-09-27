/*
 * edge-audio public API (W1 declares; W2/W3 realize).
 * Surfaces per §5.1: open, start, pop, read_history, poll_events, stream_info,
 * stop, close. The adapter boundary (C-05) is the ea_adapter_desc plus the
 * capture-side push/fault entry points.
 */
#ifndef EDGE_AUDIO_H
#define EDGE_AUDIO_H

#include "edge_audio/types.h"

typedef struct ea_stream ea_stream; /* opaque handle (D-01) */

/* C-05 — the per-platform part. config_supported enforces the active profile
 * (K-01 for SoundGuardian). transform_versions feed C-07 (R-09; O-2 gated). */
typedef struct {
    const char *name;
    bool (*config_supported)(const ea_stream_config *cfg);
    const ea_transform_version *transform_versions;
    size_t                      transform_version_count;
} ea_adapter_desc;

/* §5.1 operations. Return values use the §5.2 error categories. */
ea_status ea_open(const ea_stream_config *cfg, const ea_adapter_desc *adapter,
                  ea_stream **out); /* E-04 on invalid config */
ea_status ea_start(ea_stream *s);   /* clock starts at 0 (§3.1) */
ea_status ea_pop(ea_stream *s, ea_frame *out, ea_pop_kind *kind);
ea_status ea_read_history(ea_stream *s, uint32_t stream_id,
                          uint64_t first_sample_index, uint32_t sample_count,
                          int16_t *out, ea_unavail_reason *reason); /* C-03 */
size_t    ea_poll_events(ea_stream *s, ea_event *out, size_t max_events);
ea_status ea_stream_info_get(const ea_stream *s, ea_stream_info *out); /* C-07 */
ea_status ea_stop(ea_stream *s);
void      ea_close(ea_stream *s); /* valid from any state; RUNNING implies stop (§3.1) */

/* Capture-side entry points, called by the source adapter (C-05).
 * gap_samples_before: adapter-reported loss immediately before this block,
 * derived from hardware counters, never wall clock (E-03, D-09). A zero-length
 * block is discarded (E-05). */
ea_status ea_capture_push_block(ea_stream *s, const int16_t *interleaved,
                                uint32_t samples_per_channel,
                                uint64_t gap_samples_before);
ea_status ea_capture_fault(ea_stream *s, uint32_t code); /* E-06 */

/* §7.1 oracles (pure; W1). Exposed so tests and tools share the one formula. */
bool ea_flags_clipped(const int16_t *interleaved, uint32_t samples_per_channel,
                      uint8_t channels); /* K-05, D-06 */
bool ea_flags_silent(const int16_t *interleaved, uint32_t samples_per_channel,
                     uint8_t channels); /* K-05, D-05 */

/* C-06 validation (E-04, K-06). Returns EA_OK or EA_ERR_INVALID_CONFIG. */
ea_status ea_config_validate(const ea_stream_config *cfg,
                             const ea_adapter_desc *adapter);

#endif /* EDGE_AUDIO_H */
