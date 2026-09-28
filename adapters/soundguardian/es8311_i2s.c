/*
 * Intent: SoundGuardian ES8311/I2S source adapter (realizes C-05 target side, R-06;
 * E-03/D-09: gap reports come from the I2S driver's receive-overflow
 * callback — a hardware/DMA counter — never from wall clock; E-06: a failed
 * read faults the stream).
 *
 * The capture task reads one K-06 frame (320 samples, 20 ms at 16 kHz) per
 * i2s_channel_read and pushes it with the accumulated overflow-loss count as
 * gap_samples_before. DMA buffers live inside the I2S driver in internal
 * SRAM (K-02); the core's hot allocations go through port_esp.c the same way.
 */
#if EDGE_AUDIO_TARGET_STUBS
#include "stubs/esp_stubs.h"
#else
#include "driver/i2s_std.h"
#include "es8311.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#include <stdatomic.h>

#include "soundguardian.h"

#define SG_RATE_HZ 16000u /* K-01 */
#define SG_FRAME EA_FRAME_SAMPLES(SG_RATE_HZ)
#define SG_ES8311_ADDR 0x18
#define SG_I2C_PORT 0

static const char *TAG = "ea_sg";

typedef struct {
    ea_stream *stream;
    i2s_chan_handle_t rx;
    es8311_handle_t codec;
    TaskHandle_t task;
    /* Hardware-counter gap ledger (C-05). Written from the I2S overflow
     * callback (ISR context) and drained by the capture task, so both sides
     * are atomic: a plain read-then-clear could lose an increment that lands
     * between the two, leaving hardware loss unannounced (I-002, K-03). */
    atomic_uint_fast32_t lost_samples;
    volatile int running;
} sg_ctx;

static sg_ctx g_ctx;

/* I2S receive-queue overflow: DMA data was lost before we could read it.
 * ed->size is bytes; PCM_S16LE mono → 2 bytes per sample (D-08, K-01). */
static int on_recv_overflow(i2s_chan_handle_t handle, i2s_event_data_t *ed,
                            void *user_ctx)
{
    (void)handle;
    sg_ctx *ctx = (sg_ctx *)user_ctx;
    atomic_fetch_add(&ctx->lost_samples,
                     (uint_fast32_t)(ed->size / sizeof(int16_t)));
    return 0;
}

static void capture_task(void *arg)
{
    sg_ctx *ctx = (sg_ctx *)arg;
    static int16_t block[SG_FRAME];
    while (ctx->running) {
        size_t got = 0;
        esp_err_t err = i2s_channel_read(ctx->rx, block, sizeof(block), &got,
                                         1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s read failed");
            ea_capture_fault(ctx->stream, (uint32_t)err); /* E-06 */
            break;
        }
        /* Atomic exchange: no increment can fall between read and clear. */
        uint32_t gap = (uint32_t)atomic_exchange(&ctx->lost_samples, 0);
        ea_capture_push_block(ctx->stream, block,
                              (uint32_t)(got / sizeof(int16_t)),
                              gap); /* E-03, D-09 */
    }
    vTaskDelete(NULL);
}

ea_status ea_sg_start(uint32_t queue_capacity, uint32_t history_capacity,
                      ea_stream **out)
{
    ea_stream_config cfg = { SG_RATE_HZ, 1, queue_capacity,
                             history_capacity };
    ea_status st = ea_open(&cfg, &ea_soundguardian_adapter, out); /* E-04 */
    if (st != EA_OK)
        return st;

    g_ctx.stream = *out;
    atomic_store(&g_ctx.lost_samples, 0);
    g_ctx.running = 1;

    g_ctx.codec = es8311_create(SG_I2C_PORT, SG_ES8311_ADDR);
    es8311_init_default(g_ctx.codec, (int)SG_RATE_HZ);
    es8311_microphone_config(g_ctx.codec, 0);

    /* DMA descriptors sized to the K-06 frame; the driver allocates them in
     * internal SRAM (K-02). */
    i2s_chan_config_t chan_cfg = { 0, 0, 4, (int)SG_FRAME, 1 };
    i2s_new_channel(&chan_cfg, NULL, &g_ctx.rx);
    i2s_std_config_t std_cfg = { 0, 0, 0 };
    i2s_channel_init_std_mode(g_ctx.rx, &std_cfg);
    i2s_event_callbacks_t cbs = { NULL, on_recv_overflow, NULL, NULL };
    i2s_channel_register_event_callback(g_ctx.rx, &cbs, &g_ctx);
    i2s_channel_enable(g_ctx.rx);

    st = ea_start(*out);
    if (st != EA_OK)
        return st;
    xTaskCreate(capture_task, "ea_sg_capture", 4096, &g_ctx, 10,
                &g_ctx.task);
    ESP_LOGI(TAG, "capture running");
    return EA_OK;
}

ea_status ea_sg_stop(ea_stream *s)
{
    g_ctx.running = 0;
    i2s_channel_disable(g_ctx.rx);
    i2s_del_channel(g_ctx.rx);
    return ea_stop(s); /* K-04: flushes any open overflow episode */
}
