# edge-audio

[![ci](https://github.com/jasoneplumb/edge-audio/actions/workflows/ci.yml/badge.svg)](https://github.com/jasoneplumb/edge-audio/actions/workflows/ci.yml)
![spec](https://img.shields.io/badge/spec-v0.6-blue)
![language](https://img.shields.io/badge/C11-zero%20dependencies-lightgrey)
![targets](https://img.shields.io/badge/targets-host%20%C2%B7%20ESP--IDF%20%E2%89%A55.1-blue)

A platform-independent, deterministic audio-ingest boundary: monotonic PCM_S16LE frames on an
explicit sample-count clock, bounded queues with announced loss, optional bounded history, and
bit-exact host replay. Normative specification: [`SPEC.md`](SPEC.md) (v0.6); build evidence:
[`SPEC_BUILD_REPORT.md`](SPEC_BUILD_REPORT.md).

## Repository role

An **independently versioned component repository**: it owns one contract and is released and
tested on its own cadence. Products that need an exact revision may mount it as a Git
submodule. Contract dependencies: none.

## Setup

- **Host (build, tests, replay):** a C11 compiler, CMake ≥ 3.21, pthreads. No other
  dependencies; everything host-side is offline and deterministic.
- **Target (SoundGuardian / M5StickS3):** ESP-IDF ≥ 5.1. The adapter uses the bench-validated
  M5StickS3 ES8311 register sequence directly and does not require a separate codec component.

```bash
cmake -B build -G Ninja
cmake --build build
```

## Quick start (host)

```bash
# run the acceptance suite (§9.1 + §9.2 via the simulated adapter)
ctest --test-dir build --output-on-failure

# a 60-second capture soak at real-time pace
./build/soak_host --seconds 60

# replay a sample window from a recorded frame stream
# (tests/test_replay produces build/test_replay.earc as a sample recording)
./build/replay build/test_replay.earc --window 300,200 > window.pcm
```

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

### SoundGuardian target (ESP32-S3)

`adapters/soundguardian/` is an ESP-IDF component (16 kHz mono profile, M5StickS3 ES8311 + I2S;
hot memory in internal SRAM, history PSRAM-eligible). Its board configuration matches the
validated SoundGuardian bring-up: I2C1 GPIO47/48, I2S1 GPIO18/17/15/16, LEFT slot. The T-17 soak firmware:

```bash
cd adapters/soundguardian/example && idf.py build flash monitor
```

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
tools/                replay CLI + lib, soak_host, check_boundary.sh (T-13)
tests/                one executable per §9 group; support/ = sim adapter, recorder, harness
```

## Verification

```bash
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure --output-junit junit.xml   # full §9 suite
./build/soak_host --seconds 14400                                     # K-03 host rehearsal
speccheck check --spec SPEC.md --src src,include,adapters,tools \
    --tests tests --results build/junit.xml --out build/speccheck     # citation walk
```

speccheck has no C test-case adapter, so its per-test join is file-level; the authoritative
per-ID walk is recorded in `SPEC_BUILD_REPORT.md` (grep-based, per the spec-build fallback).

## Scope / deferrals

Implemented in full except, per the spec's own D-10 deferral: **O-2** (CommonGround profile),
**R-09** (transform versioning behavior; the C-07 carrier exists and is empty) and **T-16** —
excluded from conformance until the commonground-device hardware is settled. **T-17** (4-hour
device soak) and **K-02** placement verification require an M5StickS3 and ESP-IDF and are
*verification pending*; the host-side logic rehearsal is `soak_host`.
