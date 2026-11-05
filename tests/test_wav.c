/*
 * Intent: WAV file source adapter tests (adapters/wav; issue #8). They prove
 * the spec ids each test names (C-05, R-02, D-13, E-04, E-06, K-06, T-10)
 * through a real adapter, and the adapter-local contract of wav.h, which no
 * SPEC id covers: real-time pacing, anchors, lateness, stop and the header
 * walk (labelled "adapter-local"). Files go to a per-process directory under
 * $TMPDIR (or /tmp), each removed at exit; time runs on a scripted clock, so
 * the schedule, lateness and stops are exact and nothing waits in real time.
 * One short test runs on the real CLOCK_MONOTONIC to show the pacing is real.
 * Not covered: the big-endian byte swap, which no little-endian host can run.
 */
#define _POSIX_C_SOURCE 200809L
#include "../adapters/wav/wav.h"
#include "support/test.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RATE 16000u
#define FRAME EA_FRAME_SAMPLES(RATE) /* K-06: 320 */
#define PERIOD_NS 20000000LL         /* K-06: 20 ms */
#define T0 1000000000LL              /* the scripted clock's start */

static char g_dir[512]; /* $TMPDIR/ea_wav_test.<pid>: mkdir, since mkdtemp is not strict POSIX on every host */
static char g_made[64][1024]; /* every file written, removed at exit */
static int  g_made_n;

static void remove_files(void)
{
    for (int i = 0; i < g_made_n; i++)
        unlink(g_made[i]);
    rmdir(g_dir);
}

static int16_t sample_at(uint32_t i, uint32_t ch)
{
    return (int16_t)((i * 7u + ch * 1000u) % 30000u) - 15000; /* distinct per index/channel */
}

/* ---- files ------------------------------------------------------------- */

typedef struct {
    uint16_t tag, channels, bits;
    uint32_t rate;
    int      extra_chunk;  /* an odd-sized LIST chunk before fmt */
    int      data_first;   /* data before fmt */
    int      no_data;
    int      not_riff;
    uint32_t fmt_extra;    /* bytes past the 16 of a plain PCM fmt (cbSize...), padded when odd */
    uint32_t data_declared;/* 0 = the true size; else the size the header claims */
    uint32_t list_size;    /* 0 = 3; else the LIST chunk's declared size (nothing follows it) */
    int      list_chunks;  /* how many LIST chunks precede fmt (with extra_chunk) */
    uint16_t align_set;    /* 0 = consistent; else the block_align written */
    uint32_t byte_rate_set;/* 0 = consistent; else the byte_rate written */
    int      fmt_short;    /* a 14-byte fmt chunk (no bits-per-sample) */
} wav_shape;

static void put32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((int)(v >> (8 * i)) & 0xff, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc(v >> 8, f); }

static void write_fmt(FILE *f, const wav_shape *w)
{
    const uint16_t align = w->align_set ? w->align_set : (uint16_t)(w->channels * w->bits / 8);
    fwrite("fmt ", 1, 4, f);
    put32(f, w->fmt_short ? 14 : 16 + w->fmt_extra);
    put16(f, w->tag);
    put16(f, w->channels);
    put32(f, w->rate);
    put32(f, w->byte_rate_set ? w->byte_rate_set : w->rate * (uint32_t)(w->channels * w->bits / 8));
    put16(f, align);
    if (w->fmt_short)
        return;
    put16(f, w->bits);
    for (uint32_t i = 0; i < w->fmt_extra + (w->fmt_extra & 1u); i++)
        fputc(0, f);
}

static void write_data(FILE *f, const wav_shape *w, uint32_t frames)
{
    fwrite("data", 1, 4, f);
    put32(f, w->data_declared ? w->data_declared : frames * w->channels * 2u);
    for (uint32_t i = 0; i < frames; i++)
        for (uint32_t c = 0; c < w->channels; c++)
            put16(f, (uint16_t)sample_at(i, c));
}

