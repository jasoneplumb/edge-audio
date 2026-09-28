/*
 * Intent: Host stand-in declarations for the ESP-IDF APIs es8311_i2s.c and
 * port_esp.c use, enabled by -DEDGE_AUDIO_TARGET_STUBS=1 (plan §6 W5
 * stand-in). Syntax/boundary checking only: nothing here executes, and a
 * pass proves nothing about placement (K-02) or timing (K-03).
 */
#ifndef EA_ESP_STUBS_H
#define EA_ESP_STUBS_H

#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))

/* --- i2s_std driver (driver/i2s_std.h) --- */
typedef struct i2s_channel_obj *i2s_chan_handle_t;
typedef struct {
    int id;
    int role;
    int dma_desc_num;
    int dma_frame_num;
    int auto_clear;
} i2s_chan_config_t;
typedef struct {
    int clk_cfg_placeholder;
    int slot_cfg_placeholder;
    int gpio_cfg_placeholder;
} i2s_std_config_t;
typedef struct {
    size_t size;
} i2s_event_data_t;
typedef int (*i2s_isr_callback_t)(i2s_chan_handle_t handle,
                                  i2s_event_data_t *event, void *user_ctx);
typedef struct {
    i2s_isr_callback_t on_recv;
    i2s_isr_callback_t on_recv_q_ovf;
    i2s_isr_callback_t on_sent;
    i2s_isr_callback_t on_send_q_ovf;
} i2s_event_callbacks_t;

static inline esp_err_t i2s_new_channel(const i2s_chan_config_t *cfg,
                                        i2s_chan_handle_t *tx,
                                        i2s_chan_handle_t *rx)
{
    (void)cfg; (void)tx; (void)rx; return ESP_OK;
}
static inline esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h,
                                                  const i2s_std_config_t *c)
{
    (void)h; (void)c; return ESP_OK;
}
static inline esp_err_t
i2s_channel_register_event_callback(i2s_chan_handle_t h,
                                    const i2s_event_callbacks_t *cbs,
                                    void *user_ctx)
{
    (void)h; (void)cbs; (void)user_ctx; return ESP_OK;
}
static inline esp_err_t i2s_channel_enable(i2s_chan_handle_t h)
{
    (void)h; return ESP_OK;
}
static inline esp_err_t i2s_channel_disable(i2s_chan_handle_t h)
{
    (void)h; return ESP_OK;
}
static inline esp_err_t i2s_channel_read(i2s_chan_handle_t h, void *dest,
                                         size_t size, size_t *bytes_read,
                                         uint32_t timeout_ms)
{
    (void)h; (void)dest; (void)timeout_ms;
    if (bytes_read) *bytes_read = size;
    return ESP_OK;
}
static inline esp_err_t i2s_del_channel(i2s_chan_handle_t h)
{
    (void)h; return ESP_OK;
}

/* --- es8311 codec component --- */
typedef struct es8311_obj *es8311_handle_t;
static inline es8311_handle_t es8311_create(int port, int addr)
{
    (void)port; (void)addr; return (es8311_handle_t)1;
}
static inline esp_err_t es8311_init_default(es8311_handle_t h, int rate)
{
    (void)h; (void)rate; return ESP_OK;
}
static inline esp_err_t es8311_microphone_config(es8311_handle_t h, int dmic)
{
    (void)h; (void)dmic; return ESP_OK;
}

/* --- FreeRTOS task/sync subset --- */
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
static inline BaseType_t xTaskCreate(void (*fn)(void *), const char *name,
                                     uint32_t stack, void *arg, int prio,
                                     TaskHandle_t *out)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)out;
    return pdPASS;
}
static inline void vTaskDelete(TaskHandle_t t) { (void)t; }
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)1;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, uint32_t to)
{
    (void)s; (void)to; return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    (void)s; return pdTRUE;
}
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }

/* --- heap_caps --- */
#define MALLOC_CAP_INTERNAL 0x800
#define MALLOC_CAP_SPIRAM 0x400
#define MALLOC_CAP_8BIT 0x4
static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    extern void *calloc(size_t, size_t);
    return calloc(n, size);
}
static inline void heap_caps_free(void *p)
{
    extern void free(void *);
    free(p);
}

#endif /* EA_ESP_STUBS_H */
