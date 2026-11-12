/*
 * Intent: SoundGuardian / M5StickS3 ES8311 + I2S source adapter (C-05, R-06).
 * Gap reports come from the I2S receive-overflow callback, never wall clock;
 * failed target reads fault the stream unless shutdown is already in progress.
 *
 * The hardware configuration below is the C equivalent of the M5StickS3 path
 * bench-validated in SoundGuardian: internal I2C SDA=47/SCL=48, ES8311=0x18,
 * I2S1 MCLK=18/BCLK=17/WS=15/DIN=16, 16 kHz mono, LEFT slot.
 */
#if EDGE_AUDIO_TARGET_STUBS
#include "stubs/esp_stubs.h"
#else
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "soundguardian.h"

#define SG_RATE_HZ 16000u /* K-01 */
#define SG_FRAME EA_FRAME_SAMPLES(SG_RATE_HZ)

#define SG_I2C_PORT 1
#define SG_I2C_SDA GPIO_NUM_47
#define SG_I2C_SCL GPIO_NUM_48
#define SG_ES8311_ADDR 0x18

#define SG_I2S_PORT I2S_NUM_1
#define SG_MIC_MCLK GPIO_NUM_18
#define SG_MIC_BCLK GPIO_NUM_17
#define SG_MIC_WS GPIO_NUM_15
#define SG_MIC_DIN GPIO_NUM_16

static const char *TAG = "ea_sg";

/* Exact ES8311 ADC enable sequence used by the validated M5StickS3 bring-up.
 * The register values are those of M5Unified's
 * M5Unified::_microphone_enabled_cb_sticks3 (Copyright (c) 2021 M5Stack,
 * MIT License; notice reproduced in THIRD_PARTY_NOTICES.md). */
static const uint8_t SG_ES8311_ENABLE_REGS[][2] = {
    {0x00, 0x80}, {0x01, 0xBA}, {0x02, 0x18}, {0x0D, 0x01},
    {0x0E, 0x02}, {0x14, 0x10}, {0x17, 0xFF}, {0x1C, 0x6A},
};

typedef struct {
    ea_stream *stream;
    i2s_chan_handle_t rx;
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t codec;
    TaskHandle_t task;
    SemaphoreHandle_t task_done;
    atomic_uint_fast32_t lost_samples;
    bool owns_i2c_bus;
    bool i2s_enabled;
    volatile bool running;
} sg_ctx;

static sg_ctx g_ctx;

static void hardware_release(sg_ctx *ctx)
{
    if (ctx->i2s_enabled && ctx->rx != NULL) {
        (void)i2s_channel_disable(ctx->rx);
        ctx->i2s_enabled = false;
    }
    if (ctx->rx != NULL) {
        (void)i2s_del_channel(ctx->rx);
        ctx->rx = NULL;
    }
    if (ctx->codec != NULL) {
        (void)i2c_master_bus_rm_device(ctx->codec);
        ctx->codec = NULL;
    }
    if (ctx->owns_i2c_bus && ctx->i2c_bus != NULL) {
        (void)i2c_del_master_bus(ctx->i2c_bus);
    }
    ctx->i2c_bus = NULL;
    ctx->owns_i2c_bus = false;
}

static esp_err_t codec_configure(sg_ctx *ctx)
{
    esp_err_t err = i2c_master_get_bus_handle(SG_I2C_PORT, &ctx->i2c_bus);
    if (err != ESP_OK) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = SG_I2C_PORT,
            .sda_io_num = SG_I2C_SDA,
            .scl_io_num = SG_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
                .allow_pd = 0,
            },
        };
        err = i2c_new_master_bus(&bus_cfg, &ctx->i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "internal I2C bus init failed: %d", (int)err);
            return err;
        }
        ctx->owns_i2c_bus = true;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SG_ES8311_ADDR,
        .scl_speed_hz = 100000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    err = i2c_master_bus_add_device(ctx->i2c_bus, &dev_cfg, &ctx->codec);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 I2C device add failed: %d", (int)err);
        return err;
    }

    for (size_t i = 0;
         i < sizeof(SG_ES8311_ENABLE_REGS) / sizeof(SG_ES8311_ENABLE_REGS[0]);
         ++i) {
        err = i2c_master_transmit(ctx->codec, SG_ES8311_ENABLE_REGS[i], 2, 100);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ES8311 register 0x%02x write failed: %d",
                     SG_ES8311_ENABLE_REGS[i][0], (int)err);
            return err;
        }
    }
    return ESP_OK;
}

/* I2S receive-queue overflow: event size is overwritten DMA bytes.
 * PCM_S16LE mono => two bytes per sample (D-08, K-01). */
static bool on_recv_overflow(i2s_chan_handle_t handle, i2s_event_data_t *event,
                             void *user_ctx)
{
    (void)handle;
    sg_ctx *ctx = (sg_ctx *)user_ctx;
    atomic_fetch_add(&ctx->lost_samples,
                     (uint_fast32_t)(event->size / sizeof(int16_t)));
    return false;
}

