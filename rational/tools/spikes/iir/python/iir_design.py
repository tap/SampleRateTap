# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""IIR-0 spike: the two-path allpass half-band designs, measured in Python.

Nothing here ships. It designs and measures the structures of the IIR plan
(IIR_PLAN v8) so the spike's go/no-go list has numbers, and it generates the
coefficient tables the spike's C++ programs read (gen_tables.py). Every
formula is traced to the published literature named beside it; the README
(rational/tools/spikes/iir/README.md) collects the sources.

Conventions
-----------
- A profile is (A dB, p) with p the passband edge over the chain's lowest
  rate r_min (rational's profile, design.h). A stage at design divisor d
  (its lower rate is d * r_min, rational/PLAN.md 3.1) has its passband edge
  at p / d of its lower rate and its stopband mirrored about its own low
  Nyquist (IIR_PLAN I13).
- Frequencies named f_* are in cycles per sample of the rate named; theta
  is the low (path) rate's angle, omega the high rate's, theta = 2 omega.
- A half-band H(z) = 1/2 [A0(z^2) + z^-1 A1(z^2)] (Valenzuela and
  Constantinides 1983). The nonlinear variant's paths are cascades of
  one-multiplier sections (a + w^-1) / (1 + a w^-1) in w = z^2; the
  near-linear variant's A0 is the pure delay w^-K and A1 a real allpass of
  order N in w that approximates the delay w^-(K - 1/2).
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from itertools import product

import numpy as np
from scipy import optimize, signal

# rational's profiles (design.h) and its design margin and grid
# (k_design_margin_db, k_design_grid_points): a pin is the smallest design
# whose worst stopband on a 16384-point grid is at or below -(A + 1 dB).
PROFILES = {
    "super_economy": (70.0, 1.0 / 3.0),
    "economy": (70.0, 3.0 / 8.0),
    "balanced": (70.0, 19.0 / 48.0),
    "transparent": (120.0, 5.0 / 12.0),
}
PROFILE_ORDER = ("super_economy", "economy", "balanced", "transparent")
MARGIN_DB = 1.0
GRID = 16384
DIVISORS = (1, 2, 4, 8)


def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-300))


# ---------------------------------------------------------------------------
# The nonlinear variant: the elliptic (bireciprocal) half-band, closed form.
# ---------------------------------------------------------------------------
def halfband_coefficients(ncoef: int, transition: float, m=None) -> np.ndarray:
    """The allpass coefficients of the order-(2 ncoef + 1) elliptic half-band.

    transition is the transition band's width over the high rate,
    1/2 - 2 f_pass (f_pass over the high rate). The steps, and their sources:
    - the selectivity k = tan^2(omega_p / 2), the bireciprocal case of the
      elliptic approximation (omega_s = pi - omega_p): Gazsi 1985;
    - the complementary modulus k' = sqrt(1 - k^2), the nome q by its series
      q0 + 2 q0^5 + 15 q0^9 + 150 q0^13 with q0 = (1 - sqrt k') / (2 (1 +
      sqrt k')), and the analogue critical frequencies Omega_i by their
      theta-function series (the elliptic approximation's formulas as
      Antoniou 2006 gives them, also Gazsi 1985);
    - the half-band's allpass coefficient a_i = (1 - x_i) / (1 + x_i) with
      x_i = sqrt((1 - k Omega_i^2)(1 - Omega_i^2 / k)) / (1 + Omega_i^2),
      the pole of the bireciprocal filter on the imaginary axis of the
      z-plane at +-j sqrt(a_i): Gazsi 1985, Valenzuela and Constantinides
      1983 (the two-path split).
    `m` is a math namespace (numpy by default, or an mpmath context) so the
    same steps run in double and at high precision.
    """
    if m is None:
        m = np
    pi = m.pi
    order = 2 * ncoef + 1
    k = m.tan((1 - 2 * transition) * pi / 4) ** 2
    kk = m.sqrt(m.sqrt(1 - k * k))
    e = (1 - kk) / (1 + kk) / 2
    e4 = e ** 4
    q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)))
    out = []
    for c in range(1, ncoef + 1):
        num = sum((-1) ** i * q ** (i * (i + 1)) * m.sin((2 * i + 1) * c * pi / order) for i in range(12))
        den = sum((-1) ** i * q ** (i * i) * m.cos(2 * i * c * pi / order) for i in range(1, 12))
        ww = num * m.sqrt(m.sqrt(q)) / (den + 0.5)
        w2 = ww * ww
        x = m.sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2)
        out.append((1 - x) / (1 + x))
    return np.array(out, dtype=object if m is not np else float)


