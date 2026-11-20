# Spike X-0: ×4, ×8 and ×16 for `rational` (measurements only)

The first item of the ×4 / ×8 / ×16 plan (draft v6, section 8, item 1). A spike ships nothing
in the library: this directory holds the programs that measured, their outputs, and the
go/no-go verdicts below. Nothing here is part of any build CI runs: the host programs build
with `build.sh`, and the instruction-count workloads join a root configure only through
`icount/hook.cmake`, which no CI job passes.

**What was measured against:** SampleRateTap `main` 5181418 (`rational/` unchanged since
3528c88, the commit the plan cites) and DspTap 084f2f6 (`project()` version 0.2.0). Host:
x86-64, GCC 13.3, Python 3.13 with numpy 2.5. Targets: `arm-none-eabi-gcc` 13.2.1 and
QEMU 8.2.2 (M33 on `mps2-an505`, M55 on `mps3-an547`), Hexagon clang 19.1.5 and a
plugin-enabled `qemu-hexagon` 8.2.2, the counting plugin built from
`tools/qemu_insn_plugin/insn_count.c` against the `qemu-plugin.h` whose SHA-256 CI pins.

## The verdicts (plan section 8, item 1)

1. **Does the diff move more rows or chains than 3.3 and X4 state? No.** The generator's own
   `diff`, band 4 admitted, moves **26 rows at `economy`, 12 within the families and 14 across**;
   **6 named chains retire** (the four ×8 chains, `down_3_down_8_down_2`, `up_2_up_8_up_3`) and
   **4 are added** (`down4_down2`, `up2_up4`, `down4_down6_down2`, `up2_up6_up4`). A chain per
   tier changes **47 rows (32 within, 15 across)** and adds **14 chains used only at
   `transparent`**; the C ABI then has **18 new constants, 6 retired, 40 live, `CHAIN_END` 46**.
   Every count in 3.3, X4 and X7 is confirmed exactly (`generator/diff.txt`). X-a2 and X-a3 keep
   their sizes, for today's designer (but see verdict 2's note on X4).
2. **With β for A + 2 … A + 4, do three half-bands beat ↓4 · ↓2 at ×8 by more than R3's 10 %?
   No.** At `economy`, the tier the generator selects the 70 dB chain at, ↓4 · ↓2 ties three
   half-bands at A + 1, A + 2 and A + 3 (85, 85 and 83 MACs per output down, the same ratio up) and is 10.1 %
   below them at A + 4 and with β searched per entry (71 against 79). R3 gives a tie to the
   fewer stages, and ↓4 · ↓2 keeps a latency edge at every rule (1.25 output frames under each
   re-tuned β, 2.5 today; `generator/beta.txt`). **X3 stands, its reason restated:** under today's designer it is
   19 % below the matrix's ×8 chain (85 against 105); under any re-tuned β the gain over the
   best chain without band 4 is 0 to 10 %, and ↓4 · ↓2 wins on R3's fewer-stages rule and on
   latency, not on a 19–24 % MAC gap. At `super_economy` alone, β for A + 1 to A + 3 puts ↓4 · ↓2
   11.0–11.3 % above three half-bands, past R3's 10 %; that column does not select the row (X4).
3. **Does ↓4 · ↓2 count no fewer instructions than ↓3 · 3/4 · ↓2 on two of the three targets?
   No: it counts fewer on all three, in both formats, both ways.** Per output frame, the
   matrix's 48-family chain costs 1.22–1.31 times X3's chain down and 1.17–1.30 times up, on
   the M33, M55 and Hexagon, float and Q15 (`icount/summary.txt`). **X3 stands.** One result
   the plan does not state: the **44.1 family's** 1/8 chain today, 3/8 · ↓3, counts **fewer**
   instructions than ↓4 · ↓2 in Q15 on all three targets (0.963, 0.809 and 0.992 of it on the
   M33, M55 and Hexagon), despite 124 MACs against 85 (section 3 below).
