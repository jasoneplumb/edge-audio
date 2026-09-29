# edge-audio — IEEE-style requirements specification

Status: terse companion to the normative detailed `SPEC.md`; requirement IDs below summarize the stable component boundary rather than replacing detailed contracts.

## 1. Purpose and scope
Provide deterministic, bounded, replayable PCM ingest with an authoritative sample-count clock. Hardware capture details remain behind an adapter; detector/ASR/classification logic is out of scope.

## 2. Functional requirements
- **EA-R-001** — Capture SHALL produce monotonic PCM frames with an explicit `first_sample_index`.
- **EA-R-002** — Adapter input SHALL already be canonical PCM_S16LE at the configured rate/channel count.
- **EA-R-003** — Frame clipping, silence/level evidence, and discontinuity SHALL be explicit.
- **EA-R-004** — Capture-to-consumer queues SHALL be bounded and capture SHALL NOT block on consumers.
- **EA-R-005** — Every loss, overflow, discontinuity, and adapter fault SHALL be observable.
- **EA-R-006** — Bounded history, when enabled, SHALL return an exact requested sample interval or an explicit unavailable result; it SHALL NOT silently truncate or zero-fill gaps.
- **EA-R-007** — `first_sample_index` SHALL be authoritative for deterministic timing; wall-clock time SHALL NOT drive replay decisions.
- **EA-R-008** — Stream configuration SHALL remain fixed while running.

## 3. Interface requirements
- **EA-C-001 AudioFrame** — stream id, first sample index, fixed stream format metadata, PCM samples, and evidence flags.
- **EA-C-002 FrameQueue** — bounded, ordered, non-blocking capture-side enqueue with explicit overflow semantics.
- **EA-C-003 HistoryRead** — exact interval or explicit unavailable reason.
- **EA-C-004 Event** — structured overflow/discontinuity/adapter-fault surface.
- **EA-C-005 SourceAdapter** — sole owner of codec/I2S/DMA/microphone hardware details.

## 4. Invariants
- **EA-I-001** — Delivered frames SHALL be strictly ordered by sample index.
- **EA-I-002** — A positive uncaptured sample gap SHALL be marked as a discontinuity.
- **EA-I-003** — Diagnostics and history maintenance SHALL NOT block capture.
- **EA-I-004** — Raw audio payloads SHALL NOT be emitted through the diagnostics event surface.

## 5. Verification
- **EA-T-001** — Host deterministic framing/clock test verifies EA-R-001, EA-R-002, EA-R-007.
- **EA-T-002** — Queue saturation test verifies bounded non-blocking behavior and explicit loss reporting.
- **EA-T-003** — History exactness/gap test verifies EA-R-006.
- **EA-T-004** — Recorded-stream replay reproduces captured sample windows bit-exactly where no gap exists.

## 6. SoundGuardian integration trace
The device audio adapter owns M5StickS3 ES8311/I2S configuration and feeds this component. Detector and mute-observer consume canonical frames. Product scheduling SHALL preserve the non-blocking capture invariant. Detailed normative contracts and acceptance limits remain in `SPEC.md`.
