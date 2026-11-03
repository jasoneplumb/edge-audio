/*
 * Intent: ALSA source adapter tests (adapters/alsa; C-05, R-02, E-03, E-04, E-06).
 * Two hardware-free halves:
 *  - the real ALSA binding, through the "file" plugin's infile over a "null"
 *    slave: negotiation and bit-exact delivery of a known PCM file;
 *  - the capture loop over a scripted ea_alsa_io device: overrun gap, device
 *    faults, end of source, short reads, stop, refused push, status failure.
 * Real-device timing (overrun under load, htstamps) stays with the board
 * bring-up gate that uses this adapter.
 */
#define _POSIX_C_SOURCE 200809L
#include "../adapters/alsa/alsa.h"
#include "support/test.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define RATE 16000u
#define FRAME EA_FRAME_SAMPLES(RATE) /* K-06 */
#define BLOCKS 16u

static char g_dir[] = "/tmp/ea_alsa_testXXXXXX";
static char g_raw_mono[256], g_raw_stereo[256], g_conf[256];

static int16_t sample_at(uint32_t i, uint32_t ch)
{
    return (int16_t)((i * 7u + ch * 1000u) % 30000u) - 15000; /* distinct per index/channel */
}

/* ---- shared checks ---------------------------------------------------- */

typedef struct {
    uint32_t count;
    uint64_t last_end;
    int64_t  last_t;
    int      ordered;   /* end_index strictly increasing, t_acquire non-decreasing */
    ea_alsa_anchor last;
    ea_alsa  *stop_at_target;
    uint32_t stop_at;   /* request a stop at this anchor (1-based); 0 = never */
    ea_stream *refuse_s;
    uint32_t refuse_at; /* stop the stream at this anchor, so its push is refused */
} anchors;

static void on_anchor(const ea_alsa_anchor *an, void *user)
{
    anchors *s = user;
    if (an->end_index <= s->last_end || an->t_acquire_ns < s->last_t || an->t_acquire_ns <= 0)
        s->ordered = 0;
    s->last_end = an->end_index;
    s->last_t = an->t_acquire_ns;
    s->last = *an;
    s->count++;
    if (s->stop_at && s->count == s->stop_at)
        ea_alsa_request_stop(s->stop_at_target);
    if (s->refuse_at && s->count == s->refuse_at)
        ea_stop(s->refuse_s);
}

static ea_stream *running(uint8_t channels, uint32_t queue_cap)
{
    ea_stream_config sc = { RATE, channels, queue_cap, 0 };
    ea_stream *s = NULL;
    if (ea_open(&sc, &ea_alsa_adapter, &s) != EA_OK || ea_start(s) != EA_OK)
        return NULL;
    return s;
}

/* Pops every queued frame; checks contiguous content from sample_at(). */
static uint32_t drain(ea_stream *s, uint8_t channels, int *exact, ea_pop_kind *last_kind)
{
    uint32_t frames = 0, src = 0;
    *exact = 1;
    for (;;) {
        ea_frame f;
        ea_pop_kind kind;
        if (ea_pop(s, &f, &kind) != EA_OK || kind != EA_POP_FRAME) {
            *last_kind = kind;
            break;
        }
        for (uint32_t i = 0; i < FRAME; i++)
            for (uint32_t c = 0; c < channels; c++)
                if (f.samples[i * channels + c] != sample_at(src + i, c))
                    *exact = 0;
        src += FRAME;
        frames++;
    }
    return frames;
}

/* ---- real ALSA binding via the file plugin ---------------------------- */

static int write_raw(const char *path, uint32_t channels)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    for (uint32_t i = 0; i < BLOCKS * FRAME; i++)
        for (uint32_t c = 0; c < channels; c++) {
            int16_t v = sample_at(i, c);
            fwrite(&v, sizeof v, 1, f); /* little-endian test host */
        }
    return fclose(f);
}

