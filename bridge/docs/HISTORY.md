# RatioTap history map

This engine was RatioTap (github.com/tap/RatioTap) until its history was
imported into this repository (monorepo migration step 1b). The import
rewrote every commit into `bridge/` with `git filter-repo`, so each SHA
changed; author, date and message did not. RatioTap's own issues, pull
requests and commit links cite the old SHAs, and this table maps them.

- Imported at R0 = RatioTap `main` `8f19e8b` (33 commits), rewritten tip
  `654659e`, joined by the merge commit that imports it.
- PR numbers are RatioTap's (`GET /repos/tap/RatioTap/commits/<sha>/pulls`,
  queried once at import). PRs were rebase-merged, so several commits share
  one PR; the root commit has none.
- `c0894cf` → `89c7eba` is RatioTap's reformat commit; it belongs in
  `.git-blame-ignore-revs` (added after the migration merges).

| Old SHA (RatioTap) | New SHA (here) | RatioTap PR | Date | Subject |
|---|---|---|---|---|
| `b3b088dd8e` | `37b3a34903` | — | 2026-07-23 | Add the v0.1 plan and the amended design-brief handoff |
| `f1d5013cff` | `a5a727ad6f` | #1 | 2026-07-23 | Add the M1 skeleton: build, substrate, style, CI |
| `34d97d0b59` | `bfb8eb66c3` | #2 | 2026-07-23 | Add M2: design spike, profiles, schedule, and phase tables |
| `b4aac51791` | `5a2ee3f626` | #2 | 2026-07-23 | Refresh the profile doc table to the post-normalization measurements |
| `c0894cfcd5` | `89c7eba254` | #2 | 2026-07-23 | Apply clang-format reflow the pre-commit hook produced post-staging |
| `a83d6d85ae` | `b8bad2e02f` | #3 | 2026-07-23 | Add M3: the streaming converter, pinned to scipy sample-for-sample |
| `81c09e8f18` | `d93c0d5373` | #3 | 2026-07-23 | Deduplicate the tests include-dir line |
| `c80a872405` | `29223e9da7` | #4 | 2026-07-23 | Add M4: fixed-point converters and their parity battery |
| `c3f19672cc` | `15061f4366` | #5 | 2026-07-23 | Add M5: the golden cross-validation against SampleRateTap |
| `4bec30cef0` | `4ee4c4cef5` | #6 | 2026-07-23 | Add M6: bluetooth_bridge, the C ABI, and the demo notebook — v0.1 |
| `01de4aae7e` | `9354dacae9` | #6 | 2026-07-23 | Ignore __pycache__ and drop a committed .pyc |
| `58e3e1fdea` | `454671a3c7` | #6 | 2026-07-23 | Mark the SampleRateTap dev headers as SYSTEM includes |
| `79ce9f1529` | `c89e674bc8` | #7 | 2026-07-24 | M7a: embedded CI matrix + instruction-count ratchet (M33/M55/Hexagon) |
| `179f52558c` | `240fb38dc1` | #8 | 2026-07-24 | M7b: superblock codegen — the process() hot path as a register walk |
| `580ea47c16` | `ebae70c5df` | #9 | 2026-07-24 | M7c: commit the trip counts — constexpr profiles, compile-time dot lengths |
| `d1045b82d0` | `0ae08adef4` | #10 | 2026-07-24 | M7d: symmetry storage halving — ceil(L/2) stored rows, mirrored dots |
| `e56f7041df` | `2138c05caa` | #10 | 2026-07-24 | Re-pin dsptap to the merged main commit |
| `9f871b5302` | `14e7cd65ee` | #11 | 2026-07-24 | v0.2: wrap the M7 codegen campaign |
| `f0f459a25c` | `8ff36d6a89` | #12 | 2026-07-27 | Add the shared pull-request template (taphouse sync) |
| `606587e57e` | `a808a0f13a` | #13 | 2026-07-28 | Bump the DspTap pin to 28a34a1 |
| `1d7ba8d2e6` | `bbcfb875b2` | #14 | 2026-07-28 | Compile the C ABI in CI so the verification layer cannot rot |
| `9459ae3e61` | `4b4bc08882` | #15 | 2026-08-06 | Fix the audit findings: create-path leak, pull() contract, doc rot, CI nits |
| `ff19b220dc` | `af369bfa27` | #16 | 2026-08-07 | v0.3: re-pin the profile ladder — economy at 18 kHz, balanced keeps 19 |
| `52f37ac074` | `9139032be8` | #16 | 2026-08-07 | Add super_economy: the 16 kHz voice/comms tier |
| `3ba6f1393c` | `7dd3ae7d06` | #16 | 2026-08-07 | Add the profile-ladder comparison notebook |
| `1389e6bea2` | `879dcbd45f` | #16 | 2026-08-07 | Re-record icount baselines for the v0.3 profile ladder |
| `f1e566a1e8` | `cf5a4a27d1` | #16 | 2026-08-07 | Dedup CI: one surviving run per head SHA across push and pull_request |
| `94775b0416` | `f9bd873a7d` | #16 | 2026-08-07 | CI dedup, take two: branch-filter push instead of SHA-keyed cancellation |
| `349ab7b1df` | `1c3ef7e4d8` | #17 | 2026-09-26 | Bump DspTap to 0eb09fa and SampleRateTap to 2b4dff1; re-record icount |
| `65aa2902e8` | `6f91664488` | #18 | 2026-09-27 | Harden the ratchet and CI so the migration gates have data to read |
| `bc3f5eaab0` | `f0651442a1` | #18 | 2026-09-27 | Re-record Hexagon icount baselines under the isolated harness |
| `dcf2abcb45` | `7211dccdce` | #18 | 2026-09-27 | Pin the notebook environment and re-execute every notebook |
| `8f19e8b967` | `654659ea27` | #19 | 2026-09-27 | Re-pin the SampleRateTap test dependency to its post-step-P main |

