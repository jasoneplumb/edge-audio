# Changelog

## v0.1.0 — 2026-11-11

First public release. It contains the complete `SPEC.md` v0.6 implementation first tagged as
the 2026-09-28 pre-release, the source adapters and build support added since, and the
licensing and release tooling needed to publish it.

No code in this repository has been run on target hardware under its own tests. The host
suite, the hardware-free adapter tests and an ESP-IDF compile of the target firmware are the
evidence; K-02, K-03 (device) and T-17 remain *verification pending* (see "Known limitations"
and the README's "Validation status").

### Added

- Apache-2.0 `LICENSE`, `NOTICE` and `THIRD_PARTY_NOTICES.md`. The notices inventory the
  build and test dependencies and attribute the ES8311 register values in the target adapter
  to M5Unified (MIT).
- Release checks, run by CTest: `tools/check_release.sh` (nothing restricted, generated or
  binary is tracked; no credentials, email addresses or home-directory paths in tracked text;
  license files present; version strings agree and no suffixed form of the release version
  appears; documentation links and repository paths resolve), `tools/check_cited_commits.sh`
  (every commit hash cited in the documents is an ancestor of `HEAD`), and
  `tests/release_checks_selftest.sh`, which plants a defect for each check and requires the
  checkers to fail.
- CI job `esp-idf`: compiles the T-17 soak firmware (`adapters/soundguardian/example`) for the
  ESP32-S3 in the `espressif/idf:v5.5.3` container. It is compile-only; nothing is flashed.
- Release workflow (`.github/workflows/release.yml`): a plain `vX.Y.Z` tag push re-runs the
  host checks against the tagged tree, checks the tag against this changelog, and publishes a
  GitHub release from `.github/release-notes/<tag>.md`. Suffixed tags are rejected.
- WAV file source adapter (`adapters/wav`, issue #8): a RIFF/WAVE PCM_S16LE file played into a
  stream in real time. One K-06 block is released per 20 ms on an absolute `CLOCK_MONOTONIC`
  schedule, so lateness is counted but never accumulates as drift. Each block gets one anchor
  before its push, joining the sample clock to the release time. Any format other than the
  stream's exact rate and channels is refused at open (R-02, D-13). An optional injected
  `ADAPTER_FAULT` after N blocks drives a real adapter down T-10's path (E-06). It is POSIX C11
  only, so it is built and tested on every CMake host; the clock is injectable for tests.
- ALSA source adapter for Linux hosts (`adapters/alsa`, issue #5): exact rate and channel
  negotiation with no resampling (R-02, D-13); the PCM never stops on overrun, and loss
  measured from the hardware pointer is reported as `gap_samples` (E-03, D-09); one
  `CLOCK_MONOTONIC` anchor per block; ALSA errors reach `ea_capture_fault` (E-06). It is built
  and tested when alsa-lib is found; `test_alsa` runs without a sound card through ALSA's
  file and null plugins.
- Root `platformio.ini` with a native C11 smoke-build environment for component-local
  PlatformIO/IDE use. The authoritative host/conformance suite remains CMake + CTest;
  product-specific ESP32/ESP-IDF configuration remains owned by the consuming device
  repository. CI builds it on every main-branch push and pull request.

### Changed

- The library reports version `0.1.0` (`ea_version()`).
- `adapters/soundguardian/idf_component.yml` declares version `0.1.0` and requires
  ESP-IDF ≥ 5.5. It previously said ≥ 5.1, but the adapter needs the `esp_driver_i2c`
  component, which releases before 5.3 do not have, and it has only been compiled with 5.5.
- CI pins its actions to commit SHAs, runs with read-only permissions, cancels superseded
  runs, can be dispatched by hand, and fetches the full history for the cited-commit check.
- README: release version, a "Validation status" table separating host, compile-only and
  hardware evidence, build and test instructions that work from a clean checkout (Ninja is
  optional; alsa-lib and ESP-IDF are listed), and a license section.
- `.gitignore` excludes captured audio and recordings (`*.wav`, `*.pcm`, `*.earc`) and the
  ESP-IDF/PlatformIO state a local build creates.

### Fixed

- SoundGuardian/M5StickS3 target adapter now compiles against ESP-IDF 5.5 and uses the board
  configuration bench-validated in the separate SoundGuardian bring-up: I2C1 on GPIO47/48,
  ES8311 address 0x18, I2S1 on GPIO18/17/15/16, 16 kHz mono LEFT slot.
- Replaced the nonexistent `es8311_init_default` call and positional zero-filled I2S structs
  with the ES8311 register sequence from that bring-up and ESP-IDF default/configuration macros.
- I2S overflow callback now uses ESP-IDF's `bool` ISR callback signature.
- Target shutdown waits for the capture task before deleting the I2S channel, avoiding a
  read/delete race.
- Removed the unused `espressif/es8311` managed-component dependency; the target adapter now
  uses the same direct codec initialization sequence as the M5StickS3 bring-up.

### Included from the 2026-09-28 pre-release

The complete implementation of `SPEC.md` v0.6 (implementation-grade, Level 3), built
test-first in seven gated waves per `IMPLEMENTATION_PLAN.md`. C11 with opaque handles; 1,325
production code lines, 1,102 test lines, 15/15 acceptance groups green at the time.

- Deterministic audio-ingest core: monotonic PCM_S16LE frames on an explicit sample-count
  clock, fixed 20 ms framing, per-frame clipping/silence evidence flags (§7.1 oracles).
- Bounded frame queue with drop-oldest overflow episodes: exactly one immutable
  `QUEUE_OVERFLOW` event per episode, emitted at episode end — including episodes still open
  at `stop()`/fault/`close()`; loss is never silent.
- Structured event surface (`QUEUE_OVERFLOW` / `DISCONTINUITY` / `ADAPTER_FAULT`) with
  per-kind `at_sample` semantics and `events_dropped` accounting on the bounded event ring.
- Optional bounded history ring with a gap ledger: exact-interval, bit-identical reads or an
  explicit `UNAVAILABLE` reason (`EVICTED` / `NEVER_CAPTURED` / `OUT_OF_RANGE` /
  `SPANS_GAP`) — gap-overlapping intervals are refused, never zero-filled.
- Stream lifecycle with a full operation × state validity matrix, three-valued `pop`
  (frame / empty / end-of-stream), and `StreamInfo` metadata queryable in every state.
- EARC recorded-frame-stream format, recorder apparatus, and the `replay` CLI: bit-exact
  window reproduction positioned by `first_sample_index` only (wall clock is metadata).
- SoundGuardian / M5StickS3 adapter: ES8311/I2S ESP-IDF component with atomic DMA-counter
  gap reports (never wall clock), SRAM/PSRAM-aware memory port, 16 kHz-mono profile gate,
  and the T-17 soak firmware.
- `soak_host` rehearsal tool: 4-hour real-time run recorded PASS — 720,000 frames, zero
  starvation, zero unannounced discontinuities.
- Boundary/purity gate (`check_boundary.sh`, run as a test): no hardware or wall-clock
  references outside the adapter, no logging in the core, loss accounting confined to the
  queue, and an `Intent:` header required in every source file.
- Fixes: ISR-safe gap counter in the ES8311 adapter (atomic exchange; a racing increment
  could previously leave hardware loss unannounced); `ea_open` fails instead of silently
  degrading when a requested history ring cannot be allocated; overflow guard on the replay
  CLI's user-supplied window size; zero-channel headers rejected; SPEC.md F-017
  (mid-accumulation gap semantics pinned: partial frames lost to a gap are announced with it).
- Documentation: `SPEC.md` v0.6 (13-section implementation-grade specification, 57 traced
  ids, all 15 design decisions ratified); `IMPLEMENTATION_PLAN.md`, `SPEC_REVIEW_REPORT.md`
  (Level 3 / READY), and `SPEC_BUILD_REPORT.md` (wave ledger, per-ID walk, measured sizes,
  verdict).

### Known limitations

- Hardware verification pending: K-02 memory placement, the K-03 device soak, and T-17
  require an M5StickS3 (recorded as `VERIFICATION PENDING` in `SPEC_BUILD_REPORT.md`). The
  target firmware compiles; it has not been flashed or run from this repository.
- The ALSA adapter is tested without a sound card. Real-device timing (overrun under load,
  hardware timestamps) is not tested here.
- CommonGround profile (O-2, R-09, T-16) deferred per the spec's D-10 until appliance
  hardware is settled.