4. **If a designer change wins, does it live in DspTap? Partly.** A re-tuned β cuts MACs across
   the matrix (the mean over the 182 rows falls 3.8 % to 10.3 %, decision 6 kept), so it wins
   on MACs, but it is a whole-engine re-pin (48 to 174 of the 200 relaxation pins move, and
   every Q15 / Q31 table with them). **A fixed offset (β for A + x) needs no DspTap change:**
   `search_nyquist_m(band, p, A + x, 1 − x, …)` designs with `kaiser_beta(A + x)` and accepts
   at −(A + 1) dB, and it returns the rule's pin at all 300 keys for x = 0 … 4
   (`generator/margin_trick.txt`), so it could ride 0.8.0 on DspTap 0.2.0. **β searched per
   entry needs DspTap** (a β argument, or a joint search, in `search_nyquist_m`), so it waits
   for 0.3.0. Either way it is its own numeric change, not X3's; the maintainer decides
   whether it goes before X4 (see the note below verdict 2).

**A note on X4 (not on the list, but it moves X-a3's size).** The per-tier split is largely
today's designer too. Under β for A + 2, A + 3, A + 4 and searched, the ×8 chain at
`transparent` is ↓4 · ↓2 / ↑2 · ↑4 (the cheapest at A + 2 to A + 4; within R3's 10 % when
searched), and the ×16 chain at `transparent` is ↓8 · ↓2 / ↑2 · ↑8 under R3, the plain aliases'
own chains (at A + 1 they stay three half-bands and ↓4 · ↓2 · ↓2), so the four `_transparent`
aliases would name the same chains as `down_by8` / `down_by16`. The rows whose transparent
chain differs fall from 47 to 44, 24, 15, 25 and 16, and the `transparent`-only chains from 14
to 12, 9, 1, 7 and 5 (A + 1, A + 2, A + 3, A + 4, searched; `generator/beta.txt`). If the designer is re-tuned, X4 is best sized after it.

## Contradictions and corrections to the plan

- **X6 / X3, "the rows it fixes as a side effect":** true in MACs (124 → 85 down, 14.50 →
  10.62 up), not in Q15 instructions down. 3/8 · ↓3 counts 0.96 (M33), 0.81 (M55) and 0.99
  (Hexagon) of ↓4 · ↓2 in Q15; in float, and up in either format, X3's chain counts fewer
  everywhere. Per MAC, ↓4 · ↓2 costs about 9 instructions on the M55 in Q15 and 3/8 · ↓3 about 5
  (`icount/summary.txt`, "per MAC"). The likely cause, not measured stage by stage: the ↓4
  stage's branches are short (m = 5 at divisor 2, 10 taps each), while the 3/8 stage runs
  rows of about 27 taps, so the Q15 decimator's per-branch overhead dominates.
- **3.1's β table** is confirmed (A: 85 / 113 / 53; A + 2: 85 / 85 / 49; A + 3: 83 / 83 / 47;
  A + 4: 71 / 79 / 43), with two rows added: A + 1 gives 85 / 85 / 49, and β searched per entry
  71 / 79 / 43.
- **3.1 and X3, "a change in DspTap could not ride 0.8.0":** a fixed offset need not be in
  DspTap (verdict 4). Only a per-entry β does.
- **6.1's float FFT figures** (0.088 µs per sample at 32768 points, 0.0089 µs after ×8) did not
  reproduce: `programs/custom.cpp`'s committed output gives 387.8 µs and 37.9 µs per pair
  (0.047 and 0.0046 µs per sample; an earlier run here gave 565.6 and 57.7), and
  `fft/fft_cost.cpp` 0.0417 and 0.0037 µs. The host was shared with other sessions' jobs (a
  load average of 5 to 13 on 4 cores), so all of these are indicative; the ratio, about 10 to
  11× less after ×8, is the robust figure (the plan says 9.8×).
- **X4 sizing:** see the note above; 47 rows and 14 chains hold for today's designer only.

## Not measured

- Passband ripple and the Q15 / Q31 attained stopbands under a re-tuned β (the pin criterion is
  the stopband alone).
- Instruction counts at ×4 and ×16, at any profile but `economy`, of the worst single call
  (`callmax_*`), and the size and stack ceilings of the new chains; and the double format on
  target. X3's question is ×8 at `economy`, and X-a2 adds its own workloads and ceilings.