static const char *write_wav(const char *name, const wav_shape *w, uint32_t frames)
{
    static char path[1024];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return NULL;
    if (g_made_n < 64)
        snprintf(g_made[g_made_n++], sizeof g_made[0], "%s", path);
    fwrite(w->not_riff ? "RIFX" : "RIFF", 1, 4, f);
    put32(f, 0); /* the RIFF size is not read; the chunks are walked */
    fwrite("WAVE", 1, 4, f);
    for (int k = 0; w->extra_chunk && k < (w->list_chunks ? w->list_chunks : 1); k++) {
        fwrite("LIST", 1, 4, f);
        put32(f, w->list_size ? w->list_size : 3);
        if (!w->list_size)
            fwrite("abc\0", 1, 4, f); /* 3 bytes and the pad byte */
    }
    if (w->data_first)
        write_data(f, w, frames);
    write_fmt(f, w);
    if (!w->data_first && !w->no_data)
        write_data(f, w, frames);
    fclose(f);
    return path;
}

static wav_shape pcm(uint16_t channels)
{
    return (wav_shape){ 1, channels, 16, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
}

/* ---- the scripted clock --------------------------------------------------- */

typedef struct {
    int64_t  now;
    int64_t  overshoot;  /* each sleep wakes this late */
    uint32_t sleeps;
    ea_wav  *stop_target;
    uint32_t stop_on_sleep; /* request a stop during this sleep (1-based); 0 = never */
} fake_clock;

static int64_t fake_now(void *ctx) { return ((fake_clock *)ctx)->now; }

static void fake_sleep_until(void *ctx, int64_t t)
{
    fake_clock *c = ctx;
    c->sleeps++;
    if (c->stop_on_sleep && c->sleeps == c->stop_on_sleep) {
        ea_wav_request_stop(c->stop_target); /* the wake is early: the stop interrupts it */
        return;
    }
    if (t > c->now)
        c->now = t + c->overshoot;
}

/* ---- anchors ------------------------------------------------------------- */

typedef struct {
    uint32_t       count;
    ea_wav_anchor  a[64];
    fake_clock    *clock;
    uint32_t       stall_at;  /* the consumer holds the source this long at this anchor */
    int64_t        stall_ns;
    ea_wav        *stop_target;
    uint32_t       stop_at;
    ea_stream     *refuse_s;
    uint32_t       refuse_at;
} anchors;

static void on_anchor(const ea_wav_anchor *an, void *user)
{
    anchors *s = user;
    if (s->count < 64)
        s->a[s->count] = *an;
    s->count++;
    if (s->stall_at && s->count == s->stall_at)
        s->clock->now += s->stall_ns;
    if (s->stop_at && s->count == s->stop_at)
        ea_wav_request_stop(s->stop_target);
    if (s->refuse_at && s->count == s->refuse_at)
        ea_stop(s->refuse_s);
}

static ea_stream *stream(uint8_t channels)
{
    ea_stream_config sc = { RATE, channels, 64, 0 };
    ea_stream *s = NULL;
    if (ea_open(&sc, &ea_wav_adapter, &s) != EA_OK || ea_start(s) != EA_OK) {
        ea_close(s);
        return NULL;
    }
    return s;
}

static ea_wav *open_wav(const char *path, uint8_t channels, fake_clock *c, uint64_t fault_after, uint32_t code)
{
    static ea_wav_clock clk;
    clk = (ea_wav_clock){ fake_now, fake_sleep_until, c };
    ea_wav_config cfg = { path, RATE, channels, fault_after, code, c ? &clk : NULL };
    ea_wav *w = NULL;
    return ea_wav_open(&cfg, &w) == EA_OK ? w : NULL;
}

/* Pops every queued frame and checks it against the file, bit for bit. */
static uint32_t drain_exact(ea_stream *s, uint8_t channels)
{
    uint32_t frames = 0;
    ea_frame f;
    ea_pop_kind kind;
    while (ea_pop(s, &f, &kind) == EA_OK && kind == EA_POP_FRAME) {
        EA_CHECK_EQ(f.first_sample_index, (uint64_t)frames * FRAME);
        EA_CHECK_EQ(f.samples_per_channel, FRAME);
        EA_CHECK(!f.discontinuity);
        int same = 1;
        for (uint32_t i = 0; i < FRAME; i++)
            for (uint32_t c = 0; c < channels; c++)
                same &= f.samples[i * channels + c] == sample_at(frames * FRAME + i, c);
        EA_CHECK(same);
        frames++;
    }
    return frames;
}

/* ---- tests --------------------------------------------------------------- */

static void delivers_exactly_on_the_absolute_schedule(uint8_t channels) /* R-02, K-06 */
{
    wav_shape shape = pcm(channels);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("exact.wav", &shape, 16 * FRAME), channels, &c, 0, 0);
    ea_stream *s = stream(channels);
    EA_CHECK(w && s);
    if (!w || !s)
        goto out;
    ea_wav_params p;
    ea_wav_params_get(w, &p);
    EA_CHECK_EQ(p.data_frames, 16 * FRAME);
    anchors an = { 0 };
    EA_CHECK_EQ(ea_wav_run(w, s, 0, on_anchor, &an), EA_OK);
    ea_wav_counters n;
    ea_wav_counters_get(w, &n);
    EA_CHECK_EQ(n.end, EA_WAV_END_EOF);
    EA_CHECK_EQ(n.blocks, 16);
    EA_CHECK_EQ(n.partial_frames, 0);
    EA_CHECK_EQ(n.late_blocks, 0);
    EA_CHECK_EQ(an.count, 16);
    for (uint32_t k = 0; k < 16; k++) {
        EA_CHECK_EQ(an.a[k].end_index, (uint64_t)(k + 1) * FRAME);
        EA_CHECK_EQ(an.a[k].t_due_ns, T0 + (int64_t)(k + 1) * PERIOD_NS);
        EA_CHECK_EQ(an.a[k].t_acquire_ns, an.a[k].t_due_ns);
    }
    EA_CHECK_EQ(drain_exact(s, channels), 16);
out:
    ea_close(s);
    ea_wav_close(w);
}

