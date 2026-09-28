/* Intent: Simulated source adapter (W2 apparatus, C-05): scripted blocks, gap
 * injection, fault injection. Permissive profile; no transforms (C-07). */
#ifndef EA_SIM_ADAPTER_H
#define EA_SIM_ADAPTER_H

#include "edge_audio/edge_audio.h"

extern const ea_adapter_desc ea_sim_adapter;

/* Push n_frames' worth of DC-valued samples (K-06-sized blocks), no gaps. */
ea_status ea_sim_push_frames(ea_stream *s, uint32_t n_frames, int16_t value);

/* Push one block of samples_per_channel DC samples with a gap report before. */
ea_status ea_sim_push_block(ea_stream *s, uint32_t samples_per_channel,
                            int16_t value, uint64_t gap_before);

#endif /* EA_SIM_ADAPTER_H */