- The FFT on a target (the M55's CMSIS-Helium float backend): host wall clock only.

## Layout and how to run

```sh
cd rational/tools/spikes/x4x8
./build.sh                                   # host programs into build/ (gitignored)
PYTHON=/path/to/python-with-numpy ./run.sh   # rewrites every committed .txt but icount/ and fft/ (15-30 min)
python3 icount/run_icount.py --target m33 --plugin build/libinsncount.so --check-ratchet
build/fft_cost                               # fft/fft_cost.txt (wall clock)
```

`run_icount.py` configures the repository root with the target's toolchain file, Release and
the ratchet's options plus `icount/hook.cmake`, builds into `build/icount-<target>`, and counts
under QEMU through `scripts/icount.py`'s own `qemu_cmd` (Hexagon from its fixed path with an
empty environment). `--check-ratchet` also runs `scripts/icount.py --engine rational` on the
same build against `bench/baselines.json`: all 18 scenarios pass on all three targets in both
builds, and in the exceptions build every one is exact (+0) but the M55's `ratio23_q15_eco`,
whose baseline is the midpoint of its two builds (+2.55 % here, −2.55 % without exceptions,
as `rational/CLAUDE.md` records). So these counts come from the ratchet's toolchain and flags. The
plugin is `gcc -shared -fPIC $(pkg-config --cflags glib-2.0) -I<dir of qemu-plugin.h> -o
build/libinsncount.so tools/qemu_insn_plugin/insn_count.c`; Hexagon needs the Hexagon clang
and the plugin-enabled `qemu-hexagon` first on `PATH`.

| path | what it measures | plan section |
|---|---|---|
| `generator/x0_matrix.py` | the shipping `matrix.py`, imported read-only, driven: band 4 admitted (`diff`), a chain per tier (`pertier`, `rows`), the `by` search (`by`), the designer's β (`beta`) | 3.3, X3, X4, X7, 5, dsp-1 |
| `generator/pins_beta.cpp` | the pins under a β rule, with DspTap's own designer and the 16384-point criterion | dsp-1 |
| `generator/margin_trick.cpp` | whether `search_nyquist_m`'s A and margin arguments reach a β offset on DspTap 0.2.0 | X3, verdict 4 |
| `icount/` | instructions per output frame of the ×8 chains, M33 / M55 / Hexagon, float / Q15 | X3, verdict 3 |
| `fft/fft_cost.cpp` | `real_fft` (double) and `real_fft32` forward + inverse per sample at overlap 4 | 6.1 |
| `programs/` | the plan's `x4x8/` programs and the ÷16 demo, as run for v4–v6 | 3.1–3.4, 6.1–6.6 |
| `audit/` | the audit programs behind dsp-1, red-1, red-2 and the plan's v6 corrections | 3.1, 3.3, 6.2, 6.3 |

## 1. The generator (X3, X4, X7, the `by` mode)

`generator/x0_matrix.py diff` (`diff.txt`) runs `matrix.py`'s own `diff()` against
`rational/PLAN.md`'s tables, today (0 rows) and with band 4 admitted (26 rows), then the
per-tier comparisons:

| comparison | rows | within | across |
|---|---:|---:|---:|
| X3 at `economy` against today | 26 | 12 | 14 |
| X3 + X4: the `transparent` chain differs from the `economy` chain | 47 | 32 | 15 |
| X3 + X4 at `transparent` against today's tables (which run the `economy` chain there) | 45 | 32 | 13 |
| X4 alone (decision 6 kept): `transparent` differs from `economy` | 41 | 32 | 9 |

- X3 moves four within-family ratios: 1/8 and 8/1 (five rows each, both families), 1/48 and
  48/1 (↓3 · ↓8 · ↓2 373 → ↓4 · ↓6 · ↓2 353; ↑2 · ↑8 · ↑3 7.77 → ↑2 · ↑6 · ↑4 7.35).
- X4 moves fourteen ratios: 12/1, 24/1, 48/1, 8/3, 8/1, 16/1, 32/1, 3/8, 1/12, 1/8, 1/24, 1/16,
  1/48 and 1/32.
