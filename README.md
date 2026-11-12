# edge-audio

[![ci](https://github.com/jasoneplumb/edge-audio/actions/workflows/ci.yml/badge.svg)](https://github.com/jasoneplumb/edge-audio/actions/workflows/ci.yml)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)
![spec](https://img.shields.io/badge/spec-v0.6-blue)
![language](https://img.shields.io/badge/C11-zero%20dependencies-lightgrey)
![targets](https://img.shields.io/badge/targets-host%20%C2%B7%20ESP--IDF%20%E2%89%A55.5-blue)

A platform-independent, deterministic audio-ingest boundary: monotonic PCM_S16LE frames on an
explicit sample-count clock, bounded queues with announced loss, optional bounded history, and
bit-exact host replay. Normative specification: [`SPEC.md`](SPEC.md) (v0.6); build evidence:
[`SPEC_BUILD_REPORT.md`](SPEC_BUILD_REPORT.md).

**Release:** `v0.1.0`, the first public release ([`CHANGELOG.md`](CHANGELOG.md)). The core
and its host tooling are complete and tested on the host. The ESP32-S3 target adapter
compiles but has not been run on a device from this repository; see
[Validation status](#validation-status).

## Repository role

An **independently versioned component repository**: it owns one contract and is released and
tested on its own cadence. Products that need an exact revision may mount it as a Git
submodule. Contract dependencies: none.

## Setup

- **Host (core, tests, replay, WAV adapter):** a C11 compiler (GCC or Clang), CMake ≥ 3.21,
  and POSIX threads. Nothing else; everything host-side is offline and deterministic.
  [Ninja](https://ninja-build.org) is optional (`-G Ninja`).
- **ALSA adapter (Linux, optional):** alsa-lib headers, e.g. `sudo apt-get install
  libasound2-dev`. CMake builds `adapters/alsa` and its test only when alsa-lib is found;
  `-DEA_REQUIRE_ALSA=ON` turns its absence into a configuration error (CI uses it).
- **Target (SoundGuardian / M5StickS3, ESP32-S3):** ESP-IDF 5.5. The adapter has been compiled
  with ESP-IDF 5.5.3 (CI) and 5.5.4; releases before 5.3 lack the `esp_driver_i2c` component
  it needs. It writes the M5StickS3 ES8311 register sequence directly and does not need a
  separate codec component.
- **PlatformIO (optional):** `pip install platformio`, then `pio run -e native-smoke` builds
  the native smoke environment in `platformio.ini`. CMake + CTest remain the authoritative suite.

## Quick start (host)

From a clean checkout:

```bash
git clone https://github.com/jasoneplumb/edge-audio.git
cd edge-audio
cmake -B build                 # or: cmake -B build -G Ninja
cmake --build build

# the acceptance suite (§9.1 + §9.2 via the simulated adapter) and the release checks
ctest --test-dir build --output-on-failure

# a 60-second capture soak at real-time pace
./build/soak_host --seconds 60

# replay a sample window from a recorded frame stream
# (tests/test_replay writes build/test_replay.earc as a sample recording)
./build/replay build/test_replay.earc --window 300,200 > window.pcm
```

`release_cited_commits` needs the full Git history and is reported as skipped in a shallow
clone (`git clone --depth 1`) or a source archive.

## Usage

### Library API (§5.1 of the spec)

`include/edge_audio/edge_audio.h` — one stream per capture session, opaque handle:

| Operation | Notes |
| --- | --- |
| `ea_open(cfg, adapter, &s)` | validates against the adapter's profile; `EA_ERR_INVALID_CONFIG` on rejection |
| `ea_start(s)` | sample clock starts at 0 |
| `ea_pop(s, &frame, &kind)` | non-blocking; `kind` = frame \| empty \| end_of_stream |
| `ea_read_history(s, id, first, count, out, &why)` | exact interval or `EA_ERR_UNAVAILABLE` with `EVICTED`/`NEVER_CAPTURED`/`OUT_OF_RANGE`/`SPANS_GAP` |
| `ea_poll_events(s, out, max)` | `QUEUE_OVERFLOW` / `DISCONTINUITY` / `ADAPTER_FAULT`, each with `events_dropped` |
| `ea_stream_info_get(s, &info)` | constant per stream; empty `transform_versions` on SoundGuardian |
| `ea_stop(s)` / `ea_close(s)` | close from RUNNING implies stop; open overflow episodes emit first |

Adapters push via `ea_capture_push_block(s, samples, n, gap_before)` (gap reports come from
hardware counters, never wall clock) and `ea_capture_fault(s, code)`.

Format is always PCM_S16LE and frame duration is fixed at 20 ms (320 samples at 16 kHz);
neither is configurable.

### replay CLI

```
replay <file.earc> --window <first>,<count>
```

Raw interleaved PCM_S16LE on stdout. Exit codes: `0` ok, `2` usage/unreadable file, `3` window
unavailable (`SPANS_GAP` or `OUT_OF_RANGE` on stderr — a gap window is refused, never
zero-filled).

### Host source adapters

- `adapters/wav/` plays a RIFF/WAVE PCM_S16LE file into a stream in real time (one 20 ms block
  per period on an absolute monotonic schedule), with optional fault injection. POSIX only.
- `adapters/alsa/` captures from an ALSA PCM on Linux with exact rate/channel negotiation and
  overrun loss reported as gaps.

### SoundGuardian target (ESP32-S3)

`adapters/soundguardian/` is an ESP-IDF component (16 kHz mono profile, M5StickS3 ES8311 + I2S;
hot memory in internal SRAM, history PSRAM-eligible). Its board configuration matches the
SoundGuardian bring-up: I2C1 GPIO47/48, I2S1 GPIO18/17/15/16, LEFT slot. To build the T-17 soak
firmware from a clean checkout:

```bash
. "$IDF_PATH/export.sh"        # an ESP-IDF 5.5 installation
cd adapters/soundguardian/example
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor # needs an M5StickS3
```

## Validation status

What has run, where, and what it shows. Nothing here was run on target hardware from this
repository.

| Area | How it was checked | Result |
| --- | --- | --- |
| Core library, replay, `soak_host` (§9.1, §9.2 via the simulated adapter) | CTest on the host: Linux in CI, macOS locally | pass |
| WAV adapter | CTest on the host, scripted clock | pass |
| ALSA adapter | CTest on Linux in CI, through ALSA's file and null plugins (no sound card) | pass; real-device timing not tested |
| SoundGuardian adapter sources | `target_stub_compile`: host syntax check against stub ESP-IDF headers | pass; syntax and boundary only |
| T-17 soak firmware | `idf.py build` for ESP32-S3: CI job `esp-idf` (ESP-IDF 5.5.3) and a local build (5.5.4) | compiles; never flashed or run |
| K-03 host rehearsal | `./build/soak_host --seconds 14400`, once, 2026-09-27/28 ([`SPEC_BUILD_REPORT.md`](SPEC_BUILD_REPORT.md) §5) | pass; starvation and announcement logic only |
| M5StickS3 board configuration (pins, ES8311 address, register sequence) | bench-validated in the separate SoundGuardian bring-up | not reproduced here |
| K-02 memory placement, K-03 device soak, T-17 | need an M5StickS3 | **not run — verification pending** |
| CommonGround profile (O-2, R-09, T-16) | — | deferred per the spec's D-10 |

## Artifacts and schemas

- **EARC recorded frame stream** (`tools/replay/earc_format.h`, version 1): header
  (magic `EARC`, stream id, rate, channels, frame size) + per-frame records
  (`first_sample_index`, flags, wall clock as metadata only, samples). Written by consumers
  (the test recorder is `tests/support/recorder.c`); read by `replay`.

## Project layout

```
include/edge_audio/   public API: types.h (contracts), edge_audio.h, port.h, version.h
src/                  core: stream.c (lifecycle/capture), queue.c (bounded queue + episodes),
                      events.c, history.c (ring + gap ledger), flags.c (§7.1), config.c,
                      port_posix.c, internal.h
adapters/soundguardian/  K-01 profile gate (host-testable), ES8311/I2S capture task,
                         ESP-IDF port + component + soak example
adapters/alsa/        Linux ALSA source adapter
adapters/wav/         real-time WAV file source adapter
tools/                replay CLI + lib, soak_host, check_boundary.sh (T-13),
                      check_release.sh and check_cited_commits.sh (release checks)
tests/                one executable per §9 group; support/ = sim adapter, recorder, harness
```

## Verification

```bash
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure --output-junit junit.xml   # full §9 suite + release checks
./build/soak_host --seconds 14400                                     # K-03 host rehearsal
speccheck check --spec SPEC.md --src src,include,adapters,tools \
    --tests tests --results build/junit.xml --out build/speccheck     # citation walk (optional)
```

[speccheck](https://github.com/jasoneplumb/speccheck) is a separate tool. It has no C test-case
adapter, so its per-test join is file-level; the authoritative per-ID walk is recorded in
`SPEC_BUILD_REPORT.md` (grep-based, per the spec-build fallback).

## Scope / deferrals

Implemented in full except, per the spec's own D-10 deferral: **O-2** (CommonGround profile),
**R-09** (transform versioning behavior; the C-07 carrier exists and is empty) and **T-16** —
excluded from conformance until the commonground-device hardware is settled. **T-17** (4-hour
device soak) and **K-02** placement verification require an M5StickS3 and are
*verification pending*; the host-side logic rehearsal is `soak_host`.

## License

Apache-2.0: see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists the toolchains and libraries this
repository builds against (none is redistributed) and the M5Unified (MIT) attribution for the
ES8311 register values. Development was AI-assisted; commit messages carry the co-author
trailers.
