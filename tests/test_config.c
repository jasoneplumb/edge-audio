/*
 * T-07 — C-06 StreamConfig validation at open (proves R-11, C-06, E-04,
 * K-06).
 * Invalid configs (unsupported rate, rate failing K-06 integrality, zero
 * channels, profile violation) are rejected with INVALID_CONFIG; a valid
 * config succeeds.
 */
#include "support/test.h"
#include "edge_audio/edge_audio.h"

static bool accept_all(const ea_stream_config *cfg)
{
    (void)cfg;
    return true;
}

static bool only_16k_mono(const ea_stream_config *cfg) /* K-01-shaped profile */
{
    return cfg->sample_rate_hz == 16000 && cfg->channels == 1;
}

static const ea_adapter_desc perm = { "perm", accept_all, NULL, 0 };
static const ea_adapter_desc sg = { "sg", only_16k_mono, NULL, 0 };

static ea_stream_config valid_cfg(void)
{
    ea_stream_config c = { 16000, 1, 8, 0 };
    return c;
}

static void valid_config_succeeds(void) /* T-07, R-11 */
{
    ea_stream_config c = valid_cfg();
    EA_CHECK_EQ(ea_config_validate(&c, &perm), EA_OK);
}

static void zero_rate_rejected(void) /* T-07, E-04 */
{
    ea_stream_config c = valid_cfg();
    c.sample_rate_hz = 0;
    EA_CHECK_EQ(ea_config_validate(&c, &perm), EA_ERR_INVALID_CONFIG);
}

static void non_integral_frame_rate_rejected(void) /* T-07, E-04, K-06 */
{
    ea_stream_config c = valid_cfg();
    c.sample_rate_hz = 8001; /* 0.02 * 8001 = 160.02 is not an integer */
    EA_CHECK_EQ(ea_config_validate(&c, &perm), EA_ERR_INVALID_CONFIG);
}

static void zero_channels_rejected(void) /* T-07, E-04 */
{
    ea_stream_config c = valid_cfg();
    c.channels = 0;
    EA_CHECK_EQ(ea_config_validate(&c, &perm), EA_ERR_INVALID_CONFIG);
}

static void zero_queue_capacity_rejected(void) /* T-07, E-04: unusable queue */
{
    ea_stream_config c = valid_cfg();
    c.queue_capacity = 0;
    EA_CHECK_EQ(ea_config_validate(&c, &perm), EA_ERR_INVALID_CONFIG);
}

static void profile_violation_rejected(void) /* T-07, E-04; K-01-shaped gate */
{
    ea_stream_config c = valid_cfg();
    c.sample_rate_hz = 48000; /* K-06-valid, but not this profile */
    EA_CHECK_EQ(ea_config_validate(&c, &sg), EA_ERR_INVALID_CONFIG);
    c = valid_cfg();
    EA_CHECK_EQ(ea_config_validate(&c, &sg), EA_OK);
}

int main(void)
{
    EA_RUN(valid_config_succeeds);
    EA_RUN(zero_rate_rejected);
    EA_RUN(non_integral_frame_rate_rejected);
    EA_RUN(zero_channels_rejected);
    EA_RUN(zero_queue_capacity_rejected);
    EA_RUN(profile_violation_rejected);
    return EA_TEST_SUMMARY();
}
