/*
 * Intent: SoundGuardian profile gate (realizes K-01 config side, E-04 profile
 * violation; C-07/R-09: no transforms on this profile). Pure and
 * host-compilable — the hardware half lives in es8311_i2s.c (R-06).
 */
#include "soundguardian.h"

static bool sg_supported(const ea_stream_config *cfg)
{
    return cfg->sample_rate_hz == 16000 && cfg->channels == 1; /* K-01 */
}

const ea_adapter_desc ea_soundguardian_adapter = { "soundguardian",
                                                   sg_supported, NULL, 0 };