def split_paths(a):
    """Sorted coefficients alternate paths: a[0::2] in A0, a[1::2] in A1."""
    a = np.sort(np.asarray(a, dtype=float))
    return a[0::2], a[1::2]


def first_order_cascade(coefs, w):
    """prod (a + w^-1) / (1 + a w^-1) at w (complex, on the unit circle)."""
    out = np.ones_like(w)
    wi = 1.0 / w
    for a in coefs:
        out = out * (a + wi) / (1 + a * wi)
    return out


def first_order_cascade_delay(coefs, theta):
    """Group delay of that cascade in path (low-rate) samples."""
    tau = np.zeros_like(theta)
    for a in coefs:
        tau = tau + (1 - a * a) / (1 + 2 * a * np.cos(theta) + a * a)
    return tau


@dataclass
class TwoPath:
    """A two-path half-band: A0 and A1 as callables of theta, with delays."""

    kind: str  # "nonlinear" or "near_linear"
    a0: np.ndarray = field(default_factory=lambda: np.zeros(0))  # nonlinear path 0
    a1: np.ndarray = field(default_factory=lambda: np.zeros(0))  # nonlinear path 1
    K: int = 0  # near-linear: path 0 is w^-K
    den: np.ndarray = field(default_factory=lambda: np.ones(1))  # near-linear A1 denominator, den[0] = 1

    # -- responses at the high rate, f in cycles per high-rate sample --
    def path_responses(self, f):
        z = np.exp(2j * np.pi * np.asarray(f, dtype=float))
        w = z * z
        if self.kind == "nonlinear":
            return first_order_cascade(self.a0, w), first_order_cascade(self.a1, w)
        A0 = w ** (-self.K)
        A1 = allpass_response(self.den, 4 * np.pi * np.asarray(f, dtype=float))
        return A0, A1

    def response(self, f):
        z = np.exp(2j * np.pi * np.asarray(f, dtype=float))
        A0, A1 = self.path_responses(f)
        return 0.5 * (A0 + A1 / z)

    def group_delay(self, f):
        """High-rate samples, exact: -Im(H'/H) from the paths' delays."""
        f = np.asarray(f, dtype=float)
        theta = 4 * np.pi * f
        z = np.exp(2j * np.pi * f)
        A0, A1 = self.path_responses(f)
        if self.kind == "nonlinear":
            t0 = 2 * first_order_cascade_delay(self.a0, theta)
            t1 = 2 * first_order_cascade_delay(self.a1, theta)
        else:
            t0 = np.full_like(f, 2.0 * self.K)
            t1 = 2 * allpass_group_delay(self.den, theta)
        H = 0.5 * (A0 + A1 / z)
        dH = 0.5 * (-1j * t0 * A0 - 1j * (1 + t1) * A1 / z)
        return -np.imag(dH / H)

    @property
    def multiplies(self) -> int:
        """Multiplies per low-rate (path-rate) sample: the decimator's MACs per output."""
        if self.kind == "nonlinear":
            return len(self.a0) + len(self.a1)
        return len(self.den) - 1

    @property
    def sections(self) -> int:
        if self.kind == "nonlinear":
            return len(self.a0) + len(self.a1)
        n = len(self.den) - 1
        return (n + 1) // 2  # second-order sections, one first-order if n is odd


def stopband_grid(fp_hi, n=GRID):
    """The stopband from 1/2 - f_pass to 1/2 of the high rate."""
    return np.linspace(0.5 - fp_hi, 0.5, n)