static void mono_bit_exact(void) { delivers_exactly_on_the_absolute_schedule(1); }
static void stereo_bit_exact(void) { delivers_exactly_on_the_absolute_schedule(2); }

static void lateness_never_accumulates(void) /* adapter-local */
{
    /* Each wake is 3 ms late: the due times stay on the 20 ms grid. */
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 3000000, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("jitter.wav", &shape, 10 * FRAME), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (!w || !s)
        goto out;
    anchors an = { 0 };
    ea_wav_run(w, s, 0, on_anchor, &an);
    ea_wav_counters n;
    ea_wav_counters_get(w, &n);
    EA_CHECK_EQ(n.blocks, 10);
    EA_CHECK_EQ(an.a[9].t_due_ns, T0 + 10 * PERIOD_NS);          /* no drift */
    EA_CHECK_EQ(an.a[9].t_acquire_ns - an.a[9].t_due_ns, 3000000);
    EA_CHECK_EQ(n.max_late_ns, 3000000);
    EA_CHECK_EQ(n.late_blocks, 0);
out:
    ea_close(s);
    ea_wav_close(w);
}

static void a_stalled_consumer_makes_blocks_late_but_none_is_skipped(void) /* adapter-local */
{
    /* The consumer holds block 5 for 50 ms: block 6 (due +120 ms) goes at +150
     * (30 ms late, counted), block 7 (due +140) at once, block 8 back on time. */
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("stall.wav", &shape, 10 * FRAME), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (!w || !s)
        goto out;
    anchors an = { 0 };
    an.clock = &c;
    an.stall_at = 5;
    an.stall_ns = 50000000;
    ea_wav_run(w, s, 0, on_anchor, &an);
    ea_wav_counters n;
    ea_wav_counters_get(w, &n);
    EA_CHECK_EQ(n.blocks, 10);
    EA_CHECK_EQ(an.a[5].t_acquire_ns - an.a[5].t_due_ns, 30000000);
    EA_CHECK_EQ(an.a[6].t_acquire_ns - an.a[6].t_due_ns, 10000000);
    EA_CHECK_EQ(an.a[7].t_acquire_ns, an.a[7].t_due_ns);
    EA_CHECK_EQ(n.late_blocks, 1);
    EA_CHECK_EQ(n.max_late_ns, 30000000);
    EA_CHECK_EQ(drain_exact(s, 1), 10);
out:
    ea_close(s);
    ea_wav_close(w);
}

