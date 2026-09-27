# Specification Review Report — edge-audio SPEC.md v0.3

> - **Reviewed document:** `SPEC.md` v0.3 (commit 7faa02e)
> - **Method:** four-pass review (comprehension, local precision, cross-consistency, implementation simulation) per the spec-review skill
> - **Reviewer:** Claude (spec-review), 2026-09-27

## 1. Executive Summary

SPEC.md v0.3 is a strong Level-2 specification approaching Level 3. Its core contracts — the
sample-count clock (C-01), queue semantics (C-02), history reads (C-03), and the never-blocks
invariant (I-003) — are precise, observable, and traced to tests. §12 is exemplary: all nine
defaulted decisions are recorded with rejected alternatives and ratification status.

The review found **0 CRITICAL, 4 HIGH, 5 MEDIUM, 3 LOW** findings. The HIGH findings cluster
in two areas: (1) features the spec *names* but never *specifies* — the `CLIP_ONSET`/
`SILENCE_ONSET` event kinds, the CommonGround profile (O-2), and the R-09 transform-version
metadata, none of which have a defined trigger, contract carrier, or constraint row; and
(2) one genuine semantic hole — the behavior of sample intervals that span a discontinuity is
undefined for both history reads and replay, where three reasonable implementations
(error, zero-fill, skip) would all "conform."

**Strengths:** the sample-clock/index algebra in C-01; explicit loss semantics (I-002); a
complete E-nn table with deterministic outcomes; full §11 traceability (every R/C/I/K/E id has
a test); a §12 with real decisions, not placeholders.

**Weaknesses:** the CommonGround half of the system is a name, not a specification; two event
kinds exist only as enum values; gap-spanning reads are unspecified.

## 2. Overall Maturity

**Level 2 — Implementable.** A competent engineer can build the SoundGuardian path from this
document with minimal guessing. The CommonGround path and two event kinds cannot be built or
tested as written. Resolving the four HIGH findings (all are localized additions, not
restructuring) would move the spec to Level 3.

## 3. Findings Summary

| ID | Severity | Location | Summary |
| -- | -------- | -------- | ------- |
| F-001 | HIGH | C-04, R-03, §9 | `CLIP_ONSET` / `SILENCE_ONSET` event kinds have no trigger semantics, requirement, or test |
| F-002 | HIGH | §7, O-1/O-2, K-02 | CommonGround profile (O-2) is named but unspecified; O-1/O-2 never declared; "embedded profile" terminology drifts |
| F-003 | HIGH | R-09, C-01, T-16 | Transform version has no carrier in any contract; T-16 is untestable as written |
| F-004 | HIGH | C-03, R-08, E-02 | Sample intervals spanning a discontinuity are undefined for history reads and replay |
| F-005 | MEDIUM | K-04, E-01, C-02, T-03 | Overflow "episode" boundary undefined; C-02 wording implies per-push events, K-04 per-episode |
| F-006 | MEDIUM | C-04, E-03, T-02 | `at_sample` semantics per event kind undefined; T-02's "correct `at_sample`" has no defined expected value |
| F-007 | MEDIUM | §5.1, §3.1, E-06 | No valid-state matrix per operation; `pop`-to-drain in `STOPPED`/`FAULTED` contradicts blanket wrong-state→E-09; end-of-stream vs empty indistinguishable |
| F-008 | MEDIUM | E-03, C-05, D-09 | Who estimates the gap, and how, is unspecified |
| F-009 | MEDIUM | R-02, C-05 | Normalization is unconstrained: allowed conversions, downmix/rounding rules, non-16-bit native formats |
| F-010 | LOW | C-01, E-07 | `stream_id` uniqueness scope unspecified |
| F-011 | LOW | K-06 | Integrality of $0.02 \cdot \mathrm{sample\_rate\_hz}$ unstated for rates not divisible by 50 |
| F-012 | LOW | §12, C-02/§5.1 | Batched consistency nits: D-08 *Affects* omits K-05 and R-02; C-02 `pop` "blocking or polling" vs §5.1 "or empty" |