- The within-family chains (the C ABI's constants are exactly these): 28 today (20 named
  multi-stage), 26 under X3, 40 under X3 + X4 (30 multi-stage). The 14 `transparent`-only
  chains are 3/8 and 8/3 as one stage, ↑2 · ↑2 · ↑2, ↑2 · ↑2 · ↑3, ↑2 · ↑2 · ↑4, ↑2 · ↑3 · ↑4,
  ↑4 · ↑6 · ↑2, 8/3 · ↑3 · ↑4, ↓2 · ↓2 · ↓2, ↓3 · ↓2 · ↓2, ↓4 · ↓2 · ↓2, ↓4 · ↓3 · ↓2,
  ↓4 · ↓3 · 3/8 and ↓2 · ↓6 · ↓4.
- X7: the retired values are 10, 11, 16, 21, 22 and 27; the 18 new constants take 28 to 45 (an
  illustrative order is in `diff.txt`); `CHAIN_END` is 46. Every row X3 moves runs a 4th-band stage: 26 at
  `economy` (12 within, 14 across); under X4, 22 rows run one at `transparent` (14 within, 8
  across). The C ABI builds the cross-family ones stage by stage, so `create_stage` must
  accept (4, 1) and (1, 4) for `matrix.ipynb` to reach them (finding red-3).

`pertier.txt` is X4's prototype `md` (an `economy` and a `transparent` chain column) and its
per-tier `diff`; `rows_pertier.txt` is a prototype `ROW` format with a second chain and divisor
tuple per row, in X0's spelling (a text file, not a test input). `by.txt` is the `by` mode:
section 3.1's space (every chain of 1–3 stages, 1–4 at ×16, from the vocabulary plus band 4,
every intermediate rate between the endpoints: 29, 52 and 462 chains for ×4, ×8 and ×16 each
way), the minimum per (ratio, profile), R3's choice, and the named competitors. It reproduces
section 3.1's tables, including the ×16 difference R3 keeps (↓8 · ↓2 165 against
↓2 · ↓4 · ↓2 157 at `economy`) and the `super_economy` ×4 tie (49 and 49).

## 2. The designer's Kaiser β (finding dsp-1)

