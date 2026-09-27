# edge-audio — component specification

## Purpose

Provide a platform-independent, deterministic audio-ingest boundary for systems
that need bounded, replayable acoustic evidence. SoundGuardian uses it for TV
audio capture, detector input and acoustic mute observation. CommonGround uses
it ahead of VAD/ASR/addressability.

## Owned behavior

- Produce monotonic PCM frames with an explicit sample-count clock.
- Normalize channel/sample representation at the component boundary.
- Report clipping, silence/level evidence and discontinuities.
- Maintain optional bounded audio history without blocking capture.
- Expose bounded queues; overflow is explicit and observable.
- Keep hardware-specific codec/I2S/microphone details behind adapters.

## Non-goals

- No commercial classification.
- No ASR semantics.
- No action authorization.
- No network/UI/flash work on the real-time capture path.

## Canonical frame contract

```text
AudioFrame {
  stream_id
  first_sample_index : uint64
  sample_rate_hz
  channels
  format             : PCM_S16LE | implementation-approved equivalent
  samples[]
  flags {discontinuity, clipped, silent}
}
```

`first_sample_index` is authoritative for timing. Wall-clock timestamps are
metadata and MUST NOT drive deterministic replay decisions.

## Invariants

- Sample indices never move backward within a stream.
- Dropped frames create a discontinuity; they are never silently hidden.
- Capture cannot wait on inference, UI, network, logging or storage.
- A bounded-history read returns the exact sample interval requested or an
  explicit unavailable result.

## Platform profiles

### SoundGuardian / M5StickS3

16 kHz mono is the current target. DMA/hot queues stay in internal SRAM; larger
rolling history may use PSRAM. The source adapter owns ES8311/I2S details.

### CommonGround appliance

The adapter may add beamforming/AEC/VAD stages, but those transforms must be
explicitly versioned if their outputs become replay inputs. Raw audio remains
ephemeral by default at the product level.

## Acceptance

- Multi-hour soak without capture starvation.
- Zero hidden discontinuities.
- Host replay can reproduce sample windows from recorded frames.
- Queue overflow is surfaced as a structured event.

## Source ownership moved here

SoundGuardian: C-06, C-07, audio side of C-13, C-14, audio-history side of
C-26, and the sample-count-clock rule introduced in v0.8.
