# SPECIFICATION — edge-audio (audio ingest, PCM capture, sample-clock, bounded history; embedded library)

> - **Status:** v0.2 — draft for implementation review (restructured from the v0.1 prose spec)
> - **Language / stack:** language-agnostic component contract; reference profiles target ESP32-S3 (ESP-IDF, C/C++) and a Linux appliance (see §7, D-01) | surface: library API
> - **Sources:** prior prose `SPEC.md` (v0.1, commit 1d23c74); `README.md` (repository role); SoundGuardian spec ids transferred here — `sg:C-06`, `sg:C-07`, audio side of `sg:C-13`, `sg:C-14`, audio-history side of `sg:C-26`, and the sample-count-clock rule introduced in SoundGuardian v0.8
> - **Scope of this document:** the platform-independent audio-ingest boundary — frame contract, sample clock, queues, bounded history, and evidence flags. It does not own detector logic, ASR, product policy, or hardware bring-up beyond the adapter boundary (§0 non-goals).
> - **Normative language:** MUST/MUST NOT/SHALL/SHALL NOT = normative; SHOULD = strong recommendation; MAY = optional.
> - **Principle:** capture is deterministic and never waits — the sample-count clock is the single source of timing truth, and every loss of data is explicit.

---

## 0. Intent and purpose

edge-audio is a platform-independent, deterministic audio-ingest boundary for systems that
need bounded, replayable acoustic evidence. SoundGuardian uses it for TV audio capture,
detector input, and acoustic mute observation. CommonGround uses it ahead of VAD/ASR/
addressability.

The component sits on the **deterministic side** of a deterministic ↔ probabilistic boundary:
everything downstream of the frame queue (detectors, VAD, ASR, classification) is
probabilistic and out of scope; everything this spec owns — frame production, the sample
clock, queues, history — is deterministic and replayable. edge-audio owns the deterministic
side; the consuming product owns the probabilistic side.

**Relationship to SoundGuardian:** this repository extracts and now owns the SoundGuardian
contracts listed in the front-matter Sources. SoundGuardian consumes this spec's contracts and
MUST NOT redefine them. The transferred ownership maps as: `sg:C-06` → C-01 (frame contract),
`sg:C-07` → C-02 (frame queue), audio side of `sg:C-13` and `sg:C-14` → C-04 (events),
audio-history side of `sg:C-26` → C-03 (history read), SoundGuardian v0.8 sample-count-clock
rule → R-01/R-07.

**Non-goals:**

- No commercial classification.
- No ASR semantics.
- No action authorization.
- No network, UI, or flash work on the real-time capture path.

## 1. Actors and goals

| Actor | Goals |
| ----- | ----- |
| **Source adapter** (`adapter`) | Owns hardware/codec specifics (e.g. ES8311/I2S, ALSA); produces raw samples into the capture path; trusted, in-process |
| **Capture core** (`capture`) | Turns raw samples into canonical `AudioFrame`s (C-01), maintains the sample clock, sets evidence flags, feeds the frame queue (C-02) |
| **Frame consumer** (`consumer`) | A product pipeline (detector, VAD/ASR front-end) that drains the frame queue; may be slower than capture and must never stall it |
| **History reader** (`history`) | Reads bounded audio history by exact sample interval (C-03) for evidence extraction |
| **Host replay tool** (`replay`) | Offline process that reproduces sample windows from recorded frames (R-08); runs on a development host, not the device |
| **Operator / integrator** | Configures a platform profile (§7), consumes overflow and discontinuity events (C-04) for diagnostics |

## 2. Requirements (intent, high level)

