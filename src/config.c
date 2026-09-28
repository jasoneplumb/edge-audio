/*
 * Intent: C-06 validation at open (realizes R-11, E-04, K-06).
 * Rejections: zero/unsupported rate, rate failing K-06 integrality
 * (0.02 * sample_rate_hz must be a positive integer, i.e. rate % 50 == 0),
 * zero channels, zero queue capacity (an unusable queue cannot honor C-02),
 * and adapter profile violations (K-01-shaped gates via C-05).
 */
#include "edge_audio/edge_audio.h"

ea_status ea_config_validate(const ea_stream_config *cfg,
                             const ea_adapter_desc *adapter)
{
    if (cfg == NULL || adapter == NULL)
        return EA_ERR_INVALID_CONFIG;
    if (cfg->sample_rate_hz == 0 || cfg->sample_rate_hz % 50u != 0)
        return EA_ERR_INVALID_CONFIG; /* K-06 integrality (E-04) */
    if (cfg->channels == 0)
        return EA_ERR_INVALID_CONFIG;
    if (cfg->queue_capacity == 0)
        return EA_ERR_INVALID_CONFIG;
    if (adapter->config_supported != NULL && !adapter->config_supported(cfg))
        return EA_ERR_INVALID_CONFIG; /* profile violation (E-04, K-01) */
    return EA_OK;
}
