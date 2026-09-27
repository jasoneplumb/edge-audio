/* Host replay of recorded frame streams (R-08, I-005): reproduce any sample
 * window bit-exactly, positioned only by first_sample_index (R-07); a window
 * overlapping an unrecorded index is SPANS_GAP, never zero-filled (D-14). */
#ifndef EA_REPLAY_LIB_H
#define EA_REPLAY_LIB_H

#include <stdint.h>

typedef enum {
    EA_REPLAY_OK = 0,
    EA_REPLAY_SPANS_GAP,
    EA_REPLAY_OUT_OF_RANGE,
    EA_REPLAY_BAD_FILE
} ea_replay_result;

/* out must hold sample_count * channels int16 values. */
ea_replay_result ea_replay_window(const char *path, uint64_t first_index,
                                  uint32_t sample_count, int16_t *out);

#endif /* EA_REPLAY_LIB_H */