/* ALSA reads its configuration once per process, so the test PCMs are
 * defined before the first ALSA call (ALSA_CONFIG_PATH replaces alsa.conf). */
static int write_config(void)
{
    FILE *f = fopen(g_conf, "w");
    if (!f)
        return -1;
    for (int stereo = 0; stereo < 2; stereo++)
        fprintf(f,
                "pcm.ea_file_%s { type file slave.pcm { type null } file \"/dev/null\" "
                "infile \"%s\" format raw }\n",
                stereo ? "stereo" : "mono", stereo ? g_raw_stereo : g_raw_mono);
    fclose(f);
    return setenv("ALSA_CONFIG_PATH", g_conf, 1);
}

static void overrun_rule(void) /* E-03: the skip amount is counter-derived */
{
    EA_CHECK_EQ(ea_alsa_overrun_skip(0, 3200, 320), 0);
    EA_CHECK_EQ(ea_alsa_overrun_skip(3200, 3200, 320), 0);   /* full but not overwritten */
    EA_CHECK_EQ(ea_alsa_overrun_skip(3300, 3200, 320), 420); /* overwritten + one period */
}

static void profile_gate(void) /* E-04 */
{
    ea_stream_config ok1 = { RATE, 1, 4, 0 }, ok2 = { RATE, 2, 4, 0 };
    ea_stream_config bad_rate = { 48000, 1, 4, 0 }, bad_ch = { RATE, 3, 4, 0 };
    EA_CHECK(ea_alsa_adapter.config_supported(&ok1));
    EA_CHECK(ea_alsa_adapter.config_supported(&ok2));
    EA_CHECK(!ea_alsa_adapter.config_supported(&bad_rate));
    EA_CHECK(!ea_alsa_adapter.config_supported(&bad_ch));
    ea_alsa *a = (ea_alsa *)1; /* must be cleared on refusal */
    ea_alsa_config c48 = { "ea_file_mono", 48000, 1, 0, 0 };
    EA_CHECK_EQ(ea_alsa_open(&c48, &a), EA_ERR_INVALID_CONFIG);
    EA_CHECK(a == NULL);
    ea_alsa_config missing = { "ea_no_such_pcm", RATE, 1, 0, 0 };
    EA_CHECK_EQ(ea_alsa_open(&missing, &a), EA_ERR_INVALID_CONFIG);
}

static void file_bit_exact(uint8_t channels) /* T-01 shape via the adapter; R-02 */
{
    ea_alsa_config cfg = { channels == 2 ? "ea_file_stereo" : "ea_file_mono", RATE, channels, 0, 0 };
    ea_alsa *a = NULL;
    EA_CHECK_EQ(ea_alsa_open(&cfg, &a), EA_OK);
    ea_stream *s = running(channels, BLOCKS + 1);
    if (!a || !s)
        return;
    ea_alsa_params p;
    ea_alsa_params_get(a, &p);
    EA_CHECK_EQ(p.rate, RATE);
    EA_CHECK_EQ(p.channels, channels);
    anchors an = { .ordered = 1 };
    EA_CHECK_EQ(ea_alsa_run(a, s, BLOCKS, on_anchor, &an), EA_OK);
    EA_CHECK_EQ(an.count, BLOCKS);
    EA_CHECK(an.ordered);
    EA_CHECK_EQ(an.last.end_index, (uint64_t)BLOCKS * FRAME);
    int exact;
    ea_pop_kind k;
    EA_CHECK_EQ(drain(s, channels, &exact, &k), BLOCKS);
    EA_CHECK(exact);
    ea_event ev[4];
    EA_CHECK_EQ(ea_poll_events(s, ev, 4), 0); /* no gap, no overflow, no fault */
    ea_alsa_counters n;
    ea_alsa_counters_get(a, &n);
    EA_CHECK_EQ(n.blocks, BLOCKS);
    EA_CHECK_EQ(n.gap_samples, 0);
    EA_CHECK_EQ(n.end, EA_ALSA_END_BLOCKS);
    EA_CHECK_EQ(ea_alsa_run(a, s, 1, NULL, NULL), EA_ERR_INVALID_STATE); /* single-shot */
    ea_close(s);
    ea_alsa_close(a);
}