Rules: β = `kaiser_beta(A + x)` for every design (x = 0 is today's), and β searched per
entry: the smallest m for which some β = `kaiser_beta(A + x)`, x on −5, −4.75 … +25 dB, meets
−(A + 1) dB on the 16384-point grid, pinning the β with the deepest stopband at that m.
`pins_beta.cpp` computes the pins with DspTap's designer; at x = 0 it agrees with `matrix.py`'s
numpy search at all 300 keys the generator designs. MACs per output frame per channel at
`economy`, latency in output frames in brackets; up is down over the factor (↑2 · ↑4 is 85 / 8).

| designer's β | pins moved (of 200) | ×4: ↓2 · ↓2 | ×8: ↓4 · ↓2 | ×8: ↓2 · ↓2 · ↓2 | ×8 matrix chain, decision 6 kept | X3's ×8 gain over it | ×16 with X3 (R3) |
|---|---:|---:|---|---|---|---:|---|
| A (today) | 0 | 53 | 85 (12.88) | 113 (15.38) | ↓3 · 3/4 · ↓2, 105 | −19.0 % | ↓8 · ↓2, 165 |
| A + 1 | 48 | 49 | 85 (12.88) | 85 (14.12) | ↓2 · ↓2 · ↓2, 85 | 0 (R3: fewer stages) | ↓8 · ↓2, 165 |
| A + 2 | 63 | 49 | 85 (12.88) | 85 (14.12) | ↓2 · ↓2 · ↓2, 85 | 0 (R3: fewer stages) | ↓8 · ↓2, 165 |
| A + 3 | 102 | 47 | 83 (11.88) | 83 (13.12) | ↓2 · ↓2 · ↓2, 83 | 0 (R3: fewer stages) | ↓8 · ↓2, 163 |
| A + 4 | 127 | 43 | 71 (11.38) | 79 (12.62) | ↓2 · ↓2 · ↓2, 79 | −10.1 % | ↓2 · ↓4 · ↓2, 143 |
| searched per entry | 174 | 43 | 71 (11.38) | 79 (12.62) | ↓2 · ↓2 · ↓2, 79 | −10.1 % | ↓8 · ↓2, 135 |

The `economy` pins (m for the bands 2, 3, 4, 6, 8 at each design divisor; `beta.txt` has
all four profiles and the β the searched rule pins):

| divisor | A (today) | A + 1 | A + 2 | A + 3 | A + 4 | searched |
|---|---|---|---|---|---|---|
| 147/160 | 15 14 14 14 14 | 15 14 14 14 14 | 15 14 13 13 13 | 13 13 13 13 13 | 13 13 13 13 13 | 13 13 13 13 13 |
| 1 | 11 11 11 12 12 | 11 11 11 11 11 | 11 11 11 11 9 | 10 10 10 10 10 | 10 10 10 10 10 | 10 10 10 10 9 |
| 2 | 7 5 5 5 5 | 6 5 5 5 5 | 6 5 5 5 5 | 6 5 5 5 5 | 5 4 4 5 5 | 5 4 4 4 4 |
| 3 | 7 5 5 5 5 | 4 5 5 5 5 | 4 5 5 5 5 | 4 5 5 5 5 | 4 4 4 5 5 | 4 4 4 4 4 |
| 4 | 7 5 5 5 5 | 4 5 5 5 5 | 4 5 5 5 5 | 4 5 5 5 5 | 4 4 4 5 5 | 4 4 4 3 3 |
| 6 | 4 5 4 4 4 | 4 3 4 4 4 | 4 3 4 4 4 | 4 3 3 4 4 | 4 3 3 4 4 | 3 3 3 3 3 |
| 8 | 4 4 4 4 4 | 4 3 4 4 4 | 4 3 4 4 4 | 4 3 3 4 4 | 4 3 3 4 4 | 3 3 3 3 3 |
| 9 | 4 4 4 4 4 | 4 3 4 4 4 | 4 3 4 4 4 | 4 3 3 4 4 | 4 3 3 3 3 | 3 3 3 3 3 |
| 12 | 3 3 3 4 4 | 3 3 3 4 4 | 3 3 3 3 4 | 3 3 3 3 3 | 4 3 3 3 3 | 3 3 3 3 3 |
| 16 | 3 3 3 4 4 | 3 3 3 4 4 | 3 3 3 3 4 | 3 3 3 3 3 | 3 3 3 3 3 | 3 3 3 3 3 |

A larger offset is not always shorter: at divisor 12 the half-band is m = 3 at A + 3 and
m = 4 at A + 4, the same Kaiser-fit floor finding dsp-1 describes, moved.

The whole matrix under each rule (`beta.txt`; "rows" are of the 182):

| designer's β | pins moved: all (`economy`) | decision 6 kept: rows whose chain moves (within) | rows whose `economy` MAC/out moves | X3: rows moved against today's tables (within) | X3 + X4: per-tier rows (within) | `transparent`-only chains | mean `economy` MAC/out, decision 6 kept |
|---|---|---|---:|---|---|---:|---|
| A (today) | 0 (0) | 0 | 0 | 26 (12) | 47 (32) | 14 | 93.66 |
| A + 1 | 48 (8) | 20 (14) | 80 | 27 (17) | 44 (27) | 12 | 90.10 (−3.8 %) |
| A + 2 | 63 (13) | 22 (16) | 82 | 32 (20) | 24 (13) | 9 | 90.09 (−3.8 %) |
| A + 3 | 102 (23) | 20 (14) | 176 | 30 (18) | 15 (3) | 1 | 88.30 (−5.7 %) |
| A + 4 | 127 (32) | 26 (20) | 176 | 46 (30) | 25 (17) | 7 | 86.37 (−7.8 %) |
| searched per entry | 174 (44) | 24 (18) | 176 | 32 (18) | 16 (9) | 5 | 83.99 (−10.3 %) |

"Pins" are design.h's 200 relaxation entries (4 profiles × 10 divisors × 5 bands; `economy`'s
50 in brackets). The mean includes `bridge`'s cost in the cross rows, which no rule changes.

