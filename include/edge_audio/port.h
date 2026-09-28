/*
 * Intent: Platform port (W2). The core calls only these; the host implementation is
 * src/port_posix.c and a target (ESP-IDF) implementation lives with the
 * adapter component, which maps EA_MEM_HOT to internal SRAM and
 * EA_MEM_HISTORY to PSRAM (K-02).
 */
#ifndef EDGE_AUDIO_PORT_H
#define EDGE_AUDIO_PORT_H

#include <stddef.h>

typedef struct {
    void *align;
    unsigned char storage[120];
} ea_mutex;

void ea_mutex_init(ea_mutex *m);
void ea_mutex_lock(ea_mutex *m);
void ea_mutex_unlock(ea_mutex *m);
void ea_mutex_destroy(ea_mutex *m);

typedef enum {
    EA_MEM_HOT = 0,  /* DMA-adjacent / queue memory: internal SRAM (K-02) */
    EA_MEM_HISTORY   /* rolling history: MAY be PSRAM (K-02) */
} ea_mem_kind;

void *ea_port_alloc(size_t size, ea_mem_kind kind);
void  ea_port_free(void *p);

#endif /* EDGE_AUDIO_PORT_H */