static void file_mono(void) { file_bit_exact(1); }
static void file_stereo(void) { file_bit_exact(2); }

static void stream_mismatch_refused(void)
{
    ea_alsa_config cfg = { "ea_file_stereo", RATE, 2, 0, 0 };
    ea_alsa *a = NULL;
    EA_CHECK_EQ(ea_alsa_open(&cfg, &a), EA_OK);
    ea_stream *s = running(1, 4);
    if (a && s)
        EA_CHECK_EQ(ea_alsa_run(a, s, 1, NULL, NULL), EA_ERR_INVALID_CONFIG);
    ea_close(s);
    ea_alsa_close(a);
}

/* ---- scripted device over the ea_alsa_io seam ------------------------- */

typedef struct {
    int      start_err;
    uint32_t avail_calls, overrun_on_call, fault_on_call; /* 1-based; 0 = never */
    uint32_t delivered, eof_at, read_err_at, chunk;       /* frames; 0 = never / whole */
    uint32_t eintr_at;                                    /* frames; -EINTR once there */
    int      status_fails, forward_err;
    ea_alsa  *stop_on_eintr;                              /* request a stop before the EINTR */
} script;

static int     sc_start(void *d) { return ((script *)d)->start_err; }
static void    sc_drop(void *d) { (void)d; }
static int64_t sc_forward(void *d, uint64_t n)
{
    return ((script *)d)->forward_err ? -EIO : (int64_t)n;
}

static int64_t sc_avail(void *d)
{
    script *x = d;
    x->avail_calls++;
    if (x->fault_on_call && x->avail_calls == x->fault_on_call)
        return -EIO;
    if (x->overrun_on_call && x->avail_calls == x->overrun_on_call)
        return 10 * FRAME + 100; /* buffer + 100 overwritten */
    return 0;
}

static int64_t sc_read(void *d, int16_t *buf, uint32_t n)
{
    script *x = d;
    if (x->eintr_at && x->delivered >= x->eintr_at) {
        x->eintr_at = 0;
        if (x->stop_on_eintr)
            ea_alsa_request_stop(x->stop_on_eintr);
        return -EINTR;
    }
    if (x->read_err_at && x->delivered >= x->read_err_at)
        return -EPIPE;
    if (x->eof_at && x->delivered >= x->eof_at)
        return 0;
    if (x->chunk && n > x->chunk)
        n = x->chunk;
    if (x->eof_at && n > x->eof_at - x->delivered)
        n = x->eof_at - x->delivered;
    if (x->read_err_at && n > x->read_err_at - x->delivered)
        n = x->read_err_at - x->delivered;
    if (x->eintr_at && n > x->eintr_at - x->delivered)
        n = x->eintr_at - x->delivered;
    for (uint32_t i = 0; i < n; i++)
        buf[i] = sample_at(x->delivered + i, 0);
    x->delivered += n;
    return n;
}

static int sc_status(void *d, int64_t *t, uint32_t *avail)
{
    if (((script *)d)->status_fails)
        return -EBADFD;
    *t = 123;
    *avail = 7;
    return 0;
}

static const ea_alsa_io sc_io = { sc_start, sc_avail, sc_forward, sc_read, sc_status, sc_drop };
static const ea_alsa_params sc_params = { RATE, 1, 10, FRAME, 10 * FRAME, 1u << 30 };

typedef struct {
    ea_alsa_counters n;
    anchors an;
    uint32_t frames;
    int exact;
    ea_pop_kind last_kind;
    size_t events;
    ea_event ev[8];
} outcome;

enum { STOP_BEFORE_RUN = 1, STOP_ON_EINTR = 2 };