**Where a change would land.** The β is chosen in two places: `rational`'s
`design.h:301` (`design_nyquist(h, band, kaiser_beta(p.stopband_atten_db))`, the shipping
design) and DspTap's `nyquist.h:214` (`search_nyquist_m`'s `kaiser_beta(stopband_atten_db)`,
which `design.h:263` calls for an unpinned profile and `test_design.cpp:302` calls to verify
every pin). The generator and the scipy vectors carry their own copies: `matrix.py:84` and
`:123`, and `tools/reference/make_reference_vectors.py:44` (used at `:157` and `:182`).
`test_design.cpp:138` designs at `kaiser_beta(A)` to check each pin's minimality. With them
move: `design.h`'s profile pins and relaxation tables, `tests/coverage/matrix_rows.h` and the
plan's tables, `tests/reference/reference_vectors.h`, every bit-pinned Q15 / Q31 table in
`test_fixed_point.cpp` and the floors PLAN.md section 6 states, the ratchet rows whose pins
move (`economy`'s divisor-1 half-band goes from m = 11 to 10 at A + 3), the executed notebooks
(`design_spike.ipynb`, `matrix.ipynb`) and the book's tables. `bridge` and `async` design with
their own code and are untouched.

- **A fixed offset on DspTap 0.2.0:** `design.h:301` takes `kaiser_beta(A + x)` and
  `design.h:263` calls `search_nyquist_m(band, p, A + x, k_design_margin_db − x, …)`.
  `margin_trick.txt` shows that call returns the rule's m at all 300 keys for x = 0 to 4 (a
  negative margin is accepted: the function only compares). So it could ride SampleRateTap
  0.8.0, as a numeric change of its own; the clean form, a β argument in DspTap, can follow
  in 0.3.0 and retire the workaround.
- **β per entry:** each pin carries its β beside m (`relaxed_pins` and `profile` gain one),
  and an unpinned profile needs a joint (m, β) search, which `search_nyquist_m` cannot do on
  0.2.0 without `rational` looping over it, design math the substrate discipline puts in
  DspTap. It waits for DspTap 0.3.0.
- **Not measured:** passband ripple and the Q15 / Q31 attained stopbands under a new β. The
  pin criterion is the stopband alone; a designer change would re-measure both (PLAN.md 2.3
  and section 6).

## 3. Instruction counts (X3)

`icount/icount_x8.cpp`: the ratchet's workload shape (`bench/icount/icount_main.cpp`: two
channels, 32-frame blocks, a precomputed 997 Hz sine cycled every 12000 frames) through each
×8 chain at `economy`, built at 1000 and 3000 blocks; the figure is the difference of the two
counts over the difference of their output frames, so construction, the fixture and start-up
cancel. Instructions per output frame (two channels), exceptions on; ratio to X3's chain in
brackets (`icount/summary.txt`, which also gives the no-exceptions build and instructions per
MAC):

| chain | MAC/out | M33 float | M33 Q15 | M55 float | M55 Q15 | Hexagon float | Hexagon Q15 |
|---|---:|---:|---:|---:|---:|---:|---:|
| ↓4 · ↓2 (X3) | 85 | 26818 | 1823 | 2026 | 1523 | 5044 | 865 |
| ↓3 · 3/4 · ↓2 (48 matrix) | 105 | 32776 (1.22) | 2379 (1.31) | 2523 (1.25) | 1970 (1.29) | 6268 (1.24) | 1086 (1.26) |
| ↓2 · ↓2 · ↓2 | 113 | 36029 (1.34) | 2601 (1.43) | 2820 (1.39) | 2205 (1.45) | 6773 (1.34) | 1212 (1.40) |
| 3/8 · ↓3 (44.1 matrix) | 124 | 37764 (1.41) | 1755 (0.96) | 2201 (1.09) | 1232 (0.81) | 7031 (1.39) | 858 (0.99) |
| ↑2 · ↑4 (X3) | 10.62 | 3292 | 201.6 | 202.1 | 168.6 | 620.0 | 96.5 |
| ↑2 · 4/3 · ↑3 (48 matrix) | 12.46 | 3860 (1.17) | 254.1 (1.26) | 252.7 (1.25) | 218.8 (1.30) | 734.4 (1.18) | 124.8 (1.29) |
| ↑2 · ↑2 · ↑2 | 14.12 | 4384 (1.33) | 282.4 (1.40) | 287.2 (1.42) | 236.0 (1.40) | 835.2 (1.35) | 144.2 (1.50) |
| ↑3 · 8/3 (44.1 matrix) | 14.50 | 4497 (1.37) | 242.6 (1.20) | 259.3 (1.28) | 195.2 (1.16) | 832.7 (1.34) | 120.0 (1.24) |

