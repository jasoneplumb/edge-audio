/*
 * Intent: WAV file source adapter (C-05): a RIFF/WAVE PCM file played into a
 * stream in REAL TIME, so a recorded corpus goes through the capture core
 * paced like a live device (issue #8; commonground-device arm B).
 *
 * Contract:
 *  - the file must already be PCM_S16LE at exactly the stream's rate and
 *    channels (R-02, D-13): any other format, rate, channel count or bit
 *    depth is refused at open; nothing is converted or resampled;
 *  - one K-06 block is released per frame period on an ABSOLUTE schedule of
 *    the clock (CLOCK_MONOTONIC unless a test injects one): block k is due at
 *    t0 + (k + 1) x 20 ms, the moment its last sample would have been
 *    captured live. A late block is released at once; its lateness is kept
 *    in max_late_ns, and in late_blocks once it is a whole period late. None
 *    is skipped, and lateness never accumulates as drift;
 *  - each released block yields one anchor, delivered before its push (so a
 *    consumer that pops the frame always finds its anchor), joining the
 *    capture sample clock (end_index) to the clock (t_acquire_ns, the
 *    release) and recording when the block was due (t_due_ns). As in
 *    adapters/alsa the converse does not hold: after EA_WAV_END_REFUSED the
 *    last anchor describes a block the stream refused;
 *  - an incomplete final block is counted (partial_frames), not pushed;
 *  - a read error reaches ea_capture_fault (E-06); so does an INJECTED fault
 *    after a chosen number of pushed blocks, which drives a real adapter down
 *    T-10's path (FAULTED, ADAPTER_FAULT, queued frames drain, end of stream).
 *    A stream that refuses the fault (no longer RUNNING) ends the run
 *    EA_WAV_END_REFUSED: FAULT and INJECTED_FAULT mean the fault was recorded;
 *  - the path must be seekable (a regular file): the header walk seeks past
 *    chunks, so a FIFO or pipe is refused at open.
 *
 * POSIX C11, no device library: it builds and tests on every CMake host.
 * Status mapping as adapters/alsa: a refused file, an unopenable path and an
 * allocation failure are all EA_ERR_INVALID_CONFIG (§5.2 has four categories).
 */
#ifndef EA_WAV_H
#define EA_WAV_H

#include <stdint.h>

#include "edge_audio/edge_audio.h"

typedef struct ea_wav ea_wav;

/* Profile accepted by ea_wav_adapter: a K-06-integral rate, 1 or 2 channels. */
extern const ea_adapter_desc ea_wav_adapter;

/* The clock seam. now_ns is monotonic nanoseconds; sleep_until may return
 * early (a signal, a stop) and the adapter re-checks. NULL in the config means
 * CLOCK_MONOTONIC with nanosleep. */
typedef struct {
    int64_t (*now_ns)(void *ctx);
    void    (*sleep_until)(void *ctx, int64_t t_ns);
    void     *ctx;
} ea_wav_clock;

typedef struct {
    const char         *path;
    uint32_t            sample_rate_hz;     /* must equal the file's */
    uint8_t             channels;           /* must equal the file's */
    uint64_t            fault_after_blocks; /* 0 = none; else ea_capture_fault after this many pushes */
    uint32_t            fault_code;         /* the ADAPTER_FAULT code an injected fault reports;
                                             * non-zero when fault_after_blocks is set */
    const ea_wav_clock *clock;              /* NULL = CLOCK_MONOTONIC */
} ea_wav_config;

/* What the file holds (for run records). */
typedef struct {
    uint32_t rate, channels;
    uint64_t data_frames;   /* sample frames in the data chunk, as its size declares */
} ea_wav_params;

/* One per block, delivered before the block is pushed. */
typedef struct {
    uint64_t end_index;     /* one past the block's last sample on the capture clock */
    int64_t  t_due_ns;      /* when the block was scheduled: t0 + blocks x frame period */
    int64_t  t_acquire_ns;  /* when the adapter released it (>= t_due_ns) */
} ea_wav_anchor;

/* Why ea_wav_run returned. */
typedef enum {
    EA_WAV_END_NONE = 0,       /* not run yet */
    EA_WAV_END_BLOCKS,         /* max_blocks reached */
    EA_WAV_END_STOP,           /* ea_wav_request_stop */
    EA_WAV_END_EOF,            /* the data chunk ended */
    EA_WAV_END_FAULT,          /* a read error, reported via ea_capture_fault (E-06) */
    EA_WAV_END_INJECTED_FAULT, /* fault_after_blocks reached, reported via ea_capture_fault */
    EA_WAV_END_REFUSED         /* the stream refused a push (no longer RUNNING) */
} ea_wav_end;

typedef struct {
    uint64_t   blocks;          /* pushed */
    uint64_t   late_blocks;     /* released a whole frame period or more after due */
    int64_t    max_late_ns;     /* the largest t_acquire_ns - t_due_ns */
    uint32_t   partial_frames;  /* frames of an incomplete final block, discarded */
    int        fault_errno;     /* the read error that ended the run (FAULT, or REFUSED if the
                                 * stream refused to record it); 0 otherwise */
    ea_wav_end end;
} ea_wav_counters;

typedef void (*ea_wav_anchor_fn)(const ea_wav_anchor *a, void *user);

/* Opens the file and checks its format against cfg. EA_ERR_INVALID_CONFIG
 * when it cannot be read, is not RIFF/WAVE PCM (format tag 1) S16_LE, does
 * not match the rate and channel count exactly, has a metadata chunk over
 * 1 MiB or no data chunk within its first 64 chunks, or asks for an injected
 * fault with code 0. A data chunk shorter than its declared size simply ends
 * early (EOF, partial_frames). */
ea_status ea_wav_open(const ea_wav_config *cfg, ea_wav **out);
void      ea_wav_params_get(const ea_wav *w, ea_wav_params *out);

/* Blocking, single-shot: pushes one K-06 block per frame period into s until
 * max_blocks (0 = unbounded), a stop request, the end of the data, a read
 * error, an injected fault, or a refused push. s must be RUNNING with the
 * same rate and channels and must receive no blocks from any other source.
 * on_anchor (may be NULL) runs before each push. Returns
 * EA_ERR_INVALID_CONFIG on a rate/channel mismatch, EA_ERR_INVALID_STATE on a
 * second call, else EA_OK; the reason is counters.end. */
ea_status ea_wav_run(ea_wav *w, ea_stream *s, uint64_t max_blocks,
                     ea_wav_anchor_fn on_anchor, void *user);

/* Any thread, async-signal-safe. Observed before each block and while waiting
 * for one to fall due, so a stop takes effect within one frame period. */
void ea_wav_request_stop(ea_wav *w);
void ea_wav_counters_get(const ea_wav *w, ea_wav_counters *out);
void ea_wav_close(ea_wav *w);

#endif /* EA_WAV_H */