def passband_grid(fp_hi, n=GRID):
    return np.linspace(0.0, fp_hi, n)


def stage_edges(p, d):
    """(f_pass over the high rate, theta_p at the path rate) at divisor d."""
    fp_hi = p / (2.0 * d)
    return fp_hi, 4.0 * np.pi * fp_hi


def nonlinear_design(A, p, d=1, margin=MARGIN_DB, max_coef=40):
    """The fewest coefficients whose closed-form design meets -(A + margin)."""
    fp_hi, _ = stage_edges(p, d)
    tr = 0.5 - 2 * fp_hi
    fs = stopband_grid(fp_hi)
    for nc in range(1, max_coef + 1):
        a = halfband_coefficients(nc, tr)
        a0, a1 = split_paths(a)
        hb = TwoPath("nonlinear", a0=a0, a1=a1)
        sb = db(hb.response(fs)).max()
        if sb <= -(A + margin):
            return hb, sb
    raise RuntimeError("no nonlinear design")


# ---------------------------------------------------------------------------
# The near-linear variant: A1 a real allpass of order N approximating the
# delay w^-(K - 1/2), A0 = w^-K.
# ---------------------------------------------------------------------------
def allpass_response(den, theta):
    """A(e^{j theta}) = e^{-j N theta} conj(D) / D, D = sum d_k e^{-j k theta}."""
    den = np.asarray(den, dtype=float)
    N = len(den) - 1
    E = np.exp(-1j * np.outer(theta, np.arange(N + 1)))
    D = E @ den
    return np.exp(-1j * N * theta) * np.conj(D) / D


def allpass_phase(den, theta):
    den = np.asarray(den, dtype=float)
    N = len(den) - 1
    E = np.exp(-1j * np.outer(theta, np.arange(N + 1)))
    D = E @ den
    return -N * theta - 2 * np.unwrap(np.angle(D))


def allpass_group_delay(den, theta):
    """-d(phase)/d(theta) = N + 2 d(arg D)/d(theta), in path samples."""
    den = np.asarray(den, dtype=float)
    N = len(den) - 1
    k = np.arange(N + 1)
    E = np.exp(-1j * np.outer(theta, k))
    D = E @ den
    dD = E @ (-1j * k * den)
    return N + 2 * np.imag(dD / D)


def nearlinear_phase_error(den, K, theta):
    """epsilon(theta) = arg A1 + (K - 1/2) theta, unwrapped from theta = 0."""
    return allpass_phase(den, theta) + (K - 0.5) * theta


def nearlinear_lp(N, K, theta_p, n_grid=None, iters=12):
    """Minimax stopband by iterated linear programming (the spike's method).

    For this structure the stopband at the mirror of a passband frequency is
    |sin(eps / 2)| with eps the allpass path's phase error against the delay
    path (README, "the near-linear identity"), and
        sum_k d_k sin(k theta + beta) = |D(e^{j theta})| sin(eps / 2),
        beta = (D_t - N) theta / 2, D_t = K - 1/2,
    which is linear in the denominator coefficients d_k. So with the weight
    1 / |D| frozen from the previous iterate, minimising the largest
    |sin(eps / 2)| on a grid is a linear program in (d_1 .. d_N, t). The
    weight starts at 1 (the plain equation error) and is updated until the
    design stops improving.
    """
    if n_grid is None:
        n_grid = max(400, 40 * N)
    theta = np.linspace(0.0, theta_p, n_grid)
    Dt = K - 0.5
    beta = (Dt - N) * theta / 2.0
    S = np.sin(np.outer(theta, np.arange(N + 1)) + beta[:, None])  # columns k = 0..N
    wgt = np.ones(n_grid)
    best = None
    for _ in range(iters):
        A = S[:, 1:] * wgt[:, None]
        b0 = S[:, 0] * wgt
        # rows: +(A d + b0) <= t, -(A d + b0) <= t
        A_ub = np.vstack([np.hstack([A, -np.ones((n_grid, 1))]), np.hstack([-A, -np.ones((n_grid, 1))])])
        b_ub = np.concatenate([-b0, b0])
        c = np.zeros(N + 1)
        c[-1] = 1.0
        res = optimize.linprog(c, A_ub=A_ub, b_ub=b_ub, bounds=[(None, None)] * N + [(0, None)], method="highs")
        if res.status != 0:
            break
        den = np.concatenate([[1.0], res.x[:N]])
        if np.max(np.abs(np.roots(den))) >= 1.0:
            break
        err = np.abs(np.sin(nearlinear_phase_error(den, K, theta) / 2)).max()
        if best is None or err < best[1] * (1 - 1e-9):
            best = (den, err)
        else:
            break
        E = np.exp(-1j * np.outer(theta, np.arange(N + 1)))
        wgt = 1.0 / np.abs(E @ den)
    return best


