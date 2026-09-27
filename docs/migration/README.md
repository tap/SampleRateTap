# docs/migration — the monorepo migration's working set

Everything the migration gates need (MONOREPO_PLAN.md sections 5 and 6),
committed at step 0 and deleted at step 4 (`runs.md` may be kept).

| Path | What it is |
|---|---|
| `tips.txt` | S0 and R0, the step-0 tips every A/B gate rebuilds, with their commit counts |
| `rename.py` | the mechanical rename map (paths, namespaces, macros, targets, C ABI, banners) and gate **G14** (`check`) |
| `residual/<step>.txt` | reviewed allowlists of G14 residual hunks, one per gated step |
| `collect.py` | collectors for the snapshot gates (G1, G2, G6, G9, G10, G12) |
| `snapshot/` | the step-0 snapshot those collectors compare against |
| `allow.txt` | G1 allowlist: test rows a job may gain (never lose) |
| `runs.md` | provenance of the snapshot, and the run record of every gated SHA (G13) |
| `drafts/` | the audit's untested workflow and CMake sketches for step 1c |

## How the pieces fit

- **Snapshot gates** re-collect from the gated SHA and diff against
  `snapshot/`: `collect.py ctest-log job.log | diff - snapshot/g1/async-linux-gcc.txt`.
  Bridge test names carry RatioTap's `ratio.` prefix at step 0 and `bridge.`
  after step 3.4 (D16); collect the snapshot side with `--map` to compare
  across that rename. Hosted in the `migration-gates` workflow from 1c.
- **G14** rebuilds the tree a purely mechanical migration would have at a
  step from S0 and R0 and diffs it against HEAD:

      git clone https://github.com/tap/SampleRateTap old-async   # at S0
      git clone https://github.com/tap/RatioTap old-ratio        # at R0
      python3 docs/migration/rename.py check --through 1c

  Measured at step 0: the map applied through 3.8 to S0 and R0 builds with
  `-Werror` (GCC 13), passes 77/77 and 82/82 tests, prints output hashes
  identical to S0's and R0's (24 each) and the same four cross-validation
  lines, and leaves four G9 hits, all `srt_headers`, which 1c's glue
  replaces. A loosened cross-validation tolerance in that tree is reported
  as one residual hunk.
- **A/B gates** (G3, G4, G5, G7, G11) never read files here except
  `tips.txt`: they rebuild S0/R0 and the gated SHA in one job.
