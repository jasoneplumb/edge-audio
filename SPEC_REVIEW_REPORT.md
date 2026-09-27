# Specification Review Report — edge-audio SPEC.md v0.5

> - **Reviewed document:** `SPEC.md` v0.5
> - **Method:** four-pass review (comprehension, local precision, cross-consistency, implementation simulation) per the spec-review skill; second review cycle — the first reviewed v0.3 and produced findings F-001..F-012 (see §3.1)
> - **Reviewer:** Claude (spec-review), 2026-09-27

## 1. Executive Summary

SPEC.md v0.5 is **implementation-grade (Level 3)** and **READY**. The v0.4 uplift resolved
all twelve v0.3 findings without deferrals, and this re-review verified each resolution
against the current text (§3.1). The implementation-simulation pass over v0.5 surfaced four
further issues — one from the new D-15 episode semantics (F-013: an episode still open at
`stop()`/fault would never emit its event) and three residual consistency gaps (F-014:
`close()` from `RUNNING` allowed by §5.1 but absent from §3.1; F-015: I-002's "event exists"
momentarily false under episode-end emission; F-016: stale-`stream_id` history reads had no
defined reason). **All four were resolved in v0.5 itself**, so the document carries **0 open
findings at any severity**.

Every §12 decision (D-01..D-15) is now ratified by the integrator; none remains at `confirm`
or `open` except D-10's deliberately open CommonGround profile definition, which is a scoped
deferral with its conformance impact (O-2, R-09, T-16 excluded) stated in the spec, not an
ambiguity.

## 2. Overall Maturity

**Level 3 — Implementation-grade.** A competent coding agent can implement the SoundGuardian
path with no material semantic inference: every operation has defined inputs, outputs, error
categories, and state validity; every failure has a deterministic outcome; every I/K/E id has
a test with an unambiguous pass condition. The document is not Level 4: it has no mechanically
checkable contracts beyond the id graph, and the deferred O-2 half is intentionally
unspecified.

## 3. Findings Summary

**Open findings: none.** 0 CRITICAL, 0 HIGH, 0 MEDIUM, 0 LOW outstanding.

### 3.1 Resolution verification of the v0.3 findings (F-001..F-012)

| ID | Was | Resolution verified in v0.5 |
| -- | --- | --------------------------- |
| F-001 | HIGH | `CLIP_ONSET`/`SILENCE_ONSET` deleted from C-04; flags-only rule stated (D-12) |
| F-002 | HIGH | O-1/O-2 declared in §7.2; O-2 deferred per D-10 with R-09/T-16 conformance exclusion; "embedded profile" renamed — no stale occurrence remains |
| F-003 | HIGH | C-07 `StreamInfo` pinned; `stream_info` operation in §5.1; T-18 and §11 row present; R-09/T-16 cite it |
| F-004 | HIGH | `SPANS_GAP` in C-03/E-02; R-08 forbids zero-fill/truncation; fixtures in T-04/T-05 (D-14) |
| F-005 | MEDIUM | Episode defined in K-04 (begin/end/emission point); C-02 wording aligned (D-15) |
| F-006 | MEDIUM | Per-kind `at_sample` and `detail` table in C-04; T-02 asserts the defined values |
| F-007 | MEDIUM | Operation × state validity matrix in §5.1; three-valued `pop`; T-08 exercises every cell |
| F-008 | MEDIUM | C-05 requires adapter-reported `gap_samples` from hardware counters; wall-clock derivation forbidden |
| F-009 | MEDIUM | R-02/C-05: adapter delivers canonical PCM_S16LE; core does no sample-value conversion (D-13) |
| F-010 | LOW | `stream_id` boot-scoped in C-01 |
| F-011 | LOW | K-06 integrality rule; E-04 and T-07 carry the case |
| F-012 | LOW | D-08 *Affects* completed (K-05, R-02 added); C-02 `pop` wording aligned with §5.1 |

### 3.2 Findings raised and resolved during this cycle (F-013..F-016)

| ID | Severity (as found) | Summary | Resolution in v0.5 |
| -- | ------------------- | ------- | ------------------ |
| F-013 | MEDIUM | An overflow episode still open at `stop()` or an adapter fault would never end, so its `QUEUE_OVERFLOW` event would never be emitted — silent loss, violating I-002 | K-04: `stop()`/fault ends the episode and the event MUST be emitted before the state transition completes; T-03 gained a mid-episode `stop()` fixture |
| F-014 | MEDIUM | §5.1 declared `close` "valid from any state" but §3.1 had no `RUNNING` → closed transition; whether `close` implies `stop` (and what happens to an open episode) was undefined | §3.1 row and diagram edge added: `close()` from `RUNNING` implies `stop()` first, emitting any open episode's event (K-04); §5.1 note aligned |
| F-015 | LOW | I-002 said an event "exists" for every loss, but under D-15 the flagged frame can be delivered before its episode's event is emitted — the invariant was momentarily false in a conforming build | I-002 rephrased: the event announces the loss "no later than the end of its episode (K-04)" |
| F-016 | LOW | A `read_history` whose `stream_id` names a prior (closed) stream had no defined `UNAVAILABLE` reason, leaving T-12's expected observable ambiguous | C-03: stale `stream_id` → `NEVER_CAPTURED`; T-12 asserts it |

## 4. Detailed Findings

All findings are resolved; the summaries in §3.2 carry the observation and resolution for
each. No open finding requires the full finding format.

## 5. Requirements Review

R-01..R-11 are observable, normative, and sourced. R-09 is cleanly gated: the deferral is in
the requirement text itself, in §7.2, in D-10, and in T-16, so no reader can mistake it for
active scope. R-02 now names both obligated parties (adapter delivers canonical; core does
not convert). No conflicts found.

