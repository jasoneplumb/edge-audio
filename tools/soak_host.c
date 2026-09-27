/*
 * Host soak rehearsal (K-03 logic side; the hardware run is T-17 *(recorded)*).
 * Pushes one K-06 frame every 20 ms at real-time pace from a producer thread
 * while a consumer drains, for --seconds N (default 14400 = 4 h). Exit 0 iff
 * zero capture starvation (every push completed, I-003) and zero unannounced
 * discontinuities (every flagged frame has event accounting, I-002, K-03).
 * Covers nothing about SRAM/PSRAM placement (K-02) or device timing — those
 * stay verification pending until the M5StickS3 run.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "edge_audio/edge_audio.h"

#define FRAME 320

static bool accept_all(const ea_stream_config *cfg)
{
    (void)cfg;
    return true;
}
static const ea_adapter_desc host_adapter = { "soak-host", accept_all, NULL,
                                              0 };

static ea_stream *g_s;
static volatile int g_done;
static unsigned long long g_pushed, g_push_failures;

static void *producer(void *arg)
{
    long seconds = *(long *)arg;
    int16_t buf[FRAME];
    for (int i = 0; i < FRAME; i++)
        buf[i] = (int16_t)(i * 7);
    struct timespec period = { 0, 20 * 1000 * 1000 }; /* 20 ms (K-06) */
    long frames = seconds * 50;
    for (long i = 0; i < frames; i++) {
        if (ea_capture_push_block(g_s, buf, FRAME, 0) == EA_OK)
            g_pushed++;
        else
            g_push_failures++;
        nanosleep(&period, NULL);
    }
    g_done = 1;
    return NULL;
}

int main(int argc, char **argv)
{
    long seconds = 14400; /* K-03: 4 hours (D-04) */
    if (argc == 3 && strcmp(argv[1], "--seconds") == 0)
        seconds = atol(argv[2]);

    ea_stream_config cfg = { 16000, 1, 32, 160000 };
    if (ea_open(&cfg, &host_adapter, &g_s) != EA_OK || ea_start(g_s) != EA_OK)
        return 2;

    pthread_t t;
    pthread_create(&t, NULL, producer, &seconds);

    unsigned long long delivered = 0, disc_frames = 0, gap_events = 0,
                       ovf_events = 0, frames_lost = 0;
    ea_frame f;
    ea_pop_kind kind;
    ea_event ev[16];
    time_t last_log = time(NULL);
    while (!g_done) {
        while (ea_pop(g_s, &f, &kind) == EA_OK && kind == EA_POP_FRAME) {
            delivered++;
            if (f.discontinuity)
                disc_frames++;
        }
        size_t n = ea_poll_events(g_s, ev, 16);
        for (size_t i = 0; i < n; i++) {
            if (ev[i].kind == EA_EVENT_DISCONTINUITY)
                gap_events++;
            else if (ev[i].kind == EA_EVENT_QUEUE_OVERFLOW) {
                ovf_events++;
                frames_lost += ev[i].detail.queue_overflow.frames_dropped;
            }
        }
        if (time(NULL) - last_log >= 60) {
            last_log = time(NULL);
            printf("soak: pushed=%llu delivered=%llu disc=%llu gaps=%llu "
                   "ovf=%llu lost=%llu failures=%llu\n",
                   g_pushed, delivered, disc_frames, gap_events, ovf_events,
                   frames_lost, g_push_failures);
            fflush(stdout);
        }
        struct timespec nap = { 0, 5 * 1000 * 1000 };
        nanosleep(&nap, NULL);
    }
    pthread_join(t, NULL);
    ea_stop(g_s);
    while (ea_pop(g_s, &f, &kind) == EA_OK && kind == EA_POP_FRAME) {
        delivered++;
        if (f.discontinuity)
            disc_frames++;
    }
    size_t n = ea_poll_events(g_s, ev, 16);
    for (size_t i = 0; i < n; i++)
        if (ev[i].kind == EA_EVENT_QUEUE_OVERFLOW)
            frames_lost += ev[i].detail.queue_overflow.frames_dropped;
    ea_close(g_s);

    int starved = (g_push_failures != 0);
    int unannounced = (disc_frames > gap_events + ovf_events);
    printf("soak result: seconds=%ld pushed=%llu delivered=%llu lost=%llu "
           "disc=%llu gaps=%llu ovf=%llu failures=%llu -> %s\n",
           seconds, g_pushed, delivered, frames_lost, disc_frames, gap_events,
           ovf_events, g_push_failures,
           (starved || unannounced) ? "FAIL" : "PASS");
    return (starved || unannounced) ? 1 : 0;
}
