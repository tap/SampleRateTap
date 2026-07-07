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
- [Composition: asrc.h](part1/asrc.md)

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

# Epilogue

- [A letter from the list](epilogue/letter.md)

---

[Appendix A: The C++ decision log](appendix/cpp-decisions.md)
[Appendix B: Glossary](appendix/glossary.md)
[Appendix C: Annotated bibliography](appendix/bibliography.md)
