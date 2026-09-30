/*
 * Intent: Host stand-in declarations for the ESP-IDF APIs es8311_i2s.c and
 * port_esp.c use, enabled by -DEDGE_AUDIO_TARGET_STUBS=1.
 * Syntax/boundary checking only: nothing here executes, and a pass proves
 * nothing about hardware placement, timing, pins, or peripheral behavior.
 */
#ifndef EA_ESP_STUBS_H
#define EA_ESP_STUBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE (-1)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))

/* --- GPIO / I2C master subset --- */
typedef int gpio_num_t;
#define GPIO_NUM_15 15
#define GPIO_NUM_16 16
#define GPIO_NUM_17 17
#define GPIO_NUM_18 18
#define GPIO_NUM_47 47
#define GPIO_NUM_48 48

#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 7

typedef struct i2c_master_bus_obj *i2c_master_bus_handle_t;
typedef struct i2c_master_dev_obj *i2c_master_dev_handle_t;

typedef struct {
    int i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    int clk_source;
    uint8_t glitch_ignore_cnt;
    int intr_priority;
    size_t trans_queue_depth;
    struct {
        uint32_t enable_internal_pullup : 1;
        uint32_t allow_pd : 1;
    } flags;
} i2c_master_bus_config_t;

typedef struct {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint32_t scl_wait_us;
    struct {
        uint32_t disable_ack_check : 1;
    } flags;
} i2c_device_config_t;

static inline esp_err_t
i2c_master_get_bus_handle(int port, i2c_master_bus_handle_t *out)
{
    (void)port;
    if (out) *out = NULL;
    return ESP_ERR_INVALID_STATE;
}
static inline esp_err_t
i2c_new_master_bus(const i2c_master_bus_config_t *cfg,
                   i2c_master_bus_handle_t *out)
{
    (void)cfg;
    if (out) *out = (i2c_master_bus_handle_t)1;
    return ESP_OK;
}
static inline esp_err_t
i2c_del_master_bus(i2c_master_bus_handle_t bus)
{
    (void)bus;
    return ESP_OK;
}
static inline esp_err_t
i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                          const i2c_device_config_t *cfg,
                          i2c_master_dev_handle_t *out)
{
    (void)bus; (void)cfg;
    if (out) *out = (i2c_master_dev_handle_t)1;
    return ESP_OK;
}
static inline esp_err_t
i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    (void)dev;
    return ESP_OK;
}
static inline esp_err_t
i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data,
                    size_t size, int timeout_ms)
{
    (void)dev; (void)data; (void)size; (void)timeout_ms;
    return ESP_OK;
}

/* --- i2s_std driver subset --- */
#define I2S_NUM_1 1
#define I2S_ROLE_MASTER 1
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_MONO 1
#define I2S_STD_SLOT_LEFT 1
#define I2S_GPIO_UNUSED (-1)

typedef struct i2s_channel_obj *i2s_chan_handle_t;

typedef struct {
    int id;
    int role;
    uint32_t dma_desc_num;
    uint32_t dma_frame_num;
    bool auto_clear_after_cb;
    bool auto_clear_before_cb;
    bool allow_pd;
    int intr_priority;
} i2s_chan_config_t;

#define I2S_CHANNEL_DEFAULT_CONFIG(num, role_value) { \
    .id = (num), \
    .role = (role_value), \
    .dma_desc_num = 6, \
    .dma_frame_num = 240, \
    .auto_clear_after_cb = false, \
    .auto_clear_before_cb = false, \
    .allow_pd = false, \
    .intr_priority = 0, \
}

typedef struct {
    uint32_t sample_rate_hz;
} i2s_std_clk_config_t;

typedef struct {
    int data_bit_width;
    int slot_mode;
    int slot_mask;
} i2s_std_slot_config_t;

typedef struct {
    gpio_num_t mclk;
    gpio_num_t bclk;
    gpio_num_t ws;
    gpio_num_t dout;
    gpio_num_t din;
    struct {
        bool mclk_inv;
        bool bclk_inv;
        bool ws_inv;
    } invert_flags;
} i2s_std_gpio_config_t;

typedef struct {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;

#define I2S_STD_CLK_DEFAULT_CONFIG(rate) { .sample_rate_hz = (rate) }
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, mode) { \
    .data_bit_width = (width), .slot_mode = (mode), .slot_mask = 3 \
}

typedef struct {
    size_t size;
} i2s_event_data_t;

typedef bool (*i2s_isr_callback_t)(i2s_chan_handle_t handle,
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
    (void)cfg; (void)tx;
    if (rx) *rx = (i2s_chan_handle_t)1;
    return ESP_OK;
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

/* --- FreeRTOS task/sync subset --- */
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)

static inline BaseType_t xTaskCreate(void (*fn)(void *), const char *name,
                                     uint32_t stack, void *arg, int prio,
                                     TaskHandle_t *out)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio;
    if (out) *out = (TaskHandle_t)1;
    return pdPASS;
}
static inline void vTaskDelete(TaskHandle_t t) { (void)t; }

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return (SemaphoreHandle_t)1;
}
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void)
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