## 4. Detailed Findings

### F-001 — `CLIP_ONSET` / `SILENCE_ONSET` events are enum values, not behavior

**Severity:** HIGH

**Location:** C-04 (`kind` enumeration); R-03; §9

**Observation:** C-04 enumerates `CLIP_ONSET` and `SILENCE_ONSET` event kinds, but no
requirement, edge case, constraint, or test defines when they fire. R-03 covers only the
per-frame flags. §5.2 says "every loss or fault is exactly one event kind" — clip and silence
onsets are neither loss nor fault, so even the event surface's own framing excludes them.
D-05 explicitly rejected hysteresis, so "onset" has no defined meaning: first flagged frame
after an unflagged one? Every flagged frame? Is there a matching offset?

**Why it matters:** Two conforming implementations could emit these events at materially
different times, or never, and both pass every test in §9 (no T-nn cites them).

**Potential consequence:** SoundGuardian's acoustic-mute observation, the stated consumer of
silence evidence, gets platform-dependent event timing; the ids are also invisible to the
§11 matrix (no rows exist for these behaviors).

**Recommended resolution:** Either (a) delete both kinds from C-04 and let consumers derive
onsets from the per-frame flags — the lowest-complexity option, consistent with the D-05/D-06
statelessness rationale — or (b) add an R-nn defining onset as the first frame whose flag is
set after a frame where it was not (one bit of state per flag), an offset rule, and a T-nn.
Option (a) is recommended.

### F-002 — The CommonGround profile is a name, not a specification

**Severity:** HIGH

**Location:** §7 (profiles); O-1/O-2; K-02; R-09; T-16; §10

**Observation:** Three related gaps. (1) O-1 and O-2 are cited in R-04, R-09, C-05, C-06,
§3.3, T-16, and Figure 3.2, but no table or subsection ever *declares* them — a reader cannot
find what O-2 is beyond inference. (2) §7 has a constraint row only for the SoundGuardian
profile (K-01); the CommonGround profile has no sample rate, channel count, platform, or
adapter constraints — yet T-16 requires running it. (3) Terminology drifts: K-01 names the
"SoundGuardian / M5StickS3 profile", K-02/T-17/§10 say "embedded profile", T-16 says
"CommonGround profile" — whether K-02's SRAM/PSRAM rule binds the CommonGround device is
undecidable. D-01's single ESP-IDF + POSIX build also leaves the CommonGround appliance's
platform unstated.

**Why it matters:** Half of the stated §0 purpose ("CommonGround uses it ahead of VAD/ASR")
is unimplementable and unverifiable from this document.

**Potential consequence:** `spec-build` either invents a CommonGround profile or silently
skips T-16; K-02 gets applied (or not) to the wrong device.

**Recommended resolution:** Add an *Optional items* table declaring **O-1** (bounded history)
and **O-2** (CommonGround profile) with one-line definitions and their gates. Add a K-nn row
for the CommonGround profile (or an explicit §12 row deferring it with T-16 marked deferred).
Replace "embedded profile" with "SoundGuardian profile" everywhere, or define "embedded
profile" as a term covering both.

### F-003 — R-09's transform version has nowhere to live

**Severity:** HIGH

**Location:** R-09; C-01; T-16

**Observation:** R-09 requires transform versions to appear "in the frame stream's metadata",
and T-16 asserts that "frame stream metadata carries the transform version" — but C-01 has no
metadata field for it, and no other contract defines a stream-level metadata record. The
frame contract as pinned cannot carry what R-09 requires.

**Why it matters:** The requirement and its test reference a data shape that does not exist;
an implementer must invent a contract, which is exactly what §4 exists to prevent.

**Potential consequence:** Divergent per-product sideband formats for the one field whose
whole purpose is cross-build replay compatibility.