static void capture_task(void *arg)
{
    sg_ctx *ctx = (sg_ctx *)arg;
    static int16_t block[SG_FRAME];

    while (ctx->running) {
        size_t got = 0;
        esp_err_t err =
            i2s_channel_read(ctx->rx, block, sizeof(block), &got, 1000);
        if (err != ESP_OK) {
            if (ctx->running) {
                ESP_LOGE(TAG, "i2s read failed: %d", (int)err);
                (void)ea_capture_fault(ctx->stream, (uint32_t)err); /* E-06 */
            }
            break;
        }

        uint32_t gap =
            (uint32_t)atomic_exchange(&ctx->lost_samples, 0);
        (void)ea_capture_push_block(ctx->stream, block,
                                    (uint32_t)(got / sizeof(int16_t)),
                                    gap); /* E-03, D-09 */
    }

    ctx->task = NULL;
    if (ctx->task_done != NULL) {
        (void)xSemaphoreGive(ctx->task_done);
    }
    vTaskDelete(NULL);
}

ea_status ea_sg_start(uint32_t queue_capacity, uint32_t history_capacity,
                      ea_stream **out)
{
    if (out == NULL) {
        return EA_ERR_INVALID_CONFIG;
    }
    *out = NULL;

    ea_stream_config cfg = {
        .sample_rate_hz = SG_RATE_HZ,
        .channels = 1,
        .queue_capacity = queue_capacity,
        .history_capacity = history_capacity,
    };

    ea_stream *stream = NULL;
    ea_status st = ea_open(&cfg, &ea_soundguardian_adapter, &stream);
    if (st != EA_OK) {
        return st;
    }

    g_ctx.stream = stream;
    g_ctx.rx = NULL;
    g_ctx.i2c_bus = NULL;
    g_ctx.codec = NULL;
    g_ctx.task = NULL;
    g_ctx.task_done = NULL;
    g_ctx.owns_i2c_bus = false;
    g_ctx.i2s_enabled = false;
    g_ctx.running = false;
    atomic_store(&g_ctx.lost_samples, 0);

    if (codec_configure(&g_ctx) != ESP_OK) {
        hardware_release(&g_ctx);
        ea_close(stream);
        return EA_ERR_INVALID_STATE;
    }

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(SG_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = SG_FRAME;

    if (i2s_new_channel(&chan_cfg, NULL, &g_ctx.rx) != ESP_OK) {
        hardware_release(&g_ctx);
        ea_close(stream);
        return EA_ERR_INVALID_STATE;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SG_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = SG_MIC_MCLK,
            .bclk = SG_MIC_BCLK,
            .ws = SG_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = SG_MIC_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* ESP32-S3's default Philips macro selects BOTH slots even in mono mode.
     * The onboard M5StickS3 microphone is on LEFT; leaving BOTH enabled was
     * measured as an apparent 32 ksample/s interleaved stream. */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    if (i2s_channel_init_std_mode(g_ctx.rx, &std_cfg) != ESP_OK) {
        hardware_release(&g_ctx);
        ea_close(stream);
        return EA_ERR_INVALID_STATE;
    }

    i2s_event_callbacks_t callbacks = {
        .on_recv = NULL,
        .on_recv_q_ovf = on_recv_overflow,
        .on_sent = NULL,
        .on_send_q_ovf = NULL,
    };
    if (i2s_channel_register_event_callback(g_ctx.rx, &callbacks, &g_ctx) !=
        ESP_OK) {
        hardware_release(&g_ctx);
        ea_close(stream);
        return EA_ERR_INVALID_STATE;
    }

    g_ctx.task_done = xSemaphoreCreateBinary();
    if (g_ctx.task_done == NULL) {
        hardware_release(&g_ctx);
        ea_close(stream);
        return EA_ERR_INVALID_STATE;
    }

    st = ea_start(stream);
    if (st != EA_OK) {
        vSemaphoreDelete(g_ctx.task_done);
        g_ctx.task_done = NULL;
        hardware_release(&g_ctx);
        ea_close(stream);
        return st;
    }

    if (i2s_channel_enable(g_ctx.rx) != ESP_OK) {
        (void)ea_stop(stream);
        vSemaphoreDelete(g_ctx.task_done);
        g_ctx.task_done = NULL;
        hardware_release(&g_ctx);
        ea_close(stream);
        return EA_ERR_INVALID_STATE;
    }
    g_ctx.i2s_enabled = true;
    g_ctx.running = true;

    if (xTaskCreate(capture_task, "ea_sg_capture", 4096, &g_ctx, 10,
                    &g_ctx.task) != pdPASS) {
        g_ctx.running = false;
        (void)i2s_channel_disable(g_ctx.rx);
        g_ctx.i2s_enabled = false;
        (void)ea_stop(stream);
        vSemaphoreDelete(g_ctx.task_done);
        g_ctx.task_done = NULL;
        hardware_release(&g_ctx);
        ea_close(stream);
        return EA_ERR_INVALID_STATE;
    }

    *out = stream;
    ESP_LOGI(TAG, "capture running");
    return EA_OK;
}

ea_status ea_sg_stop(ea_stream *s)
{
    if (s == NULL || s != g_ctx.stream) {
        return EA_ERR_INVALID_STATE;
    }

    g_ctx.running = false;
    if (g_ctx.i2s_enabled && g_ctx.rx != NULL) {
        (void)i2s_channel_disable(g_ctx.rx);
        g_ctx.i2s_enabled = false;
    }

    if (g_ctx.task != NULL && g_ctx.task_done != NULL) {
        (void)xSemaphoreTake(g_ctx.task_done, pdMS_TO_TICKS(1100));
    }

    ea_status st = ea_stop(s); /* K-04: flushes any open overflow episode */

    if (g_ctx.task_done != NULL) {
        vSemaphoreDelete(g_ctx.task_done);
        g_ctx.task_done = NULL;
    }
    hardware_release(&g_ctx);
    g_ctx.stream = NULL;
    return st;
}
