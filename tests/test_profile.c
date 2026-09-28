/*
 * Intent: SoundGuardian profile gate, config side (proves K-01, E-04): the adapter
 * accepts only sample_rate_hz = 16000, channels = 1 (format is PCM_S16LE by
 * construction, D-08), and exposes no transform versions (C-07, R-09).
 */
#include "../adapters/soundguardian/soundguardian.h"
#include "support/test.h"

static void profile_accepts_only_16k_mono(void) /* K-01, E-04 */
{
    ea_stream_config ok = { 16000, 1, 8, 0 };
    ea_stream_config bad_rate = { 48000, 1, 8, 0 };
    ea_stream_config bad_ch = { 16000, 2, 8, 0 };

    EA_CHECK_EQ(ea_config_validate(&ok, &ea_soundguardian_adapter), EA_OK);
    EA_CHECK_EQ(ea_config_validate(&bad_rate, &ea_soundguardian_adapter),
                EA_ERR_INVALID_CONFIG);
    EA_CHECK_EQ(ea_config_validate(&bad_ch, &ea_soundguardian_adapter),
                EA_ERR_INVALID_CONFIG);
    EA_CHECK_EQ(ea_soundguardian_adapter.transform_version_count, 0);
}

int main(void)
{
    EA_RUN(profile_accepts_only_16k_mono);
    return EA_TEST_SUMMARY();
}
