/*
 * SoundGuardian / M5StickS3 source adapter (C-05; profile K-01).
 * profile.c is pure and host-compilable; es8311_i2s.c and port_esp.c are the
 * ESP-IDF half (R-06: all hardware detail stays behind this boundary).
 */
#ifndef EA_SOUNDGUARDIAN_H
#define EA_SOUNDGUARDIAN_H

#include "edge_audio/edge_audio.h"

/* K-01 profile gate: 16 kHz, mono, PCM_S16LE; no transforms (R-09). */
extern const ea_adapter_desc ea_soundguardian_adapter;

/* Target-only surface (es8311_i2s.c): open a stream with the SoundGuardian
 * profile, bring up ES8311 + I2S, and run the capture task that pushes
 * blocks with hardware-counter gap reports (C-05, E-03, D-09). */
ea_status ea_sg_start(uint32_t queue_capacity, uint32_t history_capacity,
                      ea_stream **out);
ea_status ea_sg_stop(ea_stream *s);

#endif /* EA_SOUNDGUARDIAN_H */