- The M33's float figures are about 155 instructions per MAC because the float datapath
  accumulates in double (`sample_traits.h`), soft-float on that core; the ratchet's float
  baselines carry the same cost.
- The no-exceptions build agrees within 0.3 % on every row but the M55's Q15 3/8 · ↓3
  (1194.6 against 1232.4, 3 %), the kind of register-allocation split the ratchet already
  records for `ratio23_q15_eco`; no comparison changes side.

## 4. The double FFT's cost (6.1)

`fft/fft_cost.cpp`, host wall clock, **indicative**: the host had 4 cores and another
session's job was using about 2.5 of them, so each size is the fastest of 15 runs (the
medians are in `fft/fft_cost.txt`). Forward plus inverse per hop at overlap 4, µs per
full-rate sample:

| bins at 48 kHz | full band, double | after decimating, 4096 points, double | ratio | float, the same two |
|---|---|---|---:|---|
| 2.93 Hz | 0.0388 (16384 points) | 0.0073 (×4) | 5.3× less | 0.0360 / 0.0074 |
| 1.46 Hz | 0.0417 (32768 points) | 0.0037 (×8) | 11× less | 0.0417 / 0.0037 |
| 0.73 Hz | 0.0462 (65536 points) | 0.0018 (×16) | 25× less | 0.0461 / 0.0018 |

On this host the double and float profiles cost the same (both run the srdif engine); the
stages' 20–27 MACs per sample (section 3.4) come on top.

## 5. The plan's scratch programs

Each is the scratch program as run for the plan, with this repository's SPDX banner and a
provenance line added and the tree's `.clang-format` applied; its output is beside it as
`<name>.txt`. `brute.cpp` and `x16.cpp` are generated (`programs/gen_brute.py`,
`programs/gen16.py`), byte for byte the sources that were run.

| program | backs | reproduces |
|---|---|---|
| `programs/brute` (gen_brute.py) | 3.1's ×4 / ×8 tables | yes (`by.txt` agrees) |
| `programs/x16` (gen16.py) | 3.1's ×16 tables | yes |
| `programs/costs` | 3.1, 3.4's latencies and MACs | yes |
| `programs/family` | 3.2 (a chain costs the same in either family) | yes |
| `programs/sixteen` | 3.3's ×16 / ×24 / ×32 rows | yes |
| `programs/split` | 6.2 (usable bins, transition-band aliasing) | yes |
| `programs/custom` | 6.2's wider-band costs; 6.1's float FFT timing | MACs yes; the timing differs (above) |
| `programs/dbl` | 6.1 (`basic_chain<double, …>`: zeros in, exact zeros out) | yes |
| `programs/low_band_fft_48k_div16` | 6.6, the ÷16 demo | yes: T = 414, 20.62 MACs per sample, 205 Hz −64.05 dB, 200 Hz and 5 kHz 0.00 dB |
| `programs/x4_check`, `eight_check`, `name_check`, `down8_process_up8` | 3.1, 3.4, X0 | yes |
| `audit/betarule`, `betasearch`, `hb`, `pins`, `relax` | dsp-1, code-19 (3.1's β table and m = 7) | yes |
| `audit/recheck.py`, `pertier.py`, `rows48.py` | red-1, red-2 (3.3, X4) | yes (`generator/diff.txt` agrees) |
| `audit/more32`, `tie`, `band4`, `custom4` | dsp-16, code-6, 3.1, 6.2's ×8 column | yes |
| `audit/removal`, `ripple`, `rt_v`, `trans`, `dip` | 6.3's removal floors, 6.2, 6.6's dip | yes |
| `audit/demo_var` | 6.6 with T + 1 (−31.46 dB) | yes (`demo_var_t_plus_1.txt`: `demo_var 205 1`) |
| `audit/q15band4` | X3: band 4's Q15 / Q31 attained stopband | yes |