static outcome run_script_ex(script *x, uint64_t max_blocks, uint32_t stop_at, uint32_t refuse_at,
                             int check_content, int stop_mode)
{
    outcome o = { 0 };
    ea_alsa *a = NULL;
    ea_stream *s = running(1, 64);
    EA_CHECK_EQ(ea_alsa_open_io(&sc_io, x, &sc_params, &a), EA_OK);
    if (!a || !s)
        return o;
    if (stop_mode == STOP_BEFORE_RUN)
        ea_alsa_request_stop(a);
    if (stop_mode == STOP_ON_EINTR)
        x->stop_on_eintr = a;
    o.an.ordered = 1;
    o.an.stop_at_target = a;
    o.an.stop_at = stop_at;
    o.an.refuse_s = s;
    o.an.refuse_at = refuse_at;
    EA_CHECK_EQ(ea_alsa_run(a, s, max_blocks, on_anchor, &o.an), EA_OK);
    ea_alsa_counters_get(a, &o.n);
    o.events = ea_poll_events(s, o.ev, 8); /* before drain: a fault's event is queued first */
    if (check_content) {
        o.frames = drain(s, 1, &o.exact, &o.last_kind);
    } else {
        ea_frame f;
        while (ea_pop(s, &f, &o.last_kind) == EA_OK && o.last_kind == EA_POP_FRAME)
            o.frames++;
    }
    ea_close(s);
    ea_alsa_close(a);
    return o;
}

static outcome run_script(script *x, uint64_t max_blocks, uint32_t stop_at, uint32_t refuse_at,
                          int check_content)
{
    return run_script_ex(x, max_blocks, stop_at, refuse_at, check_content, 0);
}

static void overrun_gap_reaches_stream(void) /* T-02 via the adapter: E-03, D-09 */
{
    script x = { .overrun_on_call = 3 };
    ea_alsa *a = NULL;
    ea_stream *s = running(1, 16);
    EA_CHECK_EQ(ea_alsa_open_io(&sc_io, &x, &sc_params, &a), EA_OK);
    if (!a || !s)
        return;
    anchors an = { .ordered = 1 };
    EA_CHECK_EQ(ea_alsa_run(a, s, 4, on_anchor, &an), EA_OK);
    const uint64_t gap = 100 + FRAME; /* overwritten + one period */
    uint64_t expect[4] = { 0, FRAME, 2 * FRAME + gap, 3 * FRAME + gap };
    for (int i = 0; i < 4; i++) {
        ea_frame f;
        ea_pop_kind kind;
        EA_CHECK_EQ(ea_pop(s, &f, &kind), EA_OK);
        EA_CHECK_EQ(kind, EA_POP_FRAME);
        EA_CHECK_EQ(f.first_sample_index, expect[i]);
        EA_CHECK_EQ(f.discontinuity, i == 2);
    }
    ea_event ev[4];
    EA_CHECK_EQ(ea_poll_events(s, ev, 4), 1);
    EA_CHECK_EQ(ev[0].kind, EA_EVENT_DISCONTINUITY);
    EA_CHECK_EQ(ev[0].at_sample, 2 * FRAME);
    EA_CHECK_EQ(ev[0].detail.discontinuity.gap_samples, gap);
    EA_CHECK_EQ(an.last.end_index, 4 * FRAME + gap); /* anchors stay on the stream clock */
    EA_CHECK_EQ(an.last.hw_position, 4 * FRAME + gap + 7);
    EA_CHECK_EQ(an.last.t_hw_ns, 123);
    ea_alsa_counters n;
    ea_alsa_counters_get(a, &n);
    EA_CHECK_EQ(n.gap_samples, gap);
    EA_CHECK_EQ(n.overrun_recoveries, 1);
    EA_CHECK_EQ(n.end, EA_ALSA_END_BLOCKS);
    ea_close(s);
    ea_alsa_close(a);
}

