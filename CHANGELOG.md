# Changelog

What changed in each release of the `tap::sr` family. The three engines, `async`,
`bridge` and `rational`, carry one version (`TAP_SR_VERSION_*`, D13), tagged `vX.Y.Z`.
Before 1.0 a minor release may break the C++ API and the C ABI, and a rename is clean:
the old spelling is removed, never aliased (D7), so it fails to compile, and
`scripts/retired_words.txt` lists it. This file starts at 0.8.0; the releases before it
are back-filled with the 1.0 release engineering (`docs/ROADMAP.md`, C3).

## 0.8.0

`rational`'s ×4, ×8 and ×16 plan (`docs/X4_X8_PLAN.md`, PRs #107, #109, #110, #111 and
the one that tags this release): the vocabulary respelled, the designer re-tuned, the
4th-band stage admitted, a chain per attenuation tier, and the chains by 4, 8 and 16
named by intent. `async` and `bridge` change only their version. DspTap stays at its
0.2.0 floor, pinned at `v0.2.0` (`TAP_SR_DSPTAP_MINIMUM` unchanged).

### What breaks, and what to do

1. **The spelling (X0; #107).** A factor joins its word, a mixed ratio reads as a
   fraction and an underscore separates a chain's stages: `down_8` is `down8`,
   `ratio_3_4` is `ratio3over4`, `down_8_down_2` is `down8_down2`. The C ABI's
   constants follow with every value unchanged: `TAP_SR_RATIONAL_DOWN_8_DOWN_2` is
   `TAP_SR_RATIONAL_DOWN8_DOWN2`. Respell with the map below; nothing numeric moved.
2. **The designer's Kaiser β (X17; #109).** Every design takes its β for A + 4 dB and
   is accepted at A + 1 dB as before, so 127 of the 200 relaxation pins, every Q15 and
   Q31 table, the scipy vectors and the coverage matrix's numbers moved: every
   `rational` output changes at every profile, within each profile's promise. Every
   Q15 stage of the vocabulary attains the 70 dB tier at its own rate, and every
   relaxed design a chain runs attains it too. Nothing to respell; re-pin anything
   that pinned `rational`'s output bits.
3. **The 4th-band stage (decision 6 as amended, X3; #110).** `up4` and `down4` join the
   vocabulary, and the coverage matrix chooses them wherever they cost the fewest MACs:
   46 rows moved at `economy`, and the 44.1 kHz family's 8/1 and 1/8 now run the
   48 kHz family's chains. Ten named chains are retired (the map below); their C values
   are never reused and `tap_sr_rational_create` refuses them.
   `TAP_SR_RATIONAL_CHAIN_COUNT` is retired for `TAP_SR_RATIONAL_CHAIN_END`, one past
   the largest value ever assigned, since the live values have gaps; a loop over the
   chains iterates the values the header lists. `tap_sr_rational_create_stage` accepts
   4/1 and 1/4.
4. **A chain per attenuation tier (X4; #111).** The matrix selects the 70 dB tier's
   chain at `economy` (run at `super_economy`, `economy` and `balanced`) and the
   120 dB tier's at `transparent`; they differ in 25 rows. Three chains join for the
   120 dB tier (`up2_ratio3over2`, `up2_up6_up4`, `down4_down6_down2`, C values 40 to
   42), and `up2_up6`, `up2_up8`, `down6_down2` and `down8_down2` (12, 13, 23, 24) now
   serve only it. A caller who chose a chain from the matrix at `transparent` reads
   that tier's column.
5. **The chains by 4, 8 and 16, named by intent (X1, X2; X-b).** Additions, not breaks:
   `down_by4`, `down_by8`, `down_by16`, `up_by4`, `up_by8` and `up_by16` are the 70 dB
   tier's chains (`down2_down2`, `down4_down2`, `down2_down4_down2` and their mirrors),
   and `down_by16_transparent` / `up_by16_transparent` the 120 dB tier's (`down8_down2`,
   `up2_up8`), by 16 alone, the one ratio whose tiers differ. Each is a plain alias of
   the matrix's chain, in both families, never a lookup (D12); the suffix names the
   tier a chain is cheapest at, not a profile. The C ABI spells them
   `TAP_SR_RATIONAL_DOWN_BY4` … `TAP_SR_RATIONAL_UP_BY16_TRANSPARENT`, each `#define`d
   as its chain's constant: no new value and no new exported symbol. `down8` beside
   `down_by8` is one stage, lower latency at twice the MACs.

### Also new

- `rational/examples/low_band_fft.cpp`: a 4096-point FFT with 0.73 Hz bins in the low
  band of a 48 kHz signal through `down_by16` / `up_by16`, the change alone
  interpolated back and added to the input delayed by the round trip's exact latency
  (378 samples at `economy`); `rational.LowBand.*` pins its claims at ×4, ×8 and ×16.
- `rational/tools/coverage/matrix.py by`: the chains by 4, 8 and 16 against every
  chain of their ratio up to three stages (four by 16), pinned by
  `rational.Chain.ByAliasesAreTheCheapestWithinR3`.
- The family plan's D19: `rational`'s charter names ratios, not a list of rates, so
  48 → 3 kHz (the ratio 1/16) is in it; the fourteen rates are the coverage matrix's
  pinned set, not a limit.

### The name map (0.7.0 → 0.8.0)

The ratios (`ratio.h`):

| 0.7.0 | 0.8.0 |
|---|---|
| `up_2`, `down_2`, `up_3`, `down_3` | `up2`, `down2`, `up3`, `down3` |
| `up_6`, `down_6`, `up_8`, `down_8` | `up6`, `down6`, `up8`, `down8` |
| `ratio_3_2`, `ratio_2_3` | `ratio3over2`, `ratio2over3` |
| `ratio_4_3`, `ratio_3_4` | `ratio4over3`, `ratio3over4` |
| `ratio_8_3`, `ratio_3_8` | `ratio8over3`, `ratio3over8` |
| (none) | `up4`, `down4` (one 4th-band stage each) |

The named chains (`chain.h`) and their C ABI constants (`TAP_SR_RATIONAL_` and the
name in capitals; every value is the one 0.7.0 gave it):

| C value | 0.7.0 | 0.8.0 |
|---:|---|---|
| 0–7 | `TAP_SR_RATIONAL_UP_2` … `TAP_SR_RATIONAL_RATIO_3_4` (the one-stage chains) | `TAP_SR_RATIONAL_UP2` … `TAP_SR_RATIONAL_RATIO3OVER4` |
| 8 | `up_2_up_2` | `up2_up2` (`up_by4`) |
| 9 | `up_2_up_3` | `up2_up3` |
| 10 | `up_2_ratio_4_3_up_3` | retired: 8/1 is `up2_up4` (28) |
| 11 | `up_3_ratio_8_3` | retired: the 44.1 family's 8/1 is `up2_up4` (28) |
| 12 | `up_2_up_6` | `up2_up6`, 12/1 at the 120 dB tier (`up3_up4`, 29, at 70 dB) |
| 13 | `up_2_up_8` | `up2_up8`, 16/1 at the 120 dB tier (`up_by16_transparent`; `up2_up4_up2`, 30, at 70 dB) |
| 14 | `up_2_up_6_up_2` | retired: 24/1 is `up2_up3_up4` (31) |
| 15 | `up_2_up_8_up_2` | retired: 32/1 is `up2_up4_up4` (32) |
| 16 | `up_2_up_8_up_3` | retired: 48/1 is `up3_up4_up4` (33) and, at 120 dB, `up2_up6_up4` (41) |
| 17 | `up_2_ratio_4_3` | `up2_ratio4over3` |
| 18 | `ratio_3_4_down_2` | `ratio3over4_down2` |
| 19 | `down_2_down_2` | `down2_down2` (`down_by4`) |
| 20 | `down_3_down_2` | `down3_down2` |
| 21 | `down_3_ratio_3_4_down_2` | retired: 1/8 is `down4_down2` (34) |
| 22 | `ratio_3_8_down_3` | retired: the 44.1 family's 1/8 is `down4_down2` (34) |
| 23 | `down_6_down_2` | `down6_down2`, 1/12 at the 120 dB tier (`down4_down3`, 35, at 70 dB) |
| 24 | `down_8_down_2` | `down8_down2`, 1/16 at the 120 dB tier (`down_by16_transparent`; `down2_down4_down2`, 36, at 70 dB) |
| 25 | `down_2_down_6_down_2` | retired: 1/24 is `down4_down3_down2` (37) |
| 26 | `down_2_down_8_down_2` | retired: 1/32 is `down4_down4_down2` (38) |
| 27 | `down_3_down_8_down_2` | retired: 1/48 is `down4_down4_down3` (39) and, at 120 dB, `down4_down6_down2` (42) |
| 28 | `TAP_SR_RATIONAL_CHAIN_COUNT` | retired: `TAP_SR_RATIONAL_CHAIN_END` (43) bounds the values |

New in 0.8.0: `up2_up4` (28, `up_by8`), `up3_up4` (29), `up2_up4_up2` (30, `up_by16`),
`up2_up3_up4` (31), `up2_up4_up4` (32), `up3_up4_up4` (33), `down4_down2` (34,
`down_by8`), `down4_down3` (35), `down2_down4_down2` (36, `down_by16`),
`down4_down3_down2` (37), `down4_down4_down2` (38), `down4_down4_down3` (39), and for
the 120 dB tier `up2_ratio3over2` (40), `up2_up6_up4` (41) and `down4_down6_down2` (42).
The ctypes binding (`rational/notebooks/tap_sr_rational_py.py`) names every chain and
alias through an explicit table of these values.
