/*
 * Intent: Replay of recorded frame streams (realizes R-08, I-005; R-07: positioning
 * uses first_sample_index only — the recorded wall clock is never read for
 * positioning). A window overlapping an unrecorded index is SPANS_GAP; a
 * window past the newest recorded index is OUT_OF_RANGE (C-03 semantics,
 * D-14 — never zero-filled or truncated).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "earc_format.h"
#include "replay_lib.h"

ea_replay_result ea_replay_window(const char *path, uint64_t first_index,
                                  uint32_t sample_count, int16_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return EA_REPLAY_BAD_FILE;

    ea_earc_header h;
    if (fread(&h, sizeof(h), 1, f) != 1 || h.magic != EA_EARC_MAGIC ||
        h.version != EA_EARC_VERSION || h.frame_samples == 0 ||
        h.channels == 0) {
        fclose(f);
        return EA_REPLAY_BAD_FILE;
    }

    const uint32_t frame_total = h.frame_samples * h.channels;
    int16_t *frame = malloc((size_t)frame_total * sizeof(int16_t));
    if (!frame) {
        fclose(f);
        return EA_REPLAY_BAD_FILE;
    }

    const uint64_t want_end = first_index + sample_count;
    uint64_t covered = 0;   /* samples of the window filled so far */
    uint64_t newest_end = 0; /* end of the newest recorded frame */

    ea_earc_record rec;
    while (fread(&rec, sizeof(rec), 1, f) == 1) {
        if (fread(frame, sizeof(int16_t), frame_total, f) != frame_total) {
            free(frame);
            fclose(f);
            return EA_REPLAY_BAD_FILE;
        }
        uint64_t fstart = rec.first_sample_index; /* R-07: the only key */
        uint64_t fend = fstart + h.frame_samples;
        if (fend > newest_end)
            newest_end = fend;

        /* Copy the overlap of [fstart, fend) with the window. */
        if (fstart < want_end && fend > first_index) {
            uint64_t lo = (fstart > first_index) ? fstart : first_index;
            uint64_t hi = (fend < want_end) ? fend : want_end;
            memcpy(out + (size_t)(lo - first_index) * h.channels,
                   frame + (size_t)(lo - fstart) * h.channels,
                   (size_t)(hi - lo) * h.channels * sizeof(int16_t));
            covered += hi - lo;
        }
    }
    free(frame);
    fclose(f);

    if (want_end > newest_end)
        return EA_REPLAY_OUT_OF_RANGE;
    if (covered != sample_count)
        return EA_REPLAY_SPANS_GAP; /* D-14: never zero-filled */
    return EA_REPLAY_OK; /* bit-exact (I-005) */
}
