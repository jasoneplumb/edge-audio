#include "support/sim_adapter.h"

#include <stdlib.h>

static bool sim_accepts(const ea_stream_config *cfg)
{
    (void)cfg;
    return true;
}

const ea_adapter_desc ea_sim_adapter = { "sim", sim_accepts, NULL, 0 };

ea_status ea_sim_push_block(ea_stream *s, uint32_t samples_per_channel,
                            int16_t value, uint64_t gap_before)
{
    ea_stream_info info;
    ea_status st = ea_stream_info_get(s, &info);
    if (st != EA_OK)
        return st;
    uint32_t total = samples_per_channel * info.channels;
    int16_t *buf = NULL;
    if (total > 0) {
        buf = malloc(total * sizeof(int16_t));
        for (uint32_t i = 0; i < total; i++)
            buf[i] = value;
    }
    st = ea_capture_push_block(s, buf, samples_per_channel, gap_before);
    free(buf);
    return st;
}

ea_status ea_sim_push_frames(ea_stream *s, uint32_t n_frames, int16_t value)
{
    ea_stream_info info;
    ea_status st = ea_stream_info_get(s, &info);
    if (st != EA_OK)
        return st;
    const uint32_t n = EA_FRAME_SAMPLES(info.sample_rate_hz);
    for (uint32_t f = 0; f < n_frames; f++) {
        st = ea_sim_push_block(s, n, value, 0);
        if (st != EA_OK)
            return st;
    }
    return EA_OK;
}
