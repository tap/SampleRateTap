# Summary

[Introduction](introduction.md)

# Part 0 — The problem

- [Two crystals, one stream](part0/two-crystals.md)
- [Budgets: latency, quality, compute](part0/budgets.md)

# Part I — The machine, file by file

- [Designing the filter: kaiser.h](part1/kaiser.md)
- [The polyphase bank](part1/polyphase-bank.md)
- [Sample types as a customization point: sample_traits.h](part1/sample-traits.md)
- [The lock-free ring: spsc_ring.h](part1/spsc-ring.md)
- [The clock servo: pi_servo.h](part1/pi-servo.md)
- [The fractional resampler](part1/fractional-resampler.md)
- [Composition: converter.h](part1/asrc.md)

# Part II — The proof system

- [Tests as specifications](part2/tests.md)
- [Counting instructions, deterministically](part2/icount.md)
- [Notebooks as calibrated instruments](part2/notebooks.md)

# Part III — Optimizing honestly

- [Profile first, claim later (C1–C2)](part3/c1-c2.md)
- [The integer phase and the wide MACs (C3–C5)](part3/c3-c5.md)
- [The channel axis (C6)](part3/c6.md)

# Part IV — Portability

- [Hexagon: a DSP that keeps secrets](part4/hexagon.md)
- [Cortex-M: bare metal, two ways](part4/cortex-m.md)
- [The C ABI](part4/c-abi.md)

# Part V — Deployment

- [Real clocks: bridges and firmware](part5/hardware.md)
- [Channels, rates, and the rules that scale](part5/scaling.md)

# Part VI — The bridge engine

- [The degenerate case: 44.1 ↔ 48 as a type](part6/degenerate.md)
- [The phase table and the schedule](part6/phase-table.md)
- [The converter, and the campaign that made it fast](part6/converter.md)
- [Three legs, and the bridge to the ASRC](part6/proof-bridge.md)

# Part VII — The rational engine

- [A ratio is a type: ratio.h](part7/charter.md)
- [One filter per stage: nyquist.h and design.h](part7/nyquist.md)
- [Two machines, one filter: stage.h](part7/stage.md)
- [Chains and the coverage matrix: chain.h](part7/chain.md)
- [Sixteen bits, per stage](part7/fixed-point.md)
- [The proof, the ABI, and the levers that lost](part7/proof.md)

# Epilogue

- [A letter from the list](epilogue/letter.md)

---

[Appendix A: The C++ decision log](appendix/cpp-decisions.md)
[Appendix B: Glossary](appendix/glossary.md)
[Appendix C: Annotated bibliography](appendix/bibliography.md)
[Appendix D: The two budgets](appendix/two-budgets.md)
