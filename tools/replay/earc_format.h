/* EARC recorded-frame-stream layout, shared by the recorder (tests/support)
 * and the replay tool (§3.3 consumer artifact; C-01 fields; R-07: wall clock
 * is stored but never drives positioning). */
#ifndef EA_EARC_FORMAT_H
#define EA_EARC_FORMAT_H

#include <stdint.h>

#define EA_EARC_MAGIC 0x43524145u /* "EARC" little-endian */
#define EA_EARC_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t stream_id;
    uint32_t sample_rate_hz;
    uint8_t channels;
    uint8_t pad[3];
    uint32_t frame_samples; /* per channel (K-06) */
} ea_earc_header;

typedef struct {
    uint64_t first_sample_index; /* the only positioning key (R-07) */
    uint8_t flags;               /* bit0 discontinuity, bit1 clipped, bit2 silent */
    uint8_t pad[7];
    uint64_t wall_clock_us;      /* metadata only */
    /* frame_samples * channels int16 samples follow */
} ea_earc_record;

#endif /* EA_EARC_FORMAT_H */