| ID | Statement |
| -- | --------- |
| **R-01** | The capture core MUST produce monotonic PCM frames carrying an explicit sample-count clock: each frame's `first_sample_index` per C-01. *(Source: v0.1 "Owned behavior"; SoundGuardian v0.8 sample-count-clock rule.)* |
| **R-02** | The capture core MUST normalize channel count and sample representation to the canonical frame contract (C-01) at the component boundary; consumers MUST NOT receive adapter-native formats. *(Source: v0.1 "Owned behavior".)* |
| **R-03** | The capture core MUST report clipping, silence/level evidence, and discontinuities via the per-frame flags in C-01, computed per the formulas in §7.1. *(Source: v0.1 "Owned behavior".)* |
| **R-04** | The component MAY maintain bounded audio history (O-1); when enabled, history writes MUST NOT block or delay the capture path (I-003). *(Source: v0.1 "Owned behavior".)* |
| **R-05** | All queues between capture and consumers MUST be bounded; overflow MUST be surfaced as a structured event (C-04) and MUST NOT silently discard data without one. *(Source: v0.1 "Owned behavior", "Acceptance".)* |
| **R-06** | Hardware-specific codec/I2S/microphone details MUST stay behind the source-adapter boundary (C-05); no code outside the adapter may reference them. *(Source: v0.1 "Owned behavior".)* |
| **R-07** | `first_sample_index` is authoritative for timing. Wall-clock timestamps, when present, are metadata and MUST NOT drive deterministic replay decisions. *(Source: v0.1 "Canonical frame contract".)* |
| **R-08** | A host replay tool MUST be able to reproduce any recorded sample window bit-exactly from recorded frames (T-04). *(Source: v0.1 "Acceptance".)* |
| **R-09** | In the CommonGround profile (O-2), adapter-side transforms (beamforming/AEC/VAD) whose outputs become replay inputs MUST be explicitly versioned in the frame stream's metadata. *(Source: v0.1 "Platform profiles".)* |
| **R-10** | Diagnostics: every drop, overflow, discontinuity, and adapter fault MUST be observable through the event surface (C-04); the capture path MUST NOT log raw audio payloads. *(Cross-cutting; derived from v0.1 invariants and non-goals.)* |
| **R-11** | Configuration: sample rate, channel count, frame size, queue depth, and history capacity MUST be fixed at stream-open time (C-06) and MUST NOT change while a stream is `RUNNING`. *(Derived; see D-02, D-03, D-07.)* |

## 3. Behavior and state model

### 3.1 Stream lifecycle

A stream is one continuous capture session with one sample clock.

| From | To | Trigger |
| ---- | -- | ------- |
| — | `CONFIGURED` | `open(config)` accepted (C-06) |
| `CONFIGURED` | `RUNNING` | `start()`; sample clock begins at index $0$ |
| `RUNNING` | `RUNNING` | frame produced; overflow (E-01) or adapter underrun (E-03) sets flags/events but does not change state |
| `RUNNING` | `STOPPED` | `stop()`; queue is drained, no further frames |
| `RUNNING` | `FAULTED` | unrecoverable adapter fault (E-06) |
| `CONFIGURED`, `STOPPED`, `FAULTED` | closed | `close()`; all resources released |

```mermaid
stateDiagram-v2
    [*] --> CONFIGURED : open(config) (C-06)
    CONFIGURED --> RUNNING : start() (R-11)
    RUNNING --> RUNNING : frame / overflow event (R-05, E-01)
    RUNNING --> STOPPED : stop()
    RUNNING --> FAULTED : adapter fault (E-06)
    CONFIGURED --> [*] : close()
    STOPPED --> [*] : close()
    FAULTED --> [*] : close()
```

*Figure 3.1 — stream lifecycle per R-11, R-05, E-01, E-06. The table above is normative; the
diagram is illustrative.*

A new stream after `close()` gets a fresh `stream_id` and a sample clock restarting at $0$
(E-07). There is no resume: sample indices never continue across streams.

### 3.2 Capture flow

```mermaid
flowchart LR
    HW["hardware codec / mic"] --> AD["source adapter (C-05)"]
    AD --> CAP["capture core: normalize (R-02), clock (R-01), flags (R-03)"]
    CAP --> Q["bounded frame queue (C-02)"]
    CAP -.-> H["bounded history ring (C-03, O-1)"]
    Q --> CONS["frame consumer"]
    CAP --> EV["event surface (C-04)"]
```

*Figure 3.2 — capture path per R-01..R-05; the dotted edge is the optional history write
(O-1), which is non-blocking per I-003.*

### 3.3 Durable artifacts

The component itself persists nothing. Recorded frame streams written by a consumer (for
replay per R-08) are the consumer's artifacts; their replayability is guaranteed by C-01 and
I-001/I-005, not by any storage format this spec pins. Raw audio remains ephemeral by default
at the product level (O-2).

## 4. Interfaces / contracts

### C-01 — Canonical audio frame

```text
AudioFrame {
  stream_id          : opaque identifier, constant for the life of one stream
  first_sample_index : uint64        # index of samples[0] within the stream, per channel frame
  sample_rate_hz     : uint32        # fixed per stream (R-11)
  channels           : uint8         # fixed per stream (R-11)
  format             : PCM_S16LE | implementation-approved equivalent (D-08)
  samples[]          : interleaved samples; length > 0 (E-05); length = frame_size * channels
  flags {
    discontinuity    : bool          # samples were lost immediately before this frame (I-002)
    clipped          : bool          # per §7.1 clipping rule
    silent           : bool          # per §7.1 silence rule
  }
  wall_clock         : optional metadata; never authoritative (R-07)
}
```

