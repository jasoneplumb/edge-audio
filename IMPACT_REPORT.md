# Impact Report

SPEC.md · changed by `--changed` · depth unbounded

## 1. Changed

| ID | Line | Reason |
| --- | --- | --- |
| C-01 | 125 | changed |
| C-06 | 233 | changed |
| K-05 | 323 | changed |
| K-06 | 324 | changed |

## 2. Impact

| ID | Depth | Via |
| --- | --- | --- |
| R-01 | 1 | R-01 -depends_on-> C-01 |
| R-02 | 1 | R-02 -depends_on-> C-01 |
| R-03 | 1 | R-03 -depends_on-> C-01 |
| R-11 | 1 | R-11 -depends_on-> C-06 |
| C-07 | 1 | C-07 -depends_on-> C-01 |
| E-04 | 1 | E-04 -depends_on-> K-06 |
| R-09 | 2 | R-09 -depends_on-> C-07 |
| C-04 | 2 | C-04 -depends_on-> R-03 |
| C-05 | 2 | C-05 -depends_on-> R-02 |
| R-05 | 3 | R-05 -depends_on-> C-04 |
| R-06 | 3 | R-06 -depends_on-> C-05 |
| R-10 | 3 | R-10 -depends_on-> C-04 |
| C-02 | 3 | C-02 -depends_on-> C-04 |
| I-006 | 4 | I-006 -depends_on-> R-06 |

## 3. Re-verify

| T id | Verifies |
| --- | --- |
| T-01 | R-01, R-02, C-01, K-06 |
| T-02 | C-04 |
| T-03 | R-05 |
| T-06 | R-03, K-05 |
| T-07 | R-11, K-06, E-04 |
| T-08 | R-11 |
| T-11 | R-10 |
| T-13 | R-06, I-006 |
| T-14 | R-10 |
| T-15 | R-05 |
| T-16 | R-09, C-07 |
| T-18 | R-11, C-07 |

## 4. Re-cite

Not scanned.

## 5. Notes

None.
