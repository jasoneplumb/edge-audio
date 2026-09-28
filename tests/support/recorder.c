/* Intent: EARC recorder implementation (W4 apparatus): writes popped frames
 * to the §3.3 consumer artifact that replay (R-08) reads; wall clock is
 * stored as metadata only (R-07). */
#include "support/recorder.h"

#include <stdio.h>
#include <stdlib.h>

#include "../../tools/replay/earc_format.h"

struct ea_recorder {
    FILE *f;
    uint32_t frame_total;
};

ea_recorder *ea_recorder_open(const char *path, const ea_stream_info *info,
                              uint32_t frame_samples)
{
    ea_recorder *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->f = fopen(path, "wb");
    if (!r->f) {
        free(r);
        return NULL;
    }
    r->frame_total = frame_samples * info->channels;
    ea_earc_header h = { EA_EARC_MAGIC, EA_EARC_VERSION, info->stream_id,
                         info->sample_rate_hz, info->channels, { 0, 0, 0 },
                         frame_samples };
    fwrite(&h, sizeof(h), 1, r->f);
    return r;
}

int ea_recorder_write(ea_recorder *r, const ea_frame *f, uint64_t wall_us)
{
    ea_earc_record rec = { f->first_sample_index,
                           (uint8_t)((f->discontinuity ? 1 : 0) |
                                     (f->clipped ? 2 : 0) |
                                     (f->silent ? 4 : 0)),
                           { 0, 0, 0, 0, 0, 0, 0 },
                           wall_us }; /* metadata only (R-07) */
    if (fwrite(&rec, sizeof(rec), 1, r->f) != 1)
        return -1;
    if (fwrite(f->samples, sizeof(int16_t), r->frame_total, r->f) !=
        r->frame_total)
        return -1;
    return 0;
}

void ea_recorder_close(ea_recorder *r)
{
    if (!r)
        return;
    fclose(r->f);
    free(r);
}
