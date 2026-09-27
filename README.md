# edge-audio

Normalized PCM/sample-clock capture, bounded audio history, and queue/drop contracts.

## Repository role

This is an **independently versioned component repository**. It owns one coherent kind of behavior/contract and should be released and tested on its own cadence.

It is not a directory that should be copied into product repositories. A product that needs an exact source revision may mount this repository as a Git submodule and commit the resulting gitlink revision.

## Contract dependencies

None at the repository-contract level.

Dependencies are on public/versioned contracts. Internal implementation details should not cross repository boundaries.

## Normative specification

See `SPEC.md`.