static void eof_counts_partial_block(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("partial.wav", &shape, 3 * FRAME + 100), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (!w || !s)
        goto out;
    ea_wav_run(w, s, 0, NULL, NULL);
    ea_wav_counters n;
    ea_wav_counters_get(w, &n);
    EA_CHECK_EQ(n.end, EA_WAV_END_EOF);
    EA_CHECK_EQ(n.blocks, 3);
    EA_CHECK_EQ(n.partial_frames, 100);
    EA_CHECK_EQ(drain_exact(s, 1), 3);
out:
    ea_close(s);
    ea_wav_close(w);
}

static void injected_fault_takes_t10_path(void) /* E-06, T-10 */
{
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("fault.wav", &shape, 10 * FRAME), 1, &c, 5, 77);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (!w || !s)
        goto out;
    EA_CHECK_EQ(ea_wav_run(w, s, 0, NULL, NULL), EA_OK);
    ea_wav_counters n;
    ea_wav_counters_get(w, &n);
    EA_CHECK_EQ(n.end, EA_WAV_END_INJECTED_FAULT);
    EA_CHECK_EQ(n.blocks, 5);
    EA_CHECK_EQ(n.fault_errno, 0); /* injected, not a read error */
    ea_event ev[4];
    size_t k = ea_poll_events(s, ev, 4);
    EA_CHECK_EQ(k, 1);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_ADAPTER_FAULT);
    EA_CHECK_EQ(ev[0].detail.adapter_fault.code, 77);
    EA_CHECK_EQ(ev[0].at_sample, 5 * FRAME);
    EA_CHECK_EQ(drain_exact(s, 1), 5); /* queued frames drain ... */
    ea_frame f;
    ea_pop_kind kind;
    EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
    EA_CHECK_EQ(kind, EA_POP_END_OF_STREAM); /* ... then end of stream */
    EA_CHECK_EQ(ea_start(s), EA_ERR_INVALID_STATE); /* FAULTED */
out:
    ea_close(s);
    ea_wav_close(w);
}

