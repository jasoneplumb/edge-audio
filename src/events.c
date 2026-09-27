/*
 * Bounded event ring (realizes C-04 delivery, E-08; guards R-10, I-003).
 * Overflow drops the oldest event and the drop count is reported on the next
 * delivered event's events_dropped field. Emission never blocks capture.
 */
#include <string.h>

#include "internal.h"

void ea_events_push(ea_events *ev, const ea_event *e)
{
    if (ev->count == EA_EVENT_RING_CAP) {
        ev->head = (ev->head + 1) % EA_EVENT_RING_CAP; /* drop oldest (E-08) */
        ev->count--;
        ev->dropped_pending++;
    }
    uint32_t slot = (ev->head + ev->count) % EA_EVENT_RING_CAP;
    ev->buf[slot] = *e;
    ev->buf[slot].events_dropped = 0;
    ev->count++;
}

size_t ea_events_poll(ea_events *ev, ea_event *out, size_t max_events)
{
    size_t n = 0;
    while (n < max_events && ev->count > 0) {
        out[n] = ev->buf[ev->head];
        if (n == 0 && ev->dropped_pending > 0) {
            /* E-08: next delivered event reports the losses before it. */
            out[0].events_dropped = ev->dropped_pending;
            ev->dropped_pending = 0;
        }
        ev->head = (ev->head + 1) % EA_EVENT_RING_CAP;
        ev->count--;
        n++;
    }
    return n;
}