Required fields: all except `wall_clock`. `first_sample_index` counts sample *frames* (one
per channel set), not individual interleaved values. For consecutive frames $n$ and $n+1$ of
the same stream with no loss, $\mathrm{first\_sample\_index}_{n+1} =
\mathrm{first\_sample\_index}_n + N_n$ where $N_n$ is frame $n$'s per-channel sample count;
any other positive gap requires `discontinuity = true` on frame $n+1$ (I-001, I-002).

### C-02 — Frame queue

```text
FrameQueue {
  capacity : fixed at open (D-03)
  push     : capture-side, non-blocking; on full queue applies drop-oldest (D-03),
             emits QueueOverflow event (C-04), and marks the next delivered frame
             discontinuity=true
  pop      : consumer-side; blocking or polling per binding; never affects capture timing
}
```

Ordering: frames are delivered in strictly increasing `first_sample_index` order (I-001).
Idempotency: a frame is delivered at most once.

### C-03 — Bounded history read

```text
HistoryRead {
  request  : (stream_id, first_sample_index, sample_count)
  response : exactly the requested interval, bit-identical to the captured samples
           | UNAVAILABLE { reason: EVICTED | NEVER_CAPTURED | OUT_OF_RANGE }
}
```

Partial results MUST NOT be returned: the response is the exact interval or an explicit
`UNAVAILABLE` (I-004, E-02). History capacity is fixed at open (D-07); eviction is oldest-first.

### C-04 — Structured event

```text
Event {
  stream_id
  kind      : QUEUE_OVERFLOW | DISCONTINUITY | ADAPTER_FAULT | CLIP_ONSET | SILENCE_ONSET
  at_sample : uint64   # sample-clock position of the event (R-07)
  detail    : kind-specific payload (e.g. frames_dropped for QUEUE_OVERFLOW)
}
```

Events are the sole diagnostics surface for loss and faults (R-10). Event emission MUST NOT
block capture (I-003); the event queue is itself bounded, and its overflow is counted, not
hidden (E-08).

### C-05 — Source adapter boundary

```text
SourceAdapter {
  provides : raw sample blocks + native format descriptor to the capture core
  owns     : codec registers, I2S/DMA configuration, microphone topology,
             optional front-end transforms (O-2, R-09)
  must not : expose hardware types/headers across the component boundary (R-06)
}
```

The adapter is the only replaceable per-platform part; two builds differing only in adapter
MUST be indistinguishable to consumers except for `sample_rate_hz`/`channels` values.

### C-06 — Stream configuration

```text
StreamConfig {
  sample_rate_hz   : uint32   (profile-constrained, §7)
  channels         : uint8
  format           : per C-01
  frame_size       : samples per channel per frame (D-02)
  queue_capacity   : frames (D-03)
  history_capacity : samples per channel, 0 = history disabled (O-1, D-07)
}
```

Invalid configurations are rejected at `open` (E-04); nothing is captured for a rejected open.

## 5. Interface specification

The component has one surface: a **library API**. There is no CLI, GUI, or network surface, so
no operations table for those and no observed test group (§9).

### 5.1 Library API operations

| Operation | Inputs | Outputs | Errors | Notes |
| --------- | ------ | ------- | ------ | ----- |
| `open` | `StreamConfig` (C-06) | stream handle, state `CONFIGURED` | invalid config → E-04 rejection | validates against active profile (§7) |
| `start` | handle | state `RUNNING`; sample clock at $0$ | wrong state → E-09 | |
| `pop` / consume | handle | next `AudioFrame` (C-01) or empty | wrong state → E-09 | never blocks capture (I-003) |
| `read_history` | `HistoryRead` request (C-03) | exact interval or `UNAVAILABLE` | E-02 | only when `history_capacity > 0` |
| `poll_events` | handle | zero or more `Event`s (C-04) | wrong state → E-09 | |
| `stop` | handle | state `STOPPED`; queue drainable | wrong state → E-09 | |
| `close` | handle | resources released | — | valid from any state |

### 5.2 Cross-cutting contract — diagnostics and errors