static void formats_other_than_the_stream_are_refused(void) /* R-02, D-13, E-04, K-06 */
{
    struct { const char *name; wav_shape shape; uint8_t channels; } cases[] = {
        { "rate.wav",       { 1, 1, 16, 48000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1 },
        { "channels.wav",   { 1, 2, 16, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  1 },
        { "bits8.wav",      { 1, 1, 8, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   1 },
        { "bits24.wav",     { 1, 1, 24, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  1 },
        { "float.wav",      { 3, 1, 16, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  1 },
        { "extensible.wav", { 0xFFFE, 1, 16, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1 },
        { "rifx.wav",       { 1, 1, 16, RATE, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },  1 },
        { "nodata.wav",     { 1, 1, 16, RATE, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 },  1 },
        { "datafirst.wav",  { 1, 1, 16, RATE, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  1 },
        { "byterate.wav",   { 1, 1, 16, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 31999, 0 }, 1 },
        { "align.wav",      { 1, 1, 16, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0 },     1 },
        { "fmt14.wav",      { 1, 1, 16, RATE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },     1 },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        const char *path = write_wav(cases[i].name, &cases[i].shape, FRAME);
        ea_wav_config cfg = { path, RATE, cases[i].channels, 0, 0, NULL };
        static int sentinel;
        ea_wav *w = (ea_wav *)(void *)&sentinel; /* must be cleared on refusal */
        EA_CHECK_EQ(ea_wav_open(&cfg, &w), EA_ERR_INVALID_CONFIG);
        EA_CHECK(w == NULL);
    }
    char missing[1024];
    snprintf(missing, sizeof missing, "%s/missing.wav", g_dir);
    ea_wav_config cfg = { missing, RATE, 1, 0, 0, NULL };
    ea_wav *w = NULL;
    EA_CHECK_EQ(ea_wav_open(&cfg, &w), EA_ERR_INVALID_CONFIG);
    cfg.sample_rate_hz = 16001; /* fails K-06 integrality */
    EA_CHECK_EQ(ea_wav_open(&cfg, &w), EA_ERR_INVALID_CONFIG);
    wav_shape good = pcm(1);
    const char *good_path = write_wav("good.wav", &good, FRAME);
    struct { uint32_t rate; uint8_t channels; } profile[] = { { 0, 1 }, { RATE, 0 }, { RATE, 3 } }; /* E-04 */
    for (size_t i = 0; i < sizeof profile / sizeof *profile; i++) {
        ea_wav_config bad = { good_path, profile[i].rate, profile[i].channels, 0, 0, NULL };
        EA_CHECK_EQ(ea_wav_open(&bad, &w), EA_ERR_INVALID_CONFIG);
    }
    EA_CHECK_EQ(ea_wav_open(NULL, &w), EA_ERR_INVALID_CONFIG);
    EA_CHECK_EQ(ea_wav_open(&cfg, NULL), EA_ERR_INVALID_CONFIG);
    wav_shape ok = pcm(1);
    ea_wav_config no_code = { write_wav("nocode.wav", &ok, FRAME), RATE, 1, 5, 0, NULL };
    EA_CHECK_EQ(ea_wav_open(&no_code, &w), EA_ERR_INVALID_CONFIG); /* injected fault needs a code */
    ea_wav_clock half = { fake_now, NULL, NULL };
    wav_shape shape = pcm(1);
    ea_wav_config bad_clock = { write_wav("clock.wav", &shape, FRAME), RATE, 1, 0, 0, &half };
    EA_CHECK_EQ(ea_wav_open(&bad_clock, &w), EA_ERR_INVALID_CONFIG);
}

static void a_real_read_error_faults(void) /* E-06, T-10 */
{
    /* The adapter's FILE takes the lowest free descriptor at open; swapping a
     * write-only descriptor in under it makes the next read fail (EBADF). */
    wav_shape shape = pcm(1);
    const char *path = write_wav("readerr.wav", &shape, 10 * FRAME);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    const int lowest = dup(0);
    close(lowest);
    ea_wav *w = open_wav(path, 1, &c, 0, 0);
    ea_stream *s = stream(1);
    const int wronly = open(path, O_WRONLY);
    EA_CHECK(w && s && wronly >= 0);
    if (w && s && wronly >= 0 && dup2(wronly, lowest) == lowest) {
        ea_wav_run(w, s, 0, NULL, NULL);
        ea_wav_counters n;
        ea_wav_counters_get(w, &n);
        EA_CHECK_EQ(n.end, EA_WAV_END_FAULT);
        EA_CHECK(n.blocks < 10); /* whatever stdio had already buffered went first */
        EA_CHECK(n.fault_errno != 0);
        ea_event ev[4];
        EA_CHECK_EQ(ea_poll_events(s, ev, 4), 1);
        EA_CHECK_EQ(ev[0].kind, EA_EVENT_ADAPTER_FAULT);
        EA_CHECK_EQ(ev[0].detail.adapter_fault.code, (uint32_t)n.fault_errno);
        EA_CHECK_EQ(drain_exact(s, 1), n.blocks); /* T-10: queued frames drain ... */
        ea_frame f;
        ea_pop_kind kind;
        EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
        EA_CHECK_EQ(kind, EA_POP_END_OF_STREAM); /* ... then end of stream */
    }
    if (wronly >= 0)
        close(wronly);
    ea_close(s);
    ea_wav_close(w);
}

static void a_data_chunk_shorter_than_declared_ends_early(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    shape.data_declared = 100 * FRAME * 2; /* claims 100 blocks; 2 and a half are there */
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("short.wav", &shape, 2 * FRAME + 50), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        ea_wav_params p;
        ea_wav_params_get(w, &p);
        EA_CHECK_EQ(p.data_frames, 100 * FRAME); /* as declared */
        ea_wav_run(w, s, 0, NULL, NULL);
        ea_wav_counters n;
        ea_wav_counters_get(w, &n);
        EA_CHECK_EQ(n.end, EA_WAV_END_EOF);
        EA_CHECK_EQ(n.blocks, 2);
        EA_CHECK_EQ(n.partial_frames, 50);
        EA_CHECK_EQ(drain_exact(s, 1), 2);
    }
    ea_close(s);
    ea_wav_close(w);
}

static void an_empty_data_chunk_pushes_nothing(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("empty.wav", &shape, 0), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        ea_wav_run(w, s, 0, NULL, NULL);
        ea_wav_counters n;
        ea_wav_counters_get(w, &n);
        EA_CHECK_EQ(n.end, EA_WAV_END_EOF);
        EA_CHECK_EQ(n.blocks, 0);
        EA_CHECK_EQ(n.partial_frames, 0);
        EA_CHECK_EQ(c.sleeps, 0);
    }
    ea_close(s);
    ea_wav_close(w);
}

static void longer_and_odd_fmt_chunks_are_read(void) /* adapter-local */
{
    uint32_t extras[] = { 2, 1 }; /* cbSize = 0 (18 bytes); an odd 17 with its pad */
    for (size_t i = 0; i < 2; i++) {
        wav_shape shape = pcm(1);
        shape.fmt_extra = extras[i];
        fake_clock c = { T0, 0, 0, NULL, 0 };
        ea_wav *w = open_wav(write_wav(i ? "fmt17.wav" : "fmt18.wav", &shape, 2 * FRAME), 1, &c, 0, 0);
        ea_stream *s = stream(1);
        EA_CHECK(w && s);
        if (w && s) {
            ea_wav_run(w, s, 0, NULL, NULL);
            EA_CHECK_EQ(drain_exact(s, 1), 2);
        }
        ea_close(s);
        ea_wav_close(w);
    }
}

static void a_hostile_header_cannot_loop_or_seek_back(void) /* adapter-local */
{
    wav_shape huge = pcm(1);
    huge.extra_chunk = 1;
    huge.list_size = 0xFFFFFFFFu; /* would be -1 as a 32-bit long */
    wav_shape many = pcm(1);
    many.extra_chunk = 1;
    many.list_chunks = 64;        /* fmt is the 65th chunk */
    const wav_shape *shapes[] = { &huge, &many };
    const char *names[] = { "huge.wav", "many.wav" };
    for (int i = 0; i < 2; i++) {
        ea_wav_config cfg = { write_wav(names[i], shapes[i], FRAME), RATE, 1, 0, 0, NULL };
        ea_wav *w = NULL;
        EA_CHECK_EQ(ea_wav_open(&cfg, &w), EA_ERR_INVALID_CONFIG);
    }
}

static void other_chunks_are_skipped(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    shape.extra_chunk = 1;
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("list.wav", &shape, 2 * FRAME), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        ea_wav_run(w, s, 0, NULL, NULL);
        EA_CHECK_EQ(drain_exact(s, 1), 2);
    }
    ea_close(s);
    ea_wav_close(w);
}

static void stream_mismatch_and_second_run_refused(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("mismatch.wav", &shape, 2 * FRAME), 1, &c, 0, 0);
    ea_stream *stereo = stream(2), *mono = stream(1);
    EA_CHECK(w && stereo && mono);
    if (w && stereo && mono) {
        EA_CHECK_EQ(ea_wav_run(w, stereo, 0, NULL, NULL), EA_ERR_INVALID_CONFIG);
        EA_CHECK_EQ(ea_wav_run(w, mono, 0, NULL, NULL), EA_OK); /* a refused run did not spend it */
        EA_CHECK_EQ(ea_wav_run(w, mono, 0, NULL, NULL), EA_ERR_INVALID_STATE);
        EA_CHECK_EQ(ea_wav_run(NULL, mono, 0, NULL, NULL), EA_ERR_INVALID_CONFIG);
        EA_CHECK_EQ(ea_wav_run(w, NULL, 0, NULL, NULL), EA_ERR_INVALID_CONFIG);
    }
    ea_close(stereo);
    ea_close(mono);
    ea_wav_close(w);
    ea_wav_close(NULL);
}

static void max_blocks_ends_the_run(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("max.wav", &shape, 10 * FRAME), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        ea_wav_run(w, s, 4, NULL, NULL);
        ea_wav_counters n;
        ea_wav_counters_get(w, &n);
        EA_CHECK_EQ(n.end, EA_WAV_END_BLOCKS);
        EA_CHECK_EQ(n.blocks, 4);
    }
    ea_close(s);
    ea_wav_close(w);
}

static void stop_after_a_block_and_while_waiting(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    /* a stop at an anchor: that block is pushed, then the run ends */
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("stop.wav", &shape, 10 * FRAME), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        anchors an = { 0 };
        an.stop_target = w;
        an.stop_at = 3;
        ea_wav_run(w, s, 0, on_anchor, &an);
        ea_wav_counters n;
        ea_wav_counters_get(w, &n);
        EA_CHECK_EQ(n.end, EA_WAV_END_STOP);
        EA_CHECK_EQ(n.blocks, 3);
    }
    ea_close(s);
    ea_wav_close(w);
    /* a stop during the wait for block 2: that block is never released */
    fake_clock c2 = { T0, 0, 0, NULL, 2 };
    w = open_wav(write_wav("stopwait.wav", &shape, 10 * FRAME), 1, &c2, 0, 0);
    s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        c2.stop_target = w;
        ea_wav_run(w, s, 0, NULL, NULL);
        ea_wav_counters n;
        ea_wav_counters_get(w, &n);
        EA_CHECK_EQ(n.end, EA_WAV_END_STOP);
        EA_CHECK_EQ(n.blocks, 1);
        EA_CHECK_EQ(drain_exact(s, 1), 1);
    }
    ea_close(s);
    ea_wav_close(w);
}

