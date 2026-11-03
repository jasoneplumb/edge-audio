/*
 * Intent: ALSA source adapter implementation (C-05, E-03, E-06, D-09); the
 * hardware half of adapters/alsa — the only file here that includes ALSA.
 * The capture loop runs over the ea_alsa_io seam; the ALSA binding is below it.
 */
#define _POSIX_C_SOURCE 200809L
#include "alsa.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "ea_alsa_request_stop must be async-signal-safe");

#define EA_ALSA_RATE 16000u
#define EA_ALSA_MAX_CHANNELS 2u

struct ea_alsa {
    const ea_alsa_io *io;
    void             *dev;
    snd_pcm_t        *owned_pcm; /* set when ea_alsa_open created the device */
    ea_alsa_params    params;
    ea_alsa_counters  n;
    int16_t          *buf;       /* one K-06 frame, interleaved, sized at open */
    atomic_int        stop;      /* set from any thread or a signal handler */
    bool              ran;       /* ea_alsa_run is single-shot */
};

static bool alsa_supported(const ea_stream_config *cfg)
{
    return cfg->sample_rate_hz == EA_ALSA_RATE && cfg->channels >= 1 &&
           cfg->channels <= EA_ALSA_MAX_CHANNELS;
}

const ea_adapter_desc ea_alsa_adapter = { "linux-alsa", alsa_supported, NULL, 0 };

uint64_t ea_alsa_overrun_skip(uint64_t avail, uint64_t buffer, uint64_t period)
{
    return avail > buffer ? avail - buffer + period : 0;
}

static int64_t mono_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

/* ---- ALSA binding of the seam ------------------------------------------ */

static int     pcm_start(void *d) { return snd_pcm_start(d); }
static int64_t pcm_avail(void *d) { return snd_pcm_avail(d); }
static int64_t pcm_forward(void *d, uint64_t n) { return snd_pcm_forward(d, (snd_pcm_uframes_t)n); }
static int64_t pcm_read(void *d, int16_t *b, uint32_t n) { return snd_pcm_readi(d, b, n); }
static void    pcm_drop(void *d) { snd_pcm_drop(d); }

static int pcm_status(void *d, int64_t *t_hw_ns, uint32_t *avail)
{
    snd_pcm_status_t *st;
    snd_pcm_status_alloca(&st);
    int err = snd_pcm_status(d, st);
    if (err < 0)
        return err;
    snd_htimestamp_t ts;
    snd_pcm_status_get_htstamp(st, &ts);
    *t_hw_ns = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
    *avail = (uint32_t)snd_pcm_status_get_avail(st);
    return 0;
}

static const ea_alsa_io alsa_io = { pcm_start, pcm_avail, pcm_forward,
                                    pcm_read,  pcm_status, pcm_drop };

static int configure(snd_pcm_t *pcm, const ea_alsa_config *cfg, ea_alsa_params *p)
{
    snd_pcm_hw_params_t *hw;
    snd_pcm_sw_params_t *sw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_sw_params_alloca(&sw);
    const uint32_t frame = EA_FRAME_SAMPLES(cfg->sample_rate_hz);
    snd_pcm_uframes_t period = cfg->period_frames ? cfg->period_frames : frame;
    snd_pcm_uframes_t buffer = cfg->buffer_frames ? cfg->buffer_frames : 10 * period;
    snd_pcm_uframes_t boundary = 0;
    unsigned rate = 0, channels = 0, periods = 0;
    int dir = 0, err;
#define EA_TRY(call)                                                           \
    do {                                                                       \
        if ((err = (call)) < 0)                                                \
            return err;                                                        \
    } while (0)
    EA_TRY(snd_pcm_hw_params_any(pcm, hw));
    EA_TRY(snd_pcm_hw_params_set_rate_resample(pcm, hw, 0)); /* no software resampling */
    EA_TRY(snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED));
    EA_TRY(snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE));
    EA_TRY(snd_pcm_hw_params_set_channels(pcm, hw, cfg->channels));
    EA_TRY(snd_pcm_hw_params_set_rate(pcm, hw, cfg->sample_rate_hz, 0)); /* exact, or refuse */
    EA_TRY(snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, &dir));
    EA_TRY(snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer));
    EA_TRY(snd_pcm_hw_params(pcm, hw));
    snd_pcm_hw_params_get_rate(hw, &rate, &dir);
    snd_pcm_hw_params_get_channels(hw, &channels);
    snd_pcm_hw_params_get_period_size(hw, &period, &dir);
    snd_pcm_hw_params_get_buffer_size(hw, &buffer);
    snd_pcm_hw_params_get_periods(hw, &periods, &dir);
    if (rate != cfg->sample_rate_hz || channels != cfg->channels)
        return -EINVAL;
    EA_TRY(snd_pcm_sw_params_current(pcm, sw));
    EA_TRY(snd_pcm_sw_params_get_boundary(sw, &boundary));
    EA_TRY(snd_pcm_sw_params_set_avail_min(pcm, sw, frame));
    EA_TRY(snd_pcm_sw_params_set_start_threshold(pcm, sw, 1));
    /* never stop on overrun: the hardware pointer keeps counting, so loss is
     * measured from it (E-03) */
    EA_TRY(snd_pcm_sw_params_set_stop_threshold(pcm, sw, boundary));
    EA_TRY(snd_pcm_sw_params_set_tstamp_mode(pcm, sw, SND_PCM_TSTAMP_ENABLE));
    EA_TRY(snd_pcm_sw_params_set_tstamp_type(pcm, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC));
    EA_TRY(snd_pcm_sw_params(pcm, sw));