Quiet by default: a healthy stream emits no events. Every loss or fault is exactly one event
kind (C-04). Raw audio payloads are never written to any log or event (R-10). Error reporting
from operations uses the binding's native mechanism (status code, exception, `Result`) but the
error *categories* are fixed by E-02, E-04, E-09.

## 6. Invariants (must hold in every valid implementation)

| ID | Invariant |
| -- | --------- |
| **I-001** | Within a stream, `first_sample_index` is strictly increasing across delivered frames; it never moves backward or repeats. |
| **I-002** | Every dropped or lost sample interval is announced: the next delivered frame carries `discontinuity = true` and a `DISCONTINUITY` or `QUEUE_OVERFLOW` event exists for it. Loss is never silently hidden. |
| **I-003** | The capture path never waits on inference, UI, network, logging, storage, history writes, or event delivery. |
| **I-004** | A bounded-history read returns the exact sample interval requested, bit-identical, or an explicit `UNAVAILABLE` — never a partial or approximated result. |
| **I-005** | Given identical recorded frame streams, replay is deterministic: the same sample windows are reproduced bit-exactly on any host (R-08). |
| **I-006** | No code outside the source adapter references hardware-specific types, registers, or headers (R-06). |

## 7. Constraints (precise and measurable)

| ID | Constraint |
| -- | ---------- |
| **K-01** | SoundGuardian / M5StickS3 profile: `sample_rate_hz` = 16000, `channels` = 1, `format` = PCM_S16LE. The source adapter owns ES8311/I2S details. |
| **K-02** | On the embedded profile, DMA buffers and hot queues MUST reside in internal SRAM; only larger rolling history MAY use PSRAM. |
| **K-03** | Soak: a stream MUST run $\geq$ 8 hours (D-04) with zero capture starvation (no adapter-side sample loss caused by this component) and zero unannounced discontinuities. |
| **K-04** | Queue overflow behavior is drop-oldest with an event per overflow episode (D-03); at most one `QUEUE_OVERFLOW` event per contiguous overflow episode, carrying the total `frames_dropped`. |
| **K-05** | Clipping and silence flag thresholds per §7.1: clipping at full-scale sample magnitude; silence at $L_{\mathrm{dBFS}} < -60$ (D-05, D-06). |

### 7.1 Level, silence, and clipping formulas

For a frame with per-channel sample count $N > 0$ and normalized samples
$x_i \in [-1, 1]$ (for PCM_S16LE, $x_i = s_i / 32768$ over all interleaved samples,
population = the whole frame):

$$
\begin{aligned}
L_{\mathrm{rms}} &= \sqrt{\frac{1}{N \cdot C}\sum_{i=1}^{N \cdot C} x_i^2} \\
L_{\mathrm{dBFS}} &= 20 \log_{10} L_{\mathrm{rms}}
\end{aligned}
$$

where $C$ is the channel count. `silent = true` iff $L_{\mathrm{dBFS}} < -60$ dBFS
(units: dB relative to full scale). `clipped = true` iff any raw sample equals the format's
extreme values ($-32768$ or $32767$ for PCM_S16LE). Degenerate case: $N = 0$ cannot occur
(frames are non-empty per C-01/E-05); if $L_{\mathrm{rms}} = 0$, $L_{\mathrm{dBFS}}$ is
$-\infty$ and the frame is silent.

## 8. Edge cases and failure semantics

| ID | Case | Semantics |
| -- | ---- | --------- |
| **E-01** | Frame queue full when capture pushes | Oldest frame(s) dropped (K-04); one `QUEUE_OVERFLOW` event per episode; next delivered frame has `discontinuity = true`. Capture never blocks. |
| **E-02** | History read for an evicted, never-captured, or out-of-range interval | `UNAVAILABLE` with reason `EVICTED`, `NEVER_CAPTURED`, or `OUT_OF_RANGE`; no partial data. |
| **E-03** | Adapter underrun/overrun (hardware lost samples) | Next frame carries `discontinuity = true`; `DISCONTINUITY` event with the gap's `at_sample`; sample clock advances by the estimated gap so the clock stays aligned to real capture time. |
| **E-04** | Invalid `StreamConfig` at `open` (unsupported rate/format, zero frame size, profile violation) | `open` is rejected with a category-`INVALID_CONFIG` error; no stream exists; no resources retained. |
| **E-05** | Adapter delivers an empty sample block | Discarded by the capture core; no frame is produced; no clock advance. |
| **E-06** | Unrecoverable adapter fault while `RUNNING` | Stream enters `FAULTED`; `ADAPTER_FAULT` event; already-queued frames remain drainable; further `pop` after drain reports end-of-stream. |
| **E-07** | Stream restart after `close` | New `stream_id`; sample clock restarts at $0$; history from the prior stream is not readable. |
| **E-08** | Event queue itself overflows | Oldest events dropped; a drop counter is maintained and reported in the next delivered event's `detail`; capture is unaffected (I-003). |
| **E-09** | Operation called in wrong state (e.g. `start` while `RUNNING`) | Rejected with a category-`INVALID_STATE` error; stream state unchanged. |