def nearlinear_polish(den, K, theta_p, n=1500):
    """Polish the LP design on the true objective: minimise t subject to
    -t <= eps(theta_i) <= t (SLSQP on the epigraph, analytic Jacobian),
    keeping the polished design only if it is stable and better on the
    dense grid. It moves the LP's designs by up to 0.7 dB."""
    N = len(den) - 1
    th = np.linspace(0, theta_p, n)
    k = np.arange(N + 1)
    E = np.exp(-1j * np.outer(th, k))

    def eps(x):
        return nearlinear_phase_error(np.r_[1, x], K, th)

    def cons(v):
        e = eps(v[:-1])
        return np.r_[v[-1] - e, v[-1] + e]

    def cjac(v):
        D = E @ np.r_[1, v[:-1]]
        J = -2 * np.imag(E[:, 1:] / D[:, None])
        one = np.ones((n, 1))
        return np.vstack([np.hstack([-J, one]), np.hstack([J, one])])

    v0 = np.r_[den[1:], np.abs(eps(den[1:])).max()]
    try:
        r = optimize.minimize(lambda v: v[-1], v0, jac=lambda v: np.r_[np.zeros(N), 1.0], method="SLSQP",
                              constraints=[{"type": "ineq", "fun": cons, "jac": cjac}],
                              options={"maxiter": 500, "ftol": 1e-20})
    except Exception:
        return den
    d2 = np.r_[1.0, r.x[:-1]]
    if not np.all(np.isfinite(d2)) or np.max(np.abs(np.roots(d2))) >= 1.0:
        return den
    dense = np.linspace(0.0, theta_p, GRID)
    e1 = np.abs(np.sin(nearlinear_phase_error(den, K, dense) / 2)).max()
    e2 = np.abs(np.sin(nearlinear_phase_error(d2, K, dense) / 2)).max()
    return d2 if e2 < e1 else den


def nearlinear_design(A, p, d=1, margin=MARGIN_DB, max_order=80, K_offsets=(-1, 0, 1, 2)):
    """The lowest order N (and its best K) meeting -(A + margin)."""
    fp_hi, theta_p = stage_edges(p, d)
    target = 10 ** (-(A + margin) / 20.0)
    dense = np.linspace(0.0, theta_p, GRID)
    for N in range(1, max_order + 1):
        cands = []
        for off in K_offsets:
            K = N + off
            if K < 1:
                continue
            r = nearlinear_lp(N, K, theta_p)
            if r is None:
                continue
            den = nearlinear_polish(r[0], K, theta_p)
            err = np.abs(np.sin(nearlinear_phase_error(den, K, dense) / 2)).max()
            cands.append((err, K, den))
        if not cands:
            continue
        err, K, den = min(cands, key=lambda c: c[0])
        if err <= target:
            hb = TwoPath("near_linear", K=K, den=den)
            sb = db(hb.response(stopband_grid(fp_hi))).max()
            return hb, sb
    raise RuntimeError("no near-linear design")


