# Gate runs

One row per gated SHA (MONOREPO_PLAN.md section 5): the run IDs of every
named workflow (G13), the runner image, and the Arm toolchain and QEMU
package versions the QEMU legs installed.

## Step 0 — the snapshot's evidence

The snapshot-class baselines under `snapshot/` come from these runs of the
two step-0 tips (`tips.txt`), all green, plus local builds of the same tips
where a job does not print what a gate needs.

| Tip | Workflow | Run | Result |
|---|---|---|---|
| S0 `5e2057f` | CI | 36348916668 | success, 15/15 jobs |
| S0 `5e2057f` | Tap House Style | 36348917285 | success |
| S0 `5e2057f` | book-pages | 36348916693 | success |
| R0 `8f19e8b` | CI | 36356088654 | success, 8/8 jobs |
| R0 `8f19e8b` | Tap House Style | 36356089470 | success |

QEMU legs (both tips): runner image `ubuntu-24.04` version 20260920.314.1,
`gcc-arm-none-eabi` 15:13.2.rel1-2, `qemu-system-arm` 1:8.2.2+ds-0ubuntu1.18.

| Snapshot | Source |
|---|---|
| `g1/<engine>-<job>.txt` | the ctest summary of each CI job's log (host jobs), or the job's uploaded `--output-log` (QEMU jobs); `collect.py ctest-log` |
| `g1/<engine>-labels.txt` | `ctest --show-only=json-v1` of a local Linux GCC Release build of each tip with the C ABI ON (77 and 82 tests; the C ABI adds none); `collect.py ctest-json` |
| `g2/<engine>-<target>.txt` | the `ctest-<target>` artifacts of the runs above; `collect.py gtest-runs` |
| `g6.txt` | the local R0 build's cross-validation test (GCC 13, x86-64), byte-identical to R0's P.4 measurement; `collect.py xval` |
| `g10/<engine>.txt` | `libsrt_capi.so` / `libratio_capi.so` from the local builds; `collect.py symbols` |
| `g12/<engine>.txt` | fresh full clones at S0/R0; `collect.py history` |
| `g9.txt` | the retired-identifier rule itself (applies from 3.7) |

G8 (book and API docs) has no file: its baseline is the S0 CI run's green
"Book build" job (mdBook with warnings as errors, image check). The Doxygen
check joins the book job at 1c.

## Gated SHAs

| Step | SHA | ci.yml | style.yml | migration-gates | ci-arm64 | compare | Image | Notes |
|---|---|---|---|---|---|---|---|---|