static void check_fault(const outcome *o, uint32_t frames, int err) /* T-10 via the adapter: E-06 */
{
    EA_CHECK_EQ(o->n.end, EA_ALSA_END_FAULT);
    EA_CHECK_EQ(o->n.fault_errno, err);
    EA_CHECK_EQ(o->n.blocks, frames);
    EA_CHECK_EQ(o->frames, frames);                    /* queued frames still drain */
    EA_CHECK_EQ(o->last_kind, EA_POP_END_OF_STREAM);   /* then end-of-stream */
    EA_CHECK_EQ(o->events, 1);
    EA_CHECK_EQ(o->ev[0].kind, EA_EVENT_ADAPTER_FAULT);
    EA_CHECK_EQ(o->ev[0].detail.adapter_fault.code, err);
}

static void fault_on_start(void)
{
    script x = { .start_err = -ENODEV };
    outcome o = run_script(&x, 0, 0, 0, 0);
    check_fault(&o, 0, ENODEV);
}

static void fault_on_avail(void)
{
    script x = { .fault_on_call = 3 };
    outcome o = run_script(&x, 0, 0, 0, 0);
    check_fault(&o, 2, EIO);
}

static void fault_on_read(void)
{
    script x = { .read_err_at = FRAME + FRAME / 2 };
    outcome o = run_script(&x, 0, 0, 0, 0);
    check_fault(&o, 1, EPIPE);
}

static void eof_counts_partial_block(void)
{
    script x = { .eof_at = 2 * FRAME + 100 };
    outcome o = run_script(&x, 0, 0, 0, 1);
    EA_CHECK_EQ(o.n.end, EA_ALSA_END_EOF);
    EA_CHECK_EQ(o.n.blocks, 2);
    EA_CHECK_EQ(o.n.partial_frames, 100);
    EA_CHECK_EQ(o.n.fault_errno, 0);
    EA_CHECK_EQ(o.frames, 2);
    EA_CHECK(o.exact);
    EA_CHECK_EQ(o.events, 0);
}

static void short_reads_reassemble(void)
{
    script x = { .chunk = 100 };
    outcome o = run_script(&x, 3, 0, 0, 1);
    EA_CHECK_EQ(o.n.end, EA_ALSA_END_BLOCKS);
    EA_CHECK_EQ(o.frames, 3);
    EA_CHECK(o.exact);
    EA_CHECK(o.n.short_reads > 0);
}

static void stop_request_ends_after_current_block(void)
{
    script x = { 0 };
    outcome o = run_script(&x, 0, 2, 0, 1);
    EA_CHECK_EQ(o.n.end, EA_ALSA_END_STOP);
    EA_CHECK_EQ(o.n.blocks, 2); /* the block whose anchor requested the stop is still pushed */
    EA_CHECK_EQ(o.frames, 2);
}

static void refused_push_ends_run(void)
{
    script x = { 0 };
    outcome o = run_script(&x, 0, 0, 2, 0);
    EA_CHECK_EQ(o.n.end, EA_ALSA_END_REFUSED);
    EA_CHECK_EQ(o.n.blocks, 1);
    EA_CHECK_EQ(o.an.count, 2); /* the refused block's anchor was delivered, its frame was not */
    EA_CHECK_EQ(o.frames, 1);
}

static void stop_interrupting_read_discards_partial(void)
{
    script x = { .eintr_at = FRAME + 50 };
    outcome o = run_script_ex(&x, 0, 0, 0, 1, STOP_ON_EINTR);
    EA_CHECK_EQ(o.n.end, EA_ALSA_END_STOP);
    EA_CHECK_EQ(o.n.blocks, 1);
    EA_CHECK_EQ(o.n.partial_frames, 50);
    EA_CHECK_EQ(o.n.fault_errno, 0);
    EA_CHECK_EQ(o.events, 0);
}

static void eintr_without_stop_retries(void)
{
    script x = { .eintr_at = FRAME + 50 };
    outcome o = run_script(&x, 3, 0, 0, 1);
    EA_CHECK_EQ(o.n.end, EA_ALSA_END_BLOCKS);
    EA_CHECK_EQ(o.frames, 3);
    EA_CHECK(o.exact); /* the interrupted block resumes where it stopped */
}