static void refused_push_ends_run(void) /* adapter-local */
{
    wav_shape shape = pcm(1);
    fake_clock c = { T0, 0, 0, NULL, 0 };
    ea_wav *w = open_wav(write_wav("refuse.wav", &shape, 10 * FRAME), 1, &c, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        anchors an = { 0 };
        an.refuse_s = s;
        an.refuse_at = 4;
        ea_wav_run(w, s, 0, on_anchor, &an);
        ea_wav_counters n;
        ea_wav_counters_get(w, &n);
        EA_CHECK_EQ(n.end, EA_WAV_END_REFUSED);
        EA_CHECK_EQ(n.blocks, 3);
        EA_CHECK_EQ(an.count, 4); /* wav.h: the last anchor describes the refused block */
    }
    ea_close(s);
    ea_wav_close(w);
}

static int64_t real_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static void real_clock_paces_in_real_time(void) /* adapter-local */
{
    /* Five blocks cannot be released before 100 ms have passed: the lower
     * bound is exact; the upper bound only catches a source that never waits
     * on a loaded host's scale. */
    wav_shape shape = pcm(1);
    ea_wav *w = open_wav(write_wav("real.wav", &shape, 5 * FRAME), 1, NULL, 0, 0);
    ea_stream *s = stream(1);
    EA_CHECK(w && s);
    if (w && s) {
        anchors an = { 0 };
        const int64_t t = real_now();
        ea_wav_run(w, s, 0, on_anchor, &an);
        const int64_t elapsed = real_now() - t;
        EA_CHECK(elapsed >= 5 * PERIOD_NS);
        EA_CHECK(elapsed < 10000000000LL); /* generous: only a source that never waits fails */
        EA_CHECK_EQ(an.count, 5);
        for (uint32_t k = 0; k < an.count && k < 5; k++)
            EA_CHECK(an.a[k].t_acquire_ns >= an.a[k].t_due_ns);
        EA_CHECK_EQ(an.a[4].t_due_ns - an.a[0].t_due_ns, 4 * PERIOD_NS);
    }
    ea_close(s);
    ea_wav_close(w);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(g_dir, sizeof g_dir, "%s/ea_wav_test.%ld", tmp && *tmp ? tmp : "/tmp", (long)getpid());
    if (mkdir(g_dir, 0700) != 0) {
        perror(g_dir);
        return 2;
    }
    atexit(remove_files);
    EA_RUN(mono_bit_exact);
    EA_RUN(stereo_bit_exact);
    EA_RUN(lateness_never_accumulates);
    EA_RUN(a_stalled_consumer_makes_blocks_late_but_none_is_skipped);
    EA_RUN(eof_counts_partial_block);
    EA_RUN(injected_fault_takes_t10_path);
    EA_RUN(formats_other_than_the_stream_are_refused);
    EA_RUN(a_real_read_error_faults);
    EA_RUN(a_data_chunk_shorter_than_declared_ends_early);
    EA_RUN(an_empty_data_chunk_pushes_nothing);
    EA_RUN(longer_and_odd_fmt_chunks_are_read);
    EA_RUN(a_hostile_header_cannot_loop_or_seek_back);
    EA_RUN(other_chunks_are_skipped);
    EA_RUN(stream_mismatch_and_second_run_refused);
    EA_RUN(max_blocks_ends_the_run);
    EA_RUN(stop_after_a_block_and_while_waiting);
    EA_RUN(refused_push_ends_run);
    EA_RUN(real_clock_paces_in_real_time);
    return EA_TEST_SUMMARY();
}
