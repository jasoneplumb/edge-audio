/*
 * Intent: ALSA source adapter (C-05) for Linux hosts — the capture core of the
 * RK3576 harness's ea_capture (rk3576-measurement-harness@d6409d6), upstreamed.
 *
 * Contract carried over unchanged:
 *  - exact rate and channel negotiation, no software resampling (R-02, D-13);
 *    channel selection belongs to ALSA route PCMs, not to this adapter;
 *  - the PCM never stops on overrun: loss is measured from the hardware
 *    pointer, skipped with a forward, and reported as gap_samples on the next
 *    block (E-03, D-09 — counters, never wall clock);
 *  - each pushed block yields one anchor joining the capture sample clock to
 *    CLOCK_MONOTONIC (t_acquire plus the driver's position/htstamp pair);
 *  - an unrecoverable device error reaches ea_capture_fault (E-06).
 *
 * No ALSA type crosses this header (R-06): callers see only edge-audio types.
 *
 * Status mapping: the core has four categories (§5.2), none for resources or
 * devices, so ea_alsa_open reports a refused configuration, an unopenable
 * device and an allocation failure alike as EA_ERR_INVALID_CONFIG.
 */
#ifndef EA_ALSA_H
#define EA_ALSA_H

#include <stdint.h>

#include "edge_audio/edge_audio.h"

typedef struct ea_alsa ea_alsa;

/* Profile accepted by ea_alsa_adapter: 16 kHz, 1 or 2 channels. */
extern const ea_adapter_desc ea_alsa_adapter;

typedef struct {
    const char *device;       /* ALSA PCM name: "hw:1,0", a route view, ... */
    uint32_t    sample_rate_hz;
    uint8_t     channels;
    uint32_t    period_frames; /* 0 = one K-06 frame (20 ms) */
    uint32_t    buffer_frames; /* 0 = ten periods */
} ea_alsa_config;

/* What the device actually negotiated (for run records). */
typedef struct {
    uint32_t rate, channels, periods;
    uint64_t period_frames, buffer_frames, boundary;
} ea_alsa_params;

/* One per block, delivered before the block is pushed. Indices are on the
 * stream's capture clock (valid because ea_alsa_run is the stream's only
 * source, from its first sample). When the driver status read fails
 * (counters.status_failures increments), t_hw_ns, hw_position and backlog are
 * 0; end_index and t_acquire_ns are always valid. */
typedef struct {
    uint64_t end_index;    /* one past the block's last sample, gaps included */
    uint64_t hw_position;  /* capture-clock index the hardware had reached at t_hw_ns */
    int64_t  t_acquire_ns; /* CLOCK_MONOTONIC when the adapter held the block */
    int64_t  t_hw_ns;      /* driver MONOTONIC htstamp of hw_position */
    uint32_t backlog;      /* frames already captured behind this block */
} ea_alsa_anchor;

/* Why ea_alsa_run returned. */
typedef enum {
    EA_ALSA_END_NONE = 0,  /* not run yet */
    EA_ALSA_END_BLOCKS,    /* max_blocks reached */
    EA_ALSA_END_STOP,      /* ea_alsa_request_stop */
    EA_ALSA_END_EOF,       /* a finite source ended (e.g. the ALSA file plugin) */
    EA_ALSA_END_FAULT,     /* device error, reported via ea_capture_fault (E-06) */
    EA_ALSA_END_REFUSED    /* the stream refused a push (no longer RUNNING) */
} ea_alsa_end;

typedef struct {
    uint64_t    blocks, gap_samples, overrun_recoveries, short_reads, status_failures;
    uint32_t    max_backlog;
    uint32_t    partial_frames;     /* frames of an incomplete final block (EOF or a stop
                                     * interrupting a read), discarded, not pushed */
    int         fault_errno;        /* set when end == EA_ALSA_END_FAULT */
    ea_alsa_end end;
} ea_alsa_counters;

typedef void (*ea_alsa_anchor_fn)(const ea_alsa_anchor *a, void *user);

/* Opens and configures the PCM. EA_ERR_INVALID_CONFIG when the device cannot
 * provide exactly the requested rate/channels/S16_LE, or cannot be opened. */
ea_status ea_alsa_open(const ea_alsa_config *cfg, ea_alsa **out);
void      ea_alsa_params_get(const ea_alsa *a, ea_alsa_params *out);

/* Blocking capture loop, single-shot: pushes one K-06 frame per block into s
 * until max_blocks (0 = unbounded), a stop request, end of source, a device
 * fault, or a refused push. s must be RUNNING with the same rate/channels and
 * must receive no blocks from any other source (the anchors assume it).
 * on_anchor (may be NULL) runs before each push, so a consumer that pops the
 * frame always finds its anchor; the converse does not hold: after
 * EA_ALSA_END_REFUSED the last anchor describes a block the stream refused. Returns EA_ERR_INVALID_CONFIG on a
 * rate/channel mismatch, EA_ERR_INVALID_STATE on a second call, else EA_OK;
 * the reason is counters.end. */
ea_status ea_alsa_run(ea_alsa *a, ea_stream *s, uint64_t max_blocks,
                      ea_alsa_anchor_fn on_anchor, void *user);

/* Any thread, async-signal-safe. Observed between blocks and on EINTR: a
 * live device returns from a read within one period, but a stalled device
 * holds the read until a signal interrupts it. */
void ea_alsa_request_stop(ea_alsa *a);
void ea_alsa_counters_get(const ea_alsa *a, ea_alsa_counters *out);
void ea_alsa_close(ea_alsa *a);

/* Pure overrun rule: frames to skip when the hardware has overwritten unread
 * samples (avail > buffer) — the overwritten frames plus one period of margin;
 * 0 when there is no overrun. */
uint64_t ea_alsa_overrun_skip(uint64_t avail, uint64_t buffer, uint64_t period);

/* Device seam, in edge-audio-neutral types. ea_alsa_open binds it to ALSA;
 * a test binds a scripted device to drive the loss, fault, EOF and stop
 * paths without hardware. Errors are negative errno values. */
typedef struct {
    int     (*start)(void *dev);
    int64_t (*avail)(void *dev);                                  /* frames, syncs the hw pointer */
    int64_t (*forward)(void *dev, uint64_t frames);               /* frames skipped */
    int64_t (*read)(void *dev, int16_t *buf, uint32_t frames);    /* frames read; 0 = end of source */
    int     (*status)(void *dev, int64_t *t_hw_ns, uint32_t *avail);
    void    (*drop)(void *dev);
} ea_alsa_io;

/* Binds an already-configured device. params must satisfy ea_alsa_adapter's
 * profile; the device stays owned by the caller (ea_alsa_close does not
 * release it). */
ea_status ea_alsa_open_io(const ea_alsa_io *io, void *dev, const ea_alsa_params *params,
                          ea_alsa **out);

#endif /* EA_ALSA_H */