#undef EA_TRY
    *p = (ea_alsa_params){ rate, channels, periods, period, buffer, boundary };
    return 0;
}

/* ---- adapter ------------------------------------------------------------ */

ea_status ea_alsa_open_io(const ea_alsa_io *io, void *dev, const ea_alsa_params *params,
                          ea_alsa **out)
{
    if (!out)
        return EA_ERR_INVALID_CONFIG;
    *out = NULL;
    if (!io || !params || params->channels > EA_ALSA_MAX_CHANNELS)
        return EA_ERR_INVALID_CONFIG;
    const ea_stream_config probe = { params->rate, (uint8_t)params->channels, 1, 0 };
    if (!alsa_supported(&probe))
        return EA_ERR_INVALID_CONFIG;
    ea_alsa *a = calloc(1, sizeof(*a));
    if (!a)
        return EA_ERR_INVALID_CONFIG;
    a->buf = malloc((size_t)EA_FRAME_SAMPLES(params->rate) * params->channels * sizeof(int16_t));
    if (!a->buf) {
        free(a);
        return EA_ERR_INVALID_CONFIG;
    }
    a->io = io;
    a->dev = dev;
    a->params = *params;
    atomic_init(&a->stop, 0);
    *out = a;
    return EA_OK;
}

ea_status ea_alsa_open(const ea_alsa_config *cfg, ea_alsa **out)
{
    if (!out)
        return EA_ERR_INVALID_CONFIG;
    *out = NULL;
    if (!cfg || !cfg->device)
        return EA_ERR_INVALID_CONFIG;
    const ea_stream_config probe = { cfg->sample_rate_hz, cfg->channels, 1, 0 };
    if (!alsa_supported(&probe))
        return EA_ERR_INVALID_CONFIG;
    snd_pcm_t *pcm;
    ea_alsa_params p;
    if (snd_pcm_open(&pcm, cfg->device, SND_PCM_STREAM_CAPTURE, 0) < 0)
        return EA_ERR_INVALID_CONFIG;
    if (configure(pcm, cfg, &p) < 0 || ea_alsa_open_io(&alsa_io, pcm, &p, out) != EA_OK) {
        snd_pcm_close(pcm);
        return EA_ERR_INVALID_CONFIG;
    }
    (*out)->owned_pcm = pcm;
    return EA_OK;
}

void ea_alsa_params_get(const ea_alsa *a, ea_alsa_params *out)
{
    *out = a->params;
}

static ea_alsa_end fault(ea_alsa *a, ea_stream *s, int64_t err)
{
    a->n.fault_errno = (int)(err < 0 ? -err : err);
    ea_capture_fault(s, (uint32_t)a->n.fault_errno); /* E-06 */
    return EA_ALSA_END_FAULT;
}