def nearlinear_sos(den):
    """The allpass path as sections: second-order (complex pairs, or real
    pairs) ordered by increasing pole radius, then one first-order if N is
    odd. Each section is (c1, c2) for (c2 + c1 w^-1 + w^-2) / (1 + c1 w^-1 +
    c2 w^-2), or (c1,) for (c1 + w^-1) / (1 + c1 w^-1)."""
    poles = np.roots(den)
    cplx = sorted([p for p in poles if p.imag > 1e-12], key=abs)
    real = sorted([p.real for p in poles if abs(p.imag) <= 1e-12], key=abs)
    secs = []
    for p in cplx:
        secs.append((-2 * p.real, abs(p) ** 2, abs(p)))
    # pair real poles two by two (smallest radii together), a first-order left
    while len(real) >= 2:
        r1, r2 = real.pop(0), real.pop(0)
        secs.append((-(r1 + r2), r1 * r2, max(abs(r1), abs(r2))))
    secs.sort(key=lambda s: s[2])
    out = [(s[0], s[1]) for s in secs]
    if real:
        out.append((-real[0],))
    return out


# ---------------------------------------------------------------------------
# Phase, round trips
# ---------------------------------------------------------------------------
def nearlinear_stage_phase_error(hb: TwoPath, p, d, f_over_rmin):
    """The stage's passband phase error (rad) at chain frequencies f / r_min."""
    theta = 2 * np.pi * np.asarray(f_over_rmin) / d  # the stage's path rate is d r_min
    return nearlinear_phase_error(hb.den, hb.K, theta) / 2.0


def round_trip(stages, p, n=4001):
    """stages: list of (TwoPath near-linear, divisor). Returns the round trip
    (down then up through the same stages) in-band: max |phase| (deg), the
    residual |1 - G| in dB where G is the round trip's response with its
    delay removed, and the bound from the per-stage maxima."""
    f = np.linspace(0.0, p, n)
    total = np.zeros_like(f)
    mag = np.ones_like(f)
    per_stage = []
    for hb, d in stages:
        e = nearlinear_stage_phase_error(hb, p, d, f)
        total = total + 2 * e  # down and up share one H(z)
        mag = mag * np.cos(e) ** 2  # |H|^2 = cos^2(eps/2), eps/2 = e
        per_stage.append(np.abs(e).max())
    G = mag * np.exp(1j * total)
    resid = np.abs(1 - G).max()
    return {
        "max_phase_deg": float(np.degrees(np.abs(total).max())),
        "bound_phase_deg": float(np.degrees(2 * sum(per_stage))),
        "residual_db": float(db(resid)),
        "bound_residual_db": float(db(2 * np.sin(sum(per_stage)))),
        "per_stage_deg": [float(np.degrees(x)) for x in per_stage],
    }


# ---------------------------------------------------------------------------
# L1 norms (worst-case peak gain from the path input to each node)
# ---------------------------------------------------------------------------
def impulse(n):
    x = np.zeros(n)
    x[0] = 1.0
    return x


def nonlinear_path_nodes(coefs, x):
    """Every node of a one-multiplier cascade y = x1 + a (x - y1): per
    section the difference node (x - y1) and the output y."""
    nodes = []
    y = np.asarray(x, dtype=float)
    for a in coefs:
        xin = y
        yout = signal.lfilter([a, 1.0], [1.0, a], xin)
        y1 = np.concatenate([[0.0], yout[:-1]])
        nodes.append(("diff", a, xin - y1))
        nodes.append(("out", a, yout))
        y = yout
    return nodes


def sos_allpass_nodes(secs, x):
    """Second-order allpass sections y = c2 (x - y2) + c1 (x1 - y1) + x2."""
    nodes = []
    y = np.asarray(x, dtype=float)
    for s in secs:
        xin = y
        if len(s) == 2:
            c1, c2 = s
            yout = signal.lfilter([c2, c1, 1.0], [1.0, c1, c2], xin)
            y1 = np.concatenate([[0.0], yout[:-1]])
            y2 = np.concatenate([[0.0, 0.0], yout[:-2]])
            x1 = np.concatenate([[0.0], xin[:-1]])
            nodes.append(("x-y2", s, xin - y2))
            nodes.append(("x1-y1", s, x1 - y1))
        else:
            (c1,) = s
            yout = signal.lfilter([c1, 1.0], [1.0, c1], xin)
            y1 = np.concatenate([[0.0], yout[:-1]])
            nodes.append(("diff", s, xin - y1))
        nodes.append(("out", s, yout))
        y = yout
    return nodes


