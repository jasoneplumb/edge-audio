/* POSIX host port (W2): pthread mutex, plain malloc for both memory kinds. */
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#include "edge_audio/port.h"

void ea_mutex_init(ea_mutex *m)
{
    _Static_assert(sizeof(pthread_mutex_t) <= sizeof((ea_mutex){0}.storage),
                   "ea_mutex storage too small for pthread_mutex_t");
    pthread_mutex_init((pthread_mutex_t *)m->storage, NULL);
}

void ea_mutex_lock(ea_mutex *m)
{
    pthread_mutex_lock((pthread_mutex_t *)m->storage);
}

void ea_mutex_unlock(ea_mutex *m)
{
    pthread_mutex_unlock((pthread_mutex_t *)m->storage);
}

void ea_mutex_destroy(ea_mutex *m)
{
    pthread_mutex_destroy((pthread_mutex_t *)m->storage);
}

void *ea_port_alloc(size_t size, ea_mem_kind kind)
{
    (void)kind; /* host: no SRAM/PSRAM distinction (K-02 is target-only) */
    return calloc(1, size);
}

void ea_port_free(void *p)
{
    free(p);
}