Full 40-character SHAs, one pair per line (old new), oldest first:

```
b3b088dd8e583dff75265d40a195fe6316d38e4f 37b3a349037e1e09d0be8a2061b72c4002084515
f1d5013cffbba90170a90d9f5167a2a692a9f612 a5a727ad6f7a7d181a43c2173c539d1273a55df4
34d97d0b595384e7188ef8fd3bec29ef2e4d7280 bfb8eb66c38437e2ef34964e8a95e6949ed49c15
b4aac5179178a4546af78f0aa8542fa912f94a95 5a2ee3f626a8e90f919a41db69836c1876442b34
c0894cfcd5c622113dcbed9e2a841540450f15a2 89c7eba2545e6d9e9619437fe242e66390a9b393
a83d6d85ae39a2d3cd2123c5a2df9112c403e50a b8bad2e02f8f12b564d2e46884cf2d535ebb1af3
81c09e8f18dd9468d73bd9cc2c2fb5458764739f d93c0d537311754c4ebe521447f1a3f410289c37
c80a87240502a87ef406e6644b63da248a13a8c0 29223e9da708b2b89914bdb404ef01e472300184
c3f19672cc874a65eac95bf23d52540cd3276d44 15061f4366ce182fe411abc149a3823f27735f5e
4bec30cef066894eda919771ff001a1c0ca60afa 4ee4c4cef5e85e6c2c02411f3ceff5051cf5f63c
01de4aae7ef523593b934a02f7fa306aa8223a05 9354dacae94873399911b4253261013965416c8e
58e3e1fdea0df184105ee95a5231a175d71e4129 454671a3c70c031e91e543bb7e169354b1b81620
79ce9f1529638e487383cb9aec6764aac2a2daa4 c89e674bc81b40816bc785e9f65b8bd8ecadc95e
179f52558c96aeea8d987092ce6e6989986e8937 240fb38dc167624fb97117f922df36999684e176
580ea47c1619bb7bb88f947c234155fa07ff6a03 ebae70c5dfa4727b5e8ac7e96da6714bcf309365
d1045b82d0ebcd9a843a8a5af411d778f245df00 0ae08adef43711a72ccb31969ddc1e4df3b31f8f
e56f7041dfad444ee75f0304e26bceb9a07ca261 2138c05caa0d30df640d1595d51e0ba7358131f1
9f871b5302f73a5dc6c85991310206c1e835fb72 14e7cd65eec95766a880d329cc6cb2f560d4fc13
f0f459a25c84a136d26463167ca364917dfba0a6 8ff36d6a8916a17176324ce8bc0d981427a467c2
606587e57e45d712c7452459437ef047c79556b0 a808a0f13af75d3ed018822c58dcaa710a776af1
1d7ba8d2e6a1872909533226966641bf25832f10 bbcfb875b23a4e6ac90df51c40da793d1fef4b77
9459ae3e61d17a99ac5de7bbb44087e3abe6b13b 4b4bc088829f32c68d37e9d1a8e77dfc2a0e0b80
ff19b220dc41bdab585d90d3fe23e172650e0cb1 af369bfa27b87576bd073394af826bdc2f24383d
52f37ac07428695c3ab58a5ce3fba36a627cf7b6 9139032be8df002315c57bd74a6e17ffffa7e15a
3ba6f1393c20a96114208cbb2fbeec6c9e95a4f9 7dd3ae7d0612646c602ff80f82d90eaf93346d46
1389e6bea25d4e197733e70f9924a872bdd99afc 879dcbd45f5310587eeaba9d6377712b8a376520
f1e566a1e89d3b4e491d58b806f7984e6b4abf8d cf5a4a27d1cf0112394de5af8bfbcf1dd7fabe17
94775b04161e7c0d8c28aea4360c1c7db57be340 f9bd873a7d59b99fe01d2b36d0014a07f7e33d25
349ab7b1df63d75e5dac9f823674e40e865b40e9 1c3ef7e4d828ee2ff38f5e4f92122a8e114c0e62
65aa2902e85d77ae8959a2d3278b559f715a48e2 6f916644887dcb093cec1886326bc4b217fdf772
bc3f5eaab072e526917b85e88ad13fab4b43e544 f0651442a128b0242e8a61f3720ceb1a49a3745b
dcf2abcb450a148411d4de3e3d23dbb420a3b2b4 7211dccdced2cc7dadc51f8e7ecaa3b52ceeb62a
8f19e8b967b8d3e9eb22393e881396d418c0584f 654659ea272fa62d6458c2ef1f95eaeb7c4ce276
```