# ---------------------------------------------------------------------------
# The guard band filter: odd-order Chebyshev II and the type II FIR.
# ---------------------------------------------------------------------------
def cheby2_stopband_edge(p_rate, order, A, rp=0.01):
    """The stopband edge (fraction of the rate) of the order-n Chebyshev II
    flat within rp dB (peak to peak, monotone) to p_rate and A dB down from
    the edge, by the bilinear transform's prewarping (Antoniou 2006)."""
    Op = math.tan(math.pi * p_rate)
    D = math.sqrt((10 ** (A / 10) - 1) / (10 ** (rp / 10) - 1))
    Os = Op * math.cosh(math.acosh(D) / order)
    return math.atan(Os) / math.pi


def cheby2_design(p_rate, order, A, rp=0.01):
    fs = cheby2_stopband_edge(p_rate, order, A, rp)
    z, pz, k = signal.cheby2(order, A, 2 * fs, output="zpk")
    return fs, (z, pz, k)


def cheby2_sos_ordered(zpk):
    """Sections ordered by increasing pole radius (IIR_PLAN I8), each pole
    pair with its nearest zero pair, then the gain distributed by the L-inf
    norm (Jackson 1970): every partial cascade peaks at 0 dB, the last
    section carrying the rest."""
    z, p, k = zpk
    sos = signal.zpk2sos(z, p, k, pairing="nearest")  # scipy orders by increasing pole radius
    radii = [max(abs(np.roots(s[3:]))) for s in sos]
    order = np.argsort(radii)
    sos = sos[order]
    # strip gains, then distribute
    total_gain = np.prod([s[0] for s in sos])
    sos = sos.copy()
    for s in sos:
        s[:3] = s[:3] / s[0]
    w = np.linspace(0, np.pi, 8192)
    cum = np.ones_like(w, dtype=complex)
    g_used = 1.0
    for i, s in enumerate(sos):
        _, h = signal.freqz(s[:3], s[3:], worN=w)
        if i < len(sos) - 1:
            peak = np.abs(cum * h).max()
            s[:3] = s[:3] / peak
            g_used *= 1 / peak
            cum = cum * h / peak
        else:
            s[:3] = s[:3] * total_gain / g_used
    return sos