**Recommended resolution:** Add a stream-level metadata contract (e.g. `C-07 — StreamInfo
{ stream_id, sample_rate_hz, channels, transform_versions[] }`, emitted once at `start` or
queryable from the handle), cite it from R-09 and T-16, and add its §11 row. If O-2 is
deferred per F-002, defer this with it.

### F-004 — Intervals spanning a discontinuity are undefined

**Severity:** HIGH

**Location:** C-03; E-02; R-08; T-04

**Observation:** Because the clock advances across gaps (E-03/D-09), a history-read or replay
interval can include sample indices that were never captured. C-03's response is "exactly the
requested interval, bit-identical" or `UNAVAILABLE` — but none of the three `UNAVAILABLE`
reasons clearly covers "partially captured": `NEVER_CAPTURED` reads as *none of it*. R-08's
"any recorded sample window" has the same hole.

**Why it matters:** Three reasonable implementations — return `UNAVAILABLE`, zero-fill the
gap, or return only the captured portion — all arguably satisfy the current text; the third
violates I-004's spirit but a tester cannot prove which was intended.

**Potential consequence:** Evidence-extraction code written against one implementation
silently misbehaves on another; T-04/T-05 pass on all three.

**Recommended resolution:** One sentence in C-03 and one in R-08: an interval that overlaps
any uncaptured (gap) index is `UNAVAILABLE { reason: NEVER_CAPTURED }` (or a new
`SPANS_GAP` reason), for history and replay alike; add the case to E-02 and a fixture to
T-05/T-04.

### F-005 — Overflow "episode" boundary is undefined

**Severity:** MEDIUM

**Location:** K-04; E-01; C-02; T-03

**Observation:** K-04 mandates "at most one `QUEUE_OVERFLOW` event per contiguous overflow
episode" without defining when an episode ends (first successful un-dropped push? first
consumer pop? a full drain?). C-02's `push` description ("on full queue … emits QueueOverflow
event") reads as one event per push, contradicting K-04. T-03 asserts "exactly one" event, so
the ambiguity lands directly in a test's pass condition.

**Why it matters / potential consequence:** Two implementations produce different event
counts for the same stall pattern; T-03 passes for one and fails for the other.

**Recommended resolution:** Define: an episode begins at the first drop after a non-dropping
push and ends at the next push that does not drop; the single event is emitted at episode end
(or start, with `frames_dropped` updated — pick one). Align C-02's wording with K-04.

### F-006 — `at_sample` has no per-kind definition

**Severity:** MEDIUM

**Location:** C-04; E-03; T-02

**Observation:** `at_sample` is "the sample-clock position of the event", but for a
`DISCONTINUITY` event is that the first lost index, the first index after the gap, or the
last delivered index? For `QUEUE_OVERFLOW`, the first dropped frame's index or the episode
end? T-02 asserts a "correct `at_sample`" that the spec never defines.

**Why it matters / potential consequence:** T-02's expected value is ambiguous — the test
test fails: two testers would disagree about a passing build.

**Recommended resolution:** One line per event kind in C-04, e.g. `DISCONTINUITY.at_sample` =
first lost sample index; `QUEUE_OVERFLOW.at_sample` = `first_sample_index` of the oldest
dropped frame.

### F-007 — No valid-state matrix; drain semantics contradict wrong-state errors

**Severity:** MEDIUM

**Location:** §5.1; §3.1; E-06; E-09

**Observation:** §5.1 marks `pop` and `poll_events` "wrong state → E-09" without saying which
states are valid. But §3.1 says a `STOPPED` stream's queue "is drained" and E-06 says queued
frames "remain drainable" in `FAULTED` — so `pop` must be *valid* in both. Similarly,
`poll_events` after a fault is the only way to see `ADAPTER_FAULT`. And §5.1's `pop` returns
"next `AudioFrame` or empty", while E-06 requires a distinguishable "end-of-stream" after
drain — the operations table gives them the same observable.