## 9. Acceptance criteria, tests, and evals

No rendered surface exists, so there is no observed test group (§5); conformance is fully
scriptable plus one recorded soak run.

### 9.1 Unit (fast, deterministic)

| ID | Test |
| -- | ---- |
| **T-01** | Feed synthetic adapter blocks; assert every delivered frame satisfies the C-01 shape and the index equation in C-01 (proves R-01, R-02, I-001). |
| **T-02** | Inject a simulated adapter gap; assert the next frame has `discontinuity = true` and a `DISCONTINUITY` event with correct `at_sample` (proves I-002, E-03). |
| **T-03** | Fill the queue past capacity; assert drop-oldest, exactly one `QUEUE_OVERFLOW` event with correct `frames_dropped`, and `discontinuity = true` on the next delivered frame (proves R-05, K-04, E-01). |
| **T-05** | History: write then read an exact interval → bit-identical samples; read an evicted and an out-of-range interval → `UNAVAILABLE` with the correct reason (proves R-04, I-004, E-02). |
| **T-06** | Known fixtures for §7.1: a full-scale frame sets `clipped`; a $-70$ dBFS frame sets `silent`; a $-40$ dBFS frame sets neither (proves R-03, K-05). |
| **T-07** | Invalid configs (unsupported rate, zero frame size, profile violation) are rejected at `open` with `INVALID_CONFIG`; valid config succeeds (proves R-11, E-04). |
| **T-08** | Wrong-state operations return `INVALID_STATE` without changing stream state, per the §3.1 table (proves R-11, E-09). |
| **T-09** | Empty adapter block produces no frame and no clock advance (proves E-05). |
| **T-10** | Fault injection: adapter fault moves stream to `FAULTED`, emits `ADAPTER_FAULT`, queued frames drain, then end-of-stream (proves E-06). |
| **T-11** | Event-queue overflow drops oldest events and reports the drop count in the next event (proves E-08, R-10). |
| **T-12** | Restart after `close`: new `stream_id`, clock at $0$, prior history unreadable (proves E-07). |
| **T-13** | Static check: no source file outside the adapter includes/references hardware-specific headers or symbols (grep/link-based; proves R-06, I-006). |
| **T-14** | No log or event payload contains raw audio samples, verified against a capture run's full diagnostic output (proves R-10). |

### 9.2 Integration

| ID | Test |
| -- | ---- |
| **T-04** | Record a frame stream from a live or simulated adapter; on a host, replay tool reproduces three arbitrary sample windows bit-exactly, using only `first_sample_index` (never wall clock) for positioning (proves R-07, R-08, I-005). |
| **T-15** | Concurrency: a consumer that stalls for 10 s while capture runs causes overflow events but zero capture-side starvation, measured via adapter-side counters (proves I-003, R-05). |
| **T-16** | CommonGround profile with a versioned front-end transform enabled: frame stream metadata carries the transform version; replay of that stream reports the version (proves R-09, O-2). |

### 9.3 Soak (recorded)

| ID | Test |
| -- | ---- |
| **T-17** *(recorded)* | 8-hour soak on the embedded profile (K-01..K-03): zero capture starvation, zero unannounced discontinuities (every gap has an event), memory placement verified SRAM/PSRAM per K-02. Result recorded in `SPEC_BUILD_REPORT.md` §soak; the suite carries a presence check that the recorded report exists and asserts these fields (proves K-01, K-02, K-03). |

## 10. Dependencies and environment

- **Contract dependencies:** none at the repository-contract level (per `README.md`); this
  repository is independently versioned and MAY be mounted as a Git submodule by products.
- **Runtime:** binding-specific (D-01). Embedded profile: ESP-IDF toolchain for ESP32-S3;
  host profile: any environment able to run the test suite and replay tool.
- **Test suite:** MUST run deterministic groups (§9.1, §9.2 with simulated adapter) on a
  development host with no hardware attached; T-17 alone requires target hardware.