static void stop_before_run(void)
{
    script x = { 0 };
    outcome o = run_script_ex(&x, 0, 0, 0, 0, STOP_BEFORE_RUN);
    EA_CHECK_EQ(o.n.end, EA_ALSA_END_STOP);
    EA_CHECK_EQ(o.n.blocks, 0);
}

static void fault_on_forward(void)
{
    script x = { .overrun_on_call = 2, .forward_err = 1 };
    outcome o = run_script(&x, 0, 0, 0, 0);
    check_fault(&o, 1, EIO);
}

static void null_arguments_refused(void)
{
    EA_CHECK_EQ(ea_alsa_run(NULL, NULL, 1, NULL, NULL), EA_ERR_INVALID_CONFIG);
}

static void status_failure_zeroes_hw_fields(void)
{
    script x = { .status_fails = 1 };
    outcome o = run_script(&x, 2, 0, 0, 0);
    EA_CHECK_EQ(o.n.status_failures, 2);
    EA_CHECK_EQ(o.an.last.end_index, 2 * FRAME);
    EA_CHECK_EQ(o.an.last.t_hw_ns, 0);
    EA_CHECK_EQ(o.an.last.hw_position, 0);
    EA_CHECK(o.an.last.t_acquire_ns > 0);
}

static void open_io_profile_gate(void) /* E-04 */
{
    script x = { 0 };
    ea_alsa *a = (ea_alsa *)1;
    ea_alsa_params p48 = sc_params;
    p48.rate = 48000;
    EA_CHECK_EQ(ea_alsa_open_io(&sc_io, &x, &p48, &a), EA_ERR_INVALID_CONFIG);
    EA_CHECK(a == NULL);
    ea_alsa_params p3 = sc_params;
    p3.channels = 3;
    EA_CHECK_EQ(ea_alsa_open_io(&sc_io, &x, &p3, &a), EA_ERR_INVALID_CONFIG);
}

int main(void)
{
    if (!mkdtemp(g_dir))
        return 2;
    snprintf(g_raw_mono, sizeof g_raw_mono, "%s/mono.raw", g_dir);
    snprintf(g_raw_stereo, sizeof g_raw_stereo, "%s/stereo.raw", g_dir);
    snprintf(g_conf, sizeof g_conf, "%s/asound.conf", g_dir);
    int setup = write_raw(g_raw_mono, 1) || write_raw(g_raw_stereo, 2) || write_config();
    if (setup)
        fprintf(stderr, "SETUP FAILED: cannot write the test PCM files or ALSA config under %s\n", g_dir);
    if (!setup) {
        EA_RUN(overrun_rule);
        EA_RUN(profile_gate);
        EA_RUN(file_mono);
        EA_RUN(file_stereo);
        EA_RUN(stream_mismatch_refused);
        EA_RUN(overrun_gap_reaches_stream);
        EA_RUN(fault_on_start);
        EA_RUN(fault_on_avail);
        EA_RUN(fault_on_read);
        EA_RUN(eof_counts_partial_block);
        EA_RUN(short_reads_reassemble);
        EA_RUN(stop_request_ends_after_current_block);
        EA_RUN(refused_push_ends_run);
        EA_RUN(status_failure_zeroes_hw_fields);
        EA_RUN(open_io_profile_gate);
        EA_RUN(stop_interrupting_read_discards_partial);
        EA_RUN(eintr_without_stop_retries);
        EA_RUN(stop_before_run);
        EA_RUN(fault_on_forward);
        EA_RUN(null_arguments_refused);
    }
    unlink(g_raw_mono);
    unlink(g_raw_stereo);
    unlink(g_conf);
    rmdir(g_dir);
    return setup ? 2 : EA_TEST_SUMMARY();
}
