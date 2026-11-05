/*
 * Intent: WAV file source adapter implementation (C-05, R-02, E-06; issue
 * #8): a RIFF/WAVE reader and the real-time release loop over the
 * ea_wav_clock seam. POSIX C11 only.
 */
#define _POSIX_C_SOURCE 200809L
#include "wav.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "ea_wav_request_stop must be async-signal-safe");

#define EA_WAV_MAX_CHANNELS 2u
#define EA_WAV_FORMAT_PCM 1u
/* Bounds on the header walk: a hostile file can neither seek backwards (a
 * size past LONG_MAX on a 32-bit long) nor make the parser walk forever. */
#define EA_WAV_MAX_CHUNKS 64
#define EA_WAV_MAX_SKIP (1ul << 20) /* metadata chunks are small; a larger one is refused */

struct ea_wav {
    FILE            *f;
    ea_wav_params    params;
    ea_wav_clock     clock;
    uint64_t         fault_after_blocks;
    uint32_t         fault_code;
    uint64_t         data_left;   /* bytes of the data chunk not yet read */
    ea_wav_counters  n;
    int16_t         *buf;         /* one K-06 frame, interleaved */
    atomic_int       stop;
    bool             ran;
    bool             swap;        /* host is big-endian: WAV samples are little-endian */
};

static bool wav_supported(const ea_stream_config *cfg)
{
    return cfg->sample_rate_hz > 0 && cfg->sample_rate_hz % 50u == 0 && /* K-06 */
           cfg->channels >= 1 && cfg->channels <= EA_WAV_MAX_CHANNELS;
}

const ea_adapter_desc ea_wav_adapter = { "wav-file", wav_supported, NULL, 0 };

/* ---- the default clock: CLOCK_MONOTONIC --------------------------------- */

