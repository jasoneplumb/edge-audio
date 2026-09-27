/*
 * SoundGuardian soak firmware (T-17 *(recorded)*; K-01, K-02, K-03):
 * captures continuously, drains frames, and logs starvation/loss counters
 * every minute. A 4-hour run's log plus the build's map file are the
 * recorded evidence for SPEC_BUILD_REPORT.md §soak.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "soundguardian.h"

static const char *TAG = "ea_soak";

void app_main(void)
{
    ea_stream *s = NULL;
    /* 32-frame queue in SRAM; 10 s of rolling history, PSRAM-eligible
     * (K-02): 160000 samples. */
    if (ea_sg_start(32, 160000, &s) != EA_OK) {
        ESP_LOGE(TAG, "start failed");
        return;
    }

    uint64_t frames = 0, discontinuities = 0, overflow_events = 0,
             frames_lost = 0, gap_events = 0;
    ea_frame f;
    ea_pop_kind kind;
    ea_event ev[16];
    TickType_t last_log = xTaskGetTickCount();

    for (;;) {
        while (ea_pop(s, &f, &kind) == EA_OK && kind == EA_POP_FRAME) {
            frames++;
            if (f.discontinuity)
                discontinuities++;
        }
        size_t n = ea_poll_events(s, ev, 16);
        for (size_t i = 0; i < n; i++) {
            if (ev[i].kind == EA_EVENT_QUEUE_OVERFLOW) {
                overflow_events++;
                frames_lost += ev[i].detail.queue_overflow.frames_dropped;
            } else if (ev[i].kind == EA_EVENT_DISCONTINUITY) {
                gap_events++;
            }
        }
        if (xTaskGetTickCount() - last_log >= pdMS_TO_TICKS(60 * 1000)) {
            last_log = xTaskGetTickCount();
            /* K-03 evidence line: every gap must have an event
             * (discontinuous frames <= gap_events + overflow episodes). */
            ESP_LOGI(TAG,
                     "frames=%llu disc=%llu gaps=%llu ovf=%llu lost=%llu",
                     (unsigned long long)frames,
                     (unsigned long long)discontinuities,
                     (unsigned long long)gap_events,
                     (unsigned long long)overflow_events,
                     (unsigned long long)frames_lost);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
