/*
 * ESP-IDF port (K-02): EA_MEM_HOT allocations go to internal SRAM,
 * EA_MEM_HISTORY may use PSRAM; mutexes are FreeRTOS semaphores.
 * Replaces src/port_posix.c in the component build.
 */
#if EDGE_AUDIO_TARGET_STUBS
#include "stubs/esp_stubs.h"
#else
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

#include "edge_audio/port.h"

void ea_mutex_init(ea_mutex *m)
{
    m->align = (void *)xSemaphoreCreateMutex();
}

void ea_mutex_lock(ea_mutex *m)
{
    xSemaphoreTake((SemaphoreHandle_t)m->align, portMAX_DELAY);
}

void ea_mutex_unlock(ea_mutex *m)
{
    xSemaphoreGive((SemaphoreHandle_t)m->align);
}

void ea_mutex_destroy(ea_mutex *m)
{
    vSemaphoreDelete((SemaphoreHandle_t)m->align);
}

void *ea_port_alloc(size_t size, ea_mem_kind kind)
{
    if (kind == EA_MEM_HISTORY) {
        /* K-02: larger rolling history MAY use PSRAM; fall back to internal
         * SRAM when no PSRAM is fitted. */
        void *p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM);
        if (p != NULL)
            return p;
    }
    return heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void ea_port_free(void *p)
{
    heap_caps_free(p);
}