/* Fills one K-06 block. Returns EA_ALSA_END_NONE when the block is complete. */
static ea_alsa_end read_block(ea_alsa *a, ea_stream *s, uint32_t frame)
{
    const uint32_t ch = a->params.channels;
    uint32_t got = 0;
    while (got < frame) {
        int64_t r = a->io->read(a->dev, a->buf + (size_t)got * ch, frame - got);
        if (r == -EINTR) {
            if (atomic_load(&a->stop)) { /* a requested stop interrupted the read: not a fault */
                a->n.partial_frames = got;
                return EA_ALSA_END_STOP;
            }
            continue;
        }
        if (r < 0)
            return fault(a, s, r);
        if (r == 0) { /* end of a finite source: an incomplete block is counted, not pushed */
            a->n.partial_frames = got;
            return EA_ALSA_END_EOF;
        }
        if ((uint32_t)r < frame - got)
            a->n.short_reads++;
        got += (uint32_t)r;
    }
    return EA_ALSA_END_NONE;
}

ea_status ea_alsa_run(ea_alsa *a, ea_stream *s, uint64_t max_blocks,
                      ea_alsa_anchor_fn on_anchor, void *user)
{
    if (!a || !s)
        return EA_ERR_INVALID_CONFIG;
    if (a->ran)
        return EA_ERR_INVALID_STATE;
    ea_stream_info info;
    if (ea_stream_info_get(s, &info) != EA_OK || info.sample_rate_hz != a->params.rate ||
        info.channels != a->params.channels)
        return EA_ERR_INVALID_CONFIG;
    a->ran = true;
    const uint32_t frame = EA_FRAME_SAMPLES(a->params.rate);
    int err = a->io->start(a->dev);
    if (err < 0) {
        a->n.end = fault(a, s, err);
        return EA_OK;
    }
    uint64_t clock_end = 0, gap = 0;
    ea_alsa_end end = EA_ALSA_END_NONE;
    while (end == EA_ALSA_END_NONE) {
        if (max_blocks && a->n.blocks == max_blocks) {
            end = EA_ALSA_END_BLOCKS;
            break;
        }
        if (atomic_load(&a->stop)) {
            end = EA_ALSA_END_STOP;
            break;
        }
        int64_t avail = a->io->avail(a->dev);
        if (avail < 0) {
            end = fault(a, s, avail);
            break;
        }
        uint64_t skip = ea_alsa_overrun_skip((uint64_t)avail, a->params.buffer_frames,
                                             a->params.period_frames);
        if (skip) {
            int64_t fwd = a->io->forward(a->dev, skip);
            if (fwd < 0) {
                end = fault(a, s, fwd);
                break;
            }
            gap += (uint64_t)fwd;
            a->n.gap_samples += (uint64_t)fwd;
            a->n.overrun_recoveries++;
        }
        if ((end = read_block(a, s, frame)) != EA_ALSA_END_NONE)
            break;
        ea_alsa_anchor an = { 0 };
        an.t_acquire_ns = mono_ns();
        clock_end += gap + frame;
        an.end_index = clock_end;
        if (a->io->status(a->dev, &an.t_hw_ns, &an.backlog) == 0) {
            an.hw_position = clock_end + an.backlog;
        } else {
            an.t_hw_ns = 0;
            an.backlog = 0;
            a->n.status_failures++;
        }
        if (an.backlog > a->n.max_backlog)
            a->n.max_backlog = an.backlog;
        if (on_anchor)
            on_anchor(&an, user);
        /* A refused push means the stream left RUNNING; nothing further can be
         * delivered, so the run ends here rather than drift from the clock. */
        if (ea_capture_push_block(s, a->buf, frame, gap) != EA_OK) {
            end = EA_ALSA_END_REFUSED;
            break;
        }
        a->n.blocks++;
        gap = 0;
    }
    a->io->drop(a->dev);
    a->n.end = end;
    return EA_OK;
}

void ea_alsa_request_stop(ea_alsa *a)
{
    atomic_store(&a->stop, 1);
}

void ea_alsa_counters_get(const ea_alsa *a, ea_alsa_counters *out)
{
    *out = a->n;
}

void ea_alsa_close(ea_alsa *a)
{
    if (!a)
        return;
    if (a->owned_pcm)
        snd_pcm_close(a->owned_pcm);
    free(a->buf);
    free(a);
}