**Why it matters / potential consequence:** T-08 ("wrong-state operations return
`INVALID_STATE` per the §3.1 table") is not decidable from the §3.1 table, which lists
transitions, not operation validity; consumers cannot distinguish "queue momentarily empty"
from "stream over".

**Recommended resolution:** Add a small operation × state validity matrix to §5.1 (rows:
operations; columns: `CONFIGURED`/`RUNNING`/`STOPPED`/`FAULTED`), and give `pop` a
three-valued result: frame | empty | end-of-stream.

### F-008 — Gap estimation is unowned

**Severity:** MEDIUM

**Location:** E-03; C-05; D-09

**Observation:** The clock "advances by the estimated gap", but nothing says the adapter
supplies the estimate or how it is derived (DMA counters, hardware FIFO status, elapsed
time). C-05's `provides` line mentions only sample blocks and a format descriptor.

**Why it matters / potential consequence:** If the capture core estimates from wall time, it
quietly violates the R-07 principle that wall clock never drives sample positioning; adapter
implementations will differ in gap accuracy with no stated tolerance.

**Recommended resolution:** Extend C-05: the adapter reports each loss as
`(gap_samples : uint64)` alongside the next block, derived from hardware counters where
available; the capture core MUST use the adapter's figure and MUST NOT derive gaps from wall
clock.

### F-009 — Normalization is a verb without rules

**Severity:** MEDIUM

**Location:** R-02; C-05

**Observation:** R-02 requires normalizing "channel count and sample representation" to
C-01, and C-05 has the adapter hand over a "native format descriptor" — implying the capture
core converts. Nothing bounds what conversions are in scope (bit-depth reduction? channel
downmix? never resampling?) or pins rounding/dither/mix rules where a conversion is allowed.

**Why it matters / potential consequence:** Bit-exact replay (I-005) starts at the capture
boundary: if a 24→16-bit or stereo→mono conversion is implementation-chosen, two builds
produce different "canonical" samples from identical hardware input.

**Recommended resolution:** Given D-08, the simplest rule: the adapter MUST deliver samples
already in PCM_S16LE at the stream's rate and channel count; the capture core performs no
sample-value conversion (R-02 reduces to layout/interleaving). Any adapter-internal
conversion is the adapter's, versioned under R-09 if replay-relevant.

### F-010 — `stream_id` uniqueness scope unspecified

**Severity:** LOW

**Location:** C-01; E-07

**Observation:** `stream_id` is "opaque, constant for the life of one stream" and E-07
requires a "new" id per stream, but unique within what scope — process, boot, device
lifetime? History and replay are keyed by it.

**Recommended resolution:** One line: unique per device boot (monotonic counter suffices);
collisions across reboots are permitted and disambiguated by the consumer's recording
context.

### F-011 — K-06 integrality unstated

**Severity:** LOW

**Location:** K-06; E-04

**Observation:** $0.02 \cdot \mathrm{sample\_rate\_hz}$ is non-integral for rates not
divisible by 50 (e.g. 44100). Profiles currently pin 16 kHz, but E-04's "unsupported rate" is
not tied to this rule.

**Recommended resolution:** Add to K-06/E-04: a rate where $0.02 \cdot
\mathrm{sample\_rate\_hz} \notin \mathbb{N}$ is an invalid configuration.

### F-012 — Batched consistency nits

**Severity:** LOW

**Location:** §12 (D-08 row); C-02 vs §5.1

**Observation:** (1) D-08's *Affects* cell omits K-05 and R-02, both of which its choice
constrains (the §7.1 normalization $x_i = s_i/32768$ and the normalization target); the
column is machine-read, so the impact walk misses them. (2) C-02 says `pop` is "blocking or
polling per binding" while §5.1 pins "next `AudioFrame` or empty" (polling) — pick one.

**Recommended resolution:** Add `K-05, R-02` to D-08's *Affects*; make §5.1's polling
semantics the single normative statement and align C-02.

## 5. Requirements Review

R-01..R-11 are observable, use normative verbs correctly, and cite sources. R-09 references a
contract that does not exist (F-003). R-02 is the only requirement whose obligation is a verb
without rules (F-009). No conflicting requirements found. Missing requirements: onset-event
behavior if C-04's extra kinds are kept (F-001).

## 6. Interface and Data-Contract Review

C-01, C-02, C-03, C-06 are pinned with types, required/optional status, and defaults; C-01's
index algebra is exemplary. Gaps: no stream-level metadata contract (F-003); no per-kind
`at_sample` definition and no `detail` payload schema beyond one example (F-006); C-05 lacks
the gap-report field (F-008). *Visual-surface completeness:* n/a — no rendered surface, and
§5 says so explicitly.

## 7. State and Failure Review

The lifecycle table and diagram agree edge-for-edge (diagram correctly captioned as
illustrative). Failure semantics are strong: every E-nn has a deterministic outcome, faults
preserve drainable frames, and event-queue overflow is itself counted (E-08). The one
structural gap is operation validity per state and the end-of-stream observable (F-007).
Recovery/retry are correctly out of scope (no retry paths; restart is a new stream, E-07).

## 8. Determinism and Algorithm Review

§7.1 is a model metric definition: formula in a `$$..$$` block, symbols defined, units,
population, and degenerate cases stated. The clipping rule is exact. Determinism holes:
gap-size estimation (F-008) and normalization conversions (F-009) are the two places where
identical hardware input could yield different canonical streams.

## 9. Edge-Case Review

E-01..E-09 cover the boundary set well (empty block, invalid config, wrong state, fault,
restart, both queue overflows). Missing: interval-spanning-gap (F-004) and the K-06
integrality boundary (F-011). Concurrent `open` of multiple streams is unaddressed — whether
multiple simultaneous streams are supported at all is unstated; acceptable as implementation
freedom for these two single-mic devices, but one line would remove the question.

## 10. Non-Functional Requirement Review

K-01..K-06 are measurable. K-03's "zero capture starvation" is given an operational
definition (adapter-side loss caused by the component). K-02 is checkable via linker/map
inspection. No latency budget exists for frame delivery; given I-003 and the bounded queue,
that is defensible freedom rather than a defect.

## 11. Security and Trust-Boundary Review

The trust model is simple and stated: everything is in-process and trusted; the deterministic
↔ probabilistic boundary is drawn in §0. R-10's "never log raw audio" is the one
privacy-relevant rule and it is tested (T-14). Nothing further is warranted at this scope.

## 12. Observability and Provenance Review

Strong: every loss is an event with a sample-clock position, `stream_id` ties frames, events,
and history together, and R-09 (once F-003 is fixed) covers transform provenance. The
event-queue drop counter (E-08) closes the "who watches the watcher" hole. `stream_id` scope
is the only loose end (F-010).

## 13. Testing and Verification Review

Every I/K/E id has at least one T id; T-17 correctly uses the *(recorded)* marker with a
stated presence check. Oracles are independent (synthetic fixtures, known dBFS levels,
adapter-side counters — no self-generated goldens). Ambiguous pass conditions: T-02
(F-006), T-03 (F-005), T-08 (F-007), T-16 (F-002/F-003). No observed group is needed and the
spec says why.

## 14. Metrics and Evaluation Review

§7.1 is complete (formula, units, population, denominator, degenerate case). No other metrics
exist. K-06's expression needs the integrality rule (F-011).

## 15. Traceability Review

§11 has a row for every declared R/C/I/K/E id with a component and test — no orphans, no
UNCITED-by-design ids except via the recorded marker. Defects: O-1/O-2 are cited but never
declared (F-002); D-08's *Affects* cell is incomplete (F-012); `CLIP_ONSET`/`SILENCE_ONSET`
behaviors have no ids at all (F-001). Cross-project `sg:` ids are correctly namespaced.

## 16. Internal-Consistency Review

The v0.3 decision edits were applied consistently (20 ms, PCM_S16LE, 4 h appear once each,
no stale values found). Remaining contradictions: C-02 per-push event vs K-04 per-episode
(F-005); §5.1 `pop` polling vs C-02 "blocking or polling" (F-012); blanket wrong-state→E-09
vs drainability (F-007); "embedded profile" vs named profiles (F-002).

## 17. Architecture Review

The adapter/capture/queue/history decomposition supports every requirement; dependency
direction is clean (hardware knowledge flows only into the adapter, I-006 enforces it, T-13
checks it). State ownership is unambiguous. No redesign warranted.

## 18. Implementation-Agent Readiness

**NO — MATERIAL QUESTIONS REMAIN.** Minimum blocking questions an implementer must ask:

1. When do `CLIP_ONSET`/`SILENCE_ONSET` fire — or do I omit them? (F-001)
2. What is the CommonGround profile — do I build T-16 at all? (F-002)
3. Where does the transform version live? (F-003)
4. What do I return for a history/replay interval that spans a gap? (F-004)

Questions 5–9 (episode boundary, `at_sample`, state matrix, gap estimation, normalization)
are answerable with defensible defaults but should be pinned before conformance is claimed.

## 19. Quality Scorecard

| Dimension | Score |
| --------- | ----: |
| Scope clarity | 5 |
| Terminology | 3 |
| Requirement precision | 4 |
| Interface completeness | 3 |
| Visual-surface completeness | n/a |
| Data-contract completeness | 3 |
| State/lifecycle definition | 4 |
| Algorithm precision | 4 |
| Failure semantics | 4 |
| Edge-case coverage | 4 |
| Non-functional requirements | 4 |
| Security specification | 4 |
| Observability/provenance | 4 |
| Testability | 4 |
| Evaluation/metrics | 5 |
| Traceability | 4 |
| Internal consistency | 3 |
| Architecture consistency | 5 |
| Implementation readiness | 3 |

## 20. Remediation Plan

### P0 — Blocking (resolve before implementation)

- **F-001** — delete `CLIP_ONSET`/`SILENCE_ONSET` from C-04 (recommended) or specify them.
- **F-002** — declare O-1/O-2; add a CommonGround K row **or** an explicit §12 deferral row
  gating R-09/T-16; unify "embedded profile" terminology.
- **F-003** — add the stream-metadata contract (or defer with F-002's deferral).
- **F-004** — pin gap-spanning interval semantics for C-03 and R-08.

### P1 — Important (resolve before claiming conformance)

- **F-005** — define the overflow episode; align C-02 with K-04.
- **F-006** — define `at_sample` per event kind.
- **F-007** — add the operation × state validity matrix and the end-of-stream observable.
- **F-008** — make the adapter own the gap estimate; forbid wall-clock derivation.
- **F-009** — require adapters to deliver PCM_S16LE at stream rate/channels (no core
  conversion).

### P2 — Improvement (may defer)

- **F-010** — `stream_id` uniqueness scope.
- **F-011** — K-06 integrality rule in E-04.
- **F-012** — D-08 *Affects* completion; C-02/§5.1 `pop` wording.

Note that F-001, F-002 (deferral option), F-004, F-008, and F-009 all *shrink* the system,
consistent with the v0.3 simplification direction.

## 21. Final Verdict

```text
Specification maturity:
Level 2

Implementation readiness:
NOT READY

Primary blocker:
Named-but-unspecified features (CLIP_ONSET/SILENCE_ONSET events, the CommonGround
profile, R-09's metadata carrier) and undefined gap-spanning interval semantics.

Most important improvement:
Resolve the four P0 findings by deletion or deferral where possible — each is a
localized edit, and three of the four reduce scope rather than add it.
```