## 6. Interface and Data-Contract Review

C-01..C-07 all pin shape, types, required/optional status, and defaults. C-04's per-kind
table closes the last payload ambiguity; C-05 now specifies both data and gap-report inputs.
The §5.1 operations table plus validity matrix gives every operation defined inputs, the
three-valued `pop` result, error categories, and state validity. *Visual-surface
completeness:* n/a — no rendered surface (§5 states this).

## 7. State and Failure Review

The §3.1 table, the diagram, and the §5.1 matrix now agree edge-for-edge, including the
re-review's `RUNNING` → closed transition (F-014). Reconstruction of the state machine from
the normative rows matches the diagram exactly. Every failure path lands in a defined state
with a defined observable, and the open-episode-at-termination path (F-013) is closed.

## 8. Determinism and Algorithm Review

The two v0.3 determinism holes (gap estimation, normalization) are closed: gaps come from
adapter hardware counters only, and no sample-value conversion exists in the core. §7.1
remains fully specified with degenerate cases. Identical hardware input now yields an
identical canonical stream by construction (I-005 chain intact).

## 9. Edge-Case Review

E-01..E-09 plus the new C-03 stale-`stream_id` rule cover the boundary set: empty block,
invalid config (including K-06 integrality), gap-spanning reads, wrong-state operations,
faults, restarts, and both queue overflows including mid-episode termination. Multi-stream
concurrency remains unaddressed, which stays legitimate implementation freedom for two
single-microphone devices.

## 10. Non-Functional Requirement Review

K-01..K-06 are measurable with defined conditions; K-03's starvation definition and T-15's
adapter-side-counter measurement make I-003 testable. No latency budget for frame delivery —
defensible freedom given I-003 and the bounded queue, unchanged from the prior review.

## 11. Security and Trust-Boundary Review

Unchanged and adequate: in-process trusted actors, the deterministic ↔ probabilistic boundary
in §0, R-10's no-raw-audio-in-logs rule tested by T-14.

## 12. Observability and Provenance Review

Strong: every loss is an event with a defined sample-clock position, `events_dropped` closes
the event-loss hole, and C-07 gives recordings a self-describing header with transform
provenance ready for O-2. `stream_id` scope is now stated.

## 13. Testing and Verification Review

T-01..T-18: every I/K/E id has at least one test; every pass condition this review could
challenge in v0.3 (T-02, T-03, T-07, T-08, T-12, T-16) now has a defined expected value.
T-16's deferral is stated in the row itself. T-17 keeps the *(recorded)* marker with a
presence check. Oracles remain independent (synthetic fixtures, known injected gaps, defined
dBFS levels, adapter-side counters) — no self-generated goldens. §9's preamble correctly
states there is no observed group and why.

## 14. Metrics and Evaluation Review

§7.1 unchanged and complete. K-06's expression now has its integrality rule. No other metrics.

## 15. Traceability Review

§11 has one row per declared R/C/I/K/E id including C-07; deferred items are labeled in
their rows. §12's *Affects* columns name only declared ids and, spot-checked against each
decision's prose (D-08, D-12, D-14, D-15), name all of them. F-nnn references in the spec
point at the review reports as sources, correctly outside the id graph. `speccheck` on v0.5:
57 ids, 0 dangling, 0 stale, 0 notes.

## 16. Internal-Consistency Review

The v0.4/v0.5 edits introduced no drift this review could find: 20 ms, PCM_S16LE, 4 h,
$-60$ dBFS, and drop-oldest each appear consistently; the C-02/K-04, §3.1/§5.1, and
I-002/D-15 tensions found during the pass were resolved in-document (F-013..F-015). Diagram
edges match normative rows, including the added `close()` edge.

## 17. Architecture Review

Unchanged from v0.3: the adapter/capture/queue/history decomposition supports every
requirement, and D-13 sharpened the boundary further (all conversion inside the adapter).
No redesign warranted.

## 18. Implementation-Agent Readiness

**YES.** A strong coding agent can implement this specification without asking material
semantic questions. Residual choices (event-queue depth, ring-buffer layout, handle
representation) are legitimate implementation freedom with no conformance impact. The one
intentionally open item — the CommonGround profile — is scoped out of conformance by D-10
rather than left ambiguous.

## 19. Quality Scorecard

| Dimension | v0.3 | v0.5 |
| --------- | ---: | ---: |
| Scope clarity | 5 | 5 |
| Terminology | 3 | 5 |
| Requirement precision | 4 | 5 |
| Interface completeness | 3 | 5 |
| Visual-surface completeness | n/a | n/a |
| Data-contract completeness | 3 | 5 |
| State/lifecycle definition | 4 | 5 |
| Algorithm precision | 4 | 5 |
| Failure semantics | 4 | 5 |
| Edge-case coverage | 4 | 5 |
| Non-functional requirements | 4 | 4 |
| Security specification | 4 | 4 |
| Observability/provenance | 4 | 5 |
| Testability | 4 | 5 |
| Evaluation/metrics | 5 | 5 |
| Traceability | 4 | 5 |
| Internal consistency | 3 | 5 |
| Architecture consistency | 5 | 5 |
| Implementation readiness | 3 | 5 |

## 20. Remediation Plan

**P0: none. P1: none. P2: none.** All findings from both review cycles are resolved in the
document under review. The only future work item is un-deferring D-10 (CommonGround profile)
when the appliance hardware is settled — tracked in §12, not a spec defect.

## 21. Final Verdict

```text
Specification maturity:
Level 3

Implementation readiness:
READY

Primary blocker:
NONE

Most important improvement:
Define the CommonGround profile (D-10) when the appliance hardware is settled, un-deferring
O-2, R-09, and T-16.
```
