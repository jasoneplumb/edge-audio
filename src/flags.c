/*
 * Intent: §7.1 flag oracles (realizes R-03, K-05; thresholds per D-05/D-06).
 * Population is the whole interleaved frame; symbols follow the spec:
 *   L_rms  = sqrt( (1 / (N*C)) * sum x_i^2 ),  x_i = s_i / 32768
 *   L_dBFS = 20 * log10(L_rms)
 * silent  iff L_dBFS < -60 (L_rms = 0 gives -inf, hence silent).
 * clipped iff any raw sample is -32768 or 32767.
 */
#include <math.h>

#include "edge_audio/edge_audio.h"

bool ea_flags_clipped(const int16_t *interleaved, uint32_t samples_per_channel,
                      uint8_t channels)
{
    const uint32_t total = samples_per_channel * channels;
    for (uint32_t i = 0; i < total; i++) {
        if (interleaved[i] == INT16_MIN || interleaved[i] == INT16_MAX)
            return true; /* D-06: any extreme sample */
    }
    return false;
}

bool ea_flags_silent(const int16_t *interleaved, uint32_t samples_per_channel,
                     uint8_t channels)
{
    const uint32_t total = samples_per_channel * channels;
    if (total == 0)
        return true; /* unreachable for valid frames (E-05); defensive */

    double sum_sq = 0.0;
    for (uint32_t i = 0; i < total; i++) {
        const double x_i = (double)interleaved[i] / 32768.0;
        sum_sq += x_i * x_i;
    }
    const double l_rms = sqrt(sum_sq / (double)total);
    if (l_rms == 0.0)
        return true; /* §7.1 degenerate case: L_dBFS = -inf */
    const double l_dbfs = 20.0 * log10(l_rms);
    return l_dbfs < -60.0; /* D-05 */
}