def cheby2_macs(order):
    """Biquads with b0 = b2 (zeros on the unit circle): 4 MACs each; the
    first-order section with its zero at -1: 2 MACs."""
    return 4 * (order // 2) + (2 if order % 2 else 0)


def typeii_min_length(p_rate, f_stop, A, rp_pp=0.01, max_len=400):
    """The shortest even-length (type II) equiripple FIR (Parks-McClellan,
    scipy.signal.remez) flat within rp_pp dB peak to peak to p_rate and A dB
    down from f_stop (fractions of the rate)."""
    dp = (10 ** (rp_pp / 20) - 1) / (10 ** (rp_pp / 20) + 1)  # 20 log10((1+dp)/(1-dp)) = rp_pp
    ds = 10 ** (-A / 20)
    for N in range(4, max_len, 2):
        try:
            h = signal.remez(N, [0, p_rate, f_stop, 0.5], [1, 0], weight=[1 / dp, 1 / ds], fs=1.0, maxiter=400)
        except Exception:
            continue
        w, H = signal.freqz(h, worN=2 ** 15, fs=1.0)
        pb = db(H[w <= p_rate])
        sb = db(H[w >= f_stop]).max()
        if pb.max() - pb.min() <= rp_pp + 1e-9 and sb <= -A:
            return N, h, float(pb.max() - pb.min()), float(sb)
    return None


# ---------------------------------------------------------------------------
# The four-path stage: H(z) = 1/4 sum_k z^-k A_k(z^4), A_k cascades of
# (a + z^-4) / (1 + a z^-4) (harris 2004, the M-path recursive polyphase
# filter; Renfors and Saramaki 1987, recursive Nth-band filters).
# ---------------------------------------------------------------------------
def fourpath_response(paths, f):
    z = np.exp(2j * np.pi * np.asarray(f, dtype=float))
    zi4 = z ** -4
    tot = np.zeros_like(z)
    for k, cs in enumerate(paths):
        A = np.ones_like(z)
        for a in cs:
            A = A * (a + zi4) / (1 + a * zi4)
        tot = tot + A * z ** (-k)
    return tot / 4


def fourpath_group_delay(paths, f):
    f = np.asarray(f, dtype=float)
    z = np.exp(2j * np.pi * f)
    zi4 = z ** -4
    H = np.zeros_like(z)
    dH = np.zeros_like(z)
    th = 8 * np.pi * f
    for k, cs in enumerate(paths):
        A = np.ones_like(z)
        for a in cs:
            A = A * (a + zi4) / (1 + a * zi4)
        tau = k + 4 * first_order_cascade_delay(cs, th)
        H = H + A * z ** (-k)
        dH = dH - 1j * tau * A * z ** (-k)
    return -np.imag(dH / H)


def fourpath_bands(p, D):
    """The bands that fold onto the passband [0, p r_min] after the stage's
    decimation by 4, over its input rate 4 D r_min (IIR_PLAN 2.4)."""
    fp = p / (4.0 * D)
    return [(0.25 - fp, 0.25 + fp), (0.5 - fp, 0.5)], fp


def compositions(total, parts=4, spread=None):
    for c in product(range(total + 1), repeat=parts):
        if sum(c) == total and (spread is None or max(c) - min(c) <= spread):
            yield list(c)


def fourpath_design(A, p, D, S, starts=24, seed=1, spread=2, margin=MARGIN_DB):
    """Best minimax design with S sections over all layouts (with paths at
    most `spread` apart), by SLSQP on the epigraph from random starts, as the
    audit's scratch program did. Returns (stopband dB on a dense grid, layout,
    coefficients)."""
    bands, fp = fourpath_bands(p, D)
    fst = np.concatenate([np.linspace(lo, hi, 240) for lo, hi in bands])
    fst_dense = np.concatenate([np.linspace(lo, hi, GRID // 2) for lo, hi in bands])
    rng = np.random.default_rng(seed)
    best = None
    for layout in compositions(S, 4, spread):
        n = S

        def unpack(x, layout=layout):
            out, i = [], 0
            for m in layout:
                out.append(list(x[i:i + m]))
                i += m
            return out

        def cons(v, unpack=unpack):
            return v[-1] - np.abs(fourpath_response(unpack(v[:-1]), fst)) ** 2

        for t in range(starts):
            x0 = rng.uniform(0.02, 0.95, n)
            v0 = np.concatenate([x0, [np.abs(fourpath_response(unpack(x0), fst)).max() ** 2]])
            try:
                r = optimize.minimize(lambda v: v[-1], v0, method="SLSQP",
                                      constraints=[{"type": "ineq", "fun": cons}],
                                      bounds=[(-0.999, 0.999)] * n + [(0, 1)],
                                      options={"maxiter": 400, "ftol": 1e-18})
            except Exception:
                continue
            paths = unpack(r.x[:-1])
            sb = db(fourpath_response(paths, fst_dense)).max()
            if best is None or sb < best[0]:
                best = (float(sb), layout, [[float(a) for a in pth] for pth in paths])
    return best


def halfband_pair_on_bands(hb_hi: TwoPath, hb_lo: TwoPath, p, D):
    """The cascade of two half-bands (the upper at 4 D r_min -> 2 D r_min, the
    lower at 2 D r_min -> D r_min) on the four-path stage's folding bands."""
    bands, _ = fourpath_bands(p, D)
    f = np.concatenate([np.linspace(lo, hi, GRID // 2) for lo, hi in bands])
    return float(db(hb_hi.response(f) * hb_lo.response(2 * f)).max())