- **Environment variables / host prerequisites:** none required by the contract; anything a
  binding adds MUST be documented in that binding's README.

## 11. Traceability matrix (id → where realized)

Component names are the §1/§4 assignments; `spec-build` fills in real modules and test files.

| Spec id | Where realized (component / module) | Verified by |
| ------- | ----------------------------------- | ----------- |
| R-01 | capture core | T-01 |
| R-02 | capture core | T-01 |
| R-03 | capture core (flag computation) | T-06 |
| R-04 | history ring | T-05 |
| R-05 | frame queue | T-03, T-15 |
| R-06 | adapter boundary / build layout | T-13 |
| R-07 | frame contract + replay tool | T-04 |
| R-08 | replay tool | T-04 |
| R-09 | CommonGround adapter (O-2) | T-16 |
| R-10 | event surface | T-11, T-14 |
| R-11 | stream lifecycle / config validation | T-07, T-08 |
| C-01 | frame contract | T-01 |
| C-02 | frame queue | T-03 |
| C-03 | history ring | T-05 |
| C-04 | event surface | T-02, T-03, T-11 |
| C-05 | source adapter | T-13 |
| C-06 | config validation | T-07 |
| I-001 | capture core (sample clock) | T-01 |
| I-002 | capture core + events | T-02, T-03 |
| I-003 | capture path design | T-15 |
| I-004 | history ring | T-05 |
| I-005 | replay tool | T-04 |
| I-006 | build layout | T-13 |
| K-01 | embedded profile config | T-07, T-17 |
| K-02 | embedded memory placement | T-17 |
| K-03 | soak | T-17 |
| K-04 | frame queue | T-03 |
| K-05 | flag computation | T-06 |
| E-01 | frame queue | T-03 |
| E-02 | history ring | T-05 |
| E-03 | capture core | T-02 |
| E-04 | config validation | T-07 |
| E-05 | capture core | T-09 |
| E-06 | fault handling | T-10 |
| E-07 | lifecycle | T-12 |
| E-08 | event surface | T-11 |
| E-09 | lifecycle | T-08 |

## 12. Open questions and decisions to confirm

The v0.1 prose spec left the following unsettled; each default below is what the normative
rows now assume.

| ID | Decision | Default taken | Alternatives | Affects | Owner / status |
| -- | -------- | ------------- | ------------ | ------- | -------------- |
| D-01 | Implementation language/stack per binding | Contract stays language-agnostic; embedded binding assumed C/C++ on ESP-IDF | Rust binding; pinning one language in the spec | R-11, C-06 | integrator / confirm |
| D-02 | Frame size (samples per channel per frame) | Configurable at open, no single pinned value | Fixed 10 ms or 20 ms frames | R-11, C-01, C-06 | integrator / confirm |
| D-03 | Queue overflow policy and default capacity | Drop-oldest; capacity configurable at open | Drop-newest; block-with-timeout (rejected: violates I-003) | R-05, C-02, K-04, E-01, T-03 | integrator / confirm |
| D-04 | Soak duration for K-03 ("multi-hour" in v0.1) | 8 hours | 4 h; 24 h | K-03, T-17 | integrator / confirm |
| D-05 | Silence threshold | $-60$ dBFS frame RMS | $-50$ dBFS; hysteresis-based onset/offset | R-03, K-05, T-06 | integrator / confirm |
| D-06 | Clipping definition | Any sample at format extreme value | $\geq$ 3 consecutive extreme samples | R-03, K-05, T-06 | integrator / confirm |
| D-07 | History capacity default and eviction | Capacity set at open, 0 disables; oldest-first eviction | Time-based retention | R-04, C-03, C-06, E-02 | integrator / confirm |
| D-08 | Which "implementation-approved equivalent" formats are allowed beyond PCM_S16LE | None approved yet; PCM_S16LE only until a format is ratified here | PCM_S32LE; float32 | C-01, K-01 | integrator / open |
| D-09 | Sample-clock advance on adapter gaps (E-03): estimated-gap advance vs. no advance | Clock advances by the estimated gap so indices stay aligned to real time | Clock counts only delivered samples (rejected: history reads by time window would drift) | E-03, I-001, T-02 | integrator / confirm |

## Revision history

- **v0.2** — restructured the v0.1 prose spec into the 13-section format; added lifecycle,
  config contract (C-06), event contract details, §7.1 formulas, edge cases E-04..E-09, full
  test and traceability coverage; all newly-defaulted decisions recorded in §12.
- **v0.1** — initial prose specification (commit 1d23c74).