static int64_t mono_now(void *ctx)
{
    (void)ctx;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

/* Sleeps toward an ABSOLUTE target: the remaining time is recomputed from the
 * clock each call, so a wake-up that is early (a signal) or late (scheduling)
 * never carries into the next block's deadline. */
static void mono_sleep_until(void *ctx, int64_t t_ns)
{
    int64_t rem = t_ns - mono_now(ctx);
    if (rem <= 0)
        return;
    struct timespec d = { (time_t)(rem / 1000000000), (long)(rem % 1000000000) };
    nanosleep(&d, NULL); /* EINTR returns early; the caller re-checks */
}

static const ea_wav_clock mono_clock = { mono_now, mono_sleep_until, NULL };

/* ---- RIFF/WAVE ----------------------------------------------------------- */

static uint32_t le32(const uint8_t *b) { return b[0] | b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }

/* Walks the chunks to "data", checking "fmt " against cfg on the way. Leaves
 * the file positioned at the first sample. */
static bool read_header(FILE *f, const ea_wav_config *cfg, ea_wav_params *p, uint64_t *data_bytes)
{
    uint8_t h[12];
    if (fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return false;
    bool have_fmt = false;
    for (int chunk = 0; chunk < EA_WAV_MAX_CHUNKS; chunk++) {
        uint8_t c[8];
        if (fread(c, 1, sizeof c, f) != sizeof c)
            return false;                     /* no data chunk */
        const uint32_t size = le32(c + 4);
        if (memcmp(c, "data", 4) && size > EA_WAV_MAX_SKIP)
            return false;
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t fmt[16];
            if (size < sizeof fmt || fread(fmt, 1, sizeof fmt, f) != sizeof fmt)
                return false;
            const uint16_t tag = le16(fmt), channels = le16(fmt + 2), align = le16(fmt + 12),
                           bits = le16(fmt + 14);
            const uint32_t rate = le32(fmt + 4), byte_rate = le32(fmt + 8);
            if (tag != EA_WAV_FORMAT_PCM || bits != 16 || channels != cfg->channels ||
                rate != cfg->sample_rate_hz || align != 2u * channels ||
                (uint64_t)byte_rate != (uint64_t)rate * align)
                return false;                 /* R-02: exactly the stream's format, or refuse */
            p->rate = rate;
            p->channels = channels;
            have_fmt = true;
            if (fseek(f, (long)(size - sizeof fmt + (size & 1u)), SEEK_CUR) != 0)
                return false;
        } else if (!memcmp(c, "data", 4)) {
            if (!have_fmt)
                return false;                 /* data before fmt: not a file this reads */
            *data_bytes = size;
            p->data_frames = size / (2u * p->channels);
            return true;
        } else if (fseek(f, (long)size + (long)(size & 1u), SEEK_CUR) != 0) {
            return false;                     /* any other chunk is skipped (word-aligned) */
        }
    }
    return false;                             /* no data chunk within the bound */
}

ea_status ea_wav_open(const ea_wav_config *cfg, ea_wav **out)
{
    if (!out)
        return EA_ERR_INVALID_CONFIG;
    *out = NULL;
    if (!cfg || !cfg->path)
        return EA_ERR_INVALID_CONFIG;
    const ea_stream_config probe = { cfg->sample_rate_hz, cfg->channels, 1, 0 };
    if (!wav_supported(&probe))
        return EA_ERR_INVALID_CONFIG;
    if (cfg->clock && (!cfg->clock->now_ns || !cfg->clock->sleep_until))
        return EA_ERR_INVALID_CONFIG;
    if (cfg->fault_after_blocks && cfg->fault_code == 0)
        return EA_ERR_INVALID_CONFIG;         /* an injected fault must be told from "no code" */
    ea_wav *w = calloc(1, sizeof(*w));
    if (!w)
        return EA_ERR_INVALID_CONFIG;
    w->buf = malloc((size_t)EA_FRAME_SAMPLES(cfg->sample_rate_hz) * cfg->channels * sizeof(int16_t));
    if (!w->buf) {
        ea_wav_close(w);
        return EA_ERR_INVALID_CONFIG;
    }
    w->f = fopen(cfg->path, "rb");
    if (!w->f) {
        ea_wav_close(w);
        return EA_ERR_INVALID_CONFIG;
    }
    if (!read_header(w->f, cfg, &w->params, &w->data_left)) {
        ea_wav_close(w);
        return EA_ERR_INVALID_CONFIG;
    }
    w->clock = cfg->clock ? *cfg->clock : mono_clock;
    w->fault_after_blocks = cfg->fault_after_blocks;
    w->fault_code = cfg->fault_code;
    const uint16_t probe_endian = 1;
    w->swap = *(const uint8_t *)&probe_endian == 0;
    atomic_init(&w->stop, 0);
    *out = w;
    return EA_OK;
}

void ea_wav_params_get(const ea_wav *w, ea_wav_params *out)
{
    *out = w->params;
}

/* E-06. A stream that has already left RUNNING (a concurrent stop) refuses
 * the fault: no ADAPTER_FAULT was emitted, so the run ends REFUSED, never
 * claiming a fault the stream did not record. */
static ea_wav_end fault(ea_wav *w, ea_stream *s, int err)
{
    w->n.fault_errno = err ? err : EIO;
    return ea_capture_fault(s, (uint32_t)w->n.fault_errno) == EA_OK ? EA_WAV_END_FAULT : EA_WAV_END_REFUSED;
}

/* Reads one K-06 block. EA_WAV_END_NONE when the block is complete. */
static ea_wav_end read_block(ea_wav *w, ea_stream *s, uint32_t frame)
{
    const uint32_t ch = w->params.channels;
    const uint64_t want = (uint64_t)frame * ch * sizeof(int16_t);
    const size_t take = (size_t)(w->data_left < want ? w->data_left : want);
    errno = 0;
    const size_t got = fread(w->buf, 1, take, w->f);
    if (got < take && ferror(w->f))
        return fault(w, s, errno);
    w->data_left -= got;
    if (got < want) { /* the data ended: an incomplete block is counted, not pushed */
        w->n.partial_frames = (uint32_t)(got / (ch * sizeof(int16_t)));
        return EA_WAV_END_EOF;
    }
    if (w->swap)
        for (uint32_t i = 0; i < frame * ch; i++)
            w->buf[i] = (int16_t)(((uint16_t)w->buf[i] >> 8) | ((uint16_t)w->buf[i] << 8));
    return EA_WAV_END_NONE;
}

ea_status ea_wav_run(ea_wav *w, ea_stream *s, uint64_t max_blocks,
                     ea_wav_anchor_fn on_anchor, void *user)
{
    if (!w || !s)
        return EA_ERR_INVALID_CONFIG;
    if (w->ran)
        return EA_ERR_INVALID_STATE;
    ea_stream_info info;
    if (ea_stream_info_get(s, &info) != EA_OK || info.sample_rate_hz != w->params.rate ||
        info.channels != w->params.channels)
        return EA_ERR_INVALID_CONFIG;
    w->ran = true;
    const uint32_t frame = EA_FRAME_SAMPLES(w->params.rate);
    const int64_t period_ns = (int64_t)frame * 1000000000 / w->params.rate; /* 20 ms, exactly (K-06) */
    const int64_t t0 = w->clock.now_ns(w->clock.ctx);
    ea_wav_end end = EA_WAV_END_NONE;
    while (end == EA_WAV_END_NONE) {
        if (max_blocks && w->n.blocks == max_blocks) {
            end = EA_WAV_END_BLOCKS;
            break;
        }
        if (atomic_load(&w->stop)) {
            end = EA_WAV_END_STOP;
            break;
        }
        if ((end = read_block(w, s, frame)) != EA_WAV_END_NONE)
            break;
        ea_wav_anchor an;
        an.t_due_ns = t0 + (int64_t)(w->n.blocks + 1) * period_ns; /* absolute: no drift */
        int64_t now;
        while ((now = w->clock.now_ns(w->clock.ctx)) < an.t_due_ns && !atomic_load(&w->stop))
            w->clock.sleep_until(w->clock.ctx, an.t_due_ns);
        if (atomic_load(&w->stop)) { /* a block not yet due is never released */
            end = EA_WAV_END_STOP;
            break;
        }
        an.t_acquire_ns = now;
        an.end_index = (w->n.blocks + 1) * frame;
        const int64_t late = now - an.t_due_ns;
        if (late > w->n.max_late_ns)
            w->n.max_late_ns = late;
        if (late >= period_ns)
            w->n.late_blocks++;
        if (on_anchor)
            on_anchor(&an, user);
        if (ea_capture_push_block(s, w->buf, frame, 0) != EA_OK) {
            end = EA_WAV_END_REFUSED;
            break;
        }
        w->n.blocks++;
        if (w->fault_after_blocks && w->n.blocks == w->fault_after_blocks)
            end = ea_capture_fault(s, w->fault_code) == EA_OK /* E-06, injected */
                      ? EA_WAV_END_INJECTED_FAULT
                      : EA_WAV_END_REFUSED;
    }
    w->n.end = end;
    return EA_OK;
}

void ea_wav_request_stop(ea_wav *w)
{
    atomic_store(&w->stop, 1);
}

void ea_wav_counters_get(const ea_wav *w, ea_wav_counters *out)
{
    *out = w->n;
}

void ea_wav_close(ea_wav *w)
{
    if (!w)
        return;
    if (w->f)
        fclose(w->f);
    free(w->buf);
    free(w);
}
