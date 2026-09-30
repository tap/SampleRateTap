#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""The monorepo migration's mechanical rename map, and gate G14.

This file is the specification of every *mechanical* change the migration
makes (MONOREPO_PLAN.md sections 4.1 and 6, steps 1-3). Applied to the
step-0 trees (SampleRateTap@S0 and RatioTap@R0, from tips.txt) it produces
the tree a purely mechanical migration would have at a given step. G14 then
diffs that against the real tree: every hunk left over is a *residual*, and
each must be listed in the reviewed allowlist residual/<step>.txt. That is
how a loosened EXPECT_NEAR or a stray CI edit is caught even when every
other gate is green (R2-GATE-8).

  rename.py build --through STEP --out DIR [--s0-repo P] [--r0-repo P]
                  [--no-format]
  rename.py check --through STEP [--tree DIR] [--allow FILE] [...]
  rename.py apply --step STEP [--tree DIR]   (one class, in place)

STEP is one of 1c, 2, 3.1 ... 3.8 (4 behaves as 3.8). A step's classes
apply cumulatively: --through 3.3 applies paths, 3.1, 3.2 and 3.3.

Conventions:
- Inputs come from `git archive` of the tips, so submodule contents and
  untracked files never enter the comparison; the tree under test is read
  the same way (`git archive HEAD`), and docs/migration/ is excluded.
- Text substitutions are whole-token regexes applied to every text file, in
  the order listed. Order matters where one name is a prefix of another.
- After the substitutions, C/C++ files that changed are run through
  clang-format with the tree's .clang-format, because step 3 formats each
  rename commit with the pre-commit hook (one commit per class, rename and
  reflow together). The hook pins clang-format 18.1.3; so must this.
- Engine names follow D5 (v3.1): RatioTap's engine is `bridge`.
"""
import argparse
import fnmatch
import hashlib
import io
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
STEPS = ["1c", "2", "3.1", "3.2", "3.3", "3.4", "3.5", "3.6", "3.7", "3.8"]
CXX_SUFFIXES = {".h", ".hpp", ".c", ".cc", ".cpp"}
FAMILY_HOLDER = "Timothy Place and the SampleRateTap contributors"


def at_least(through: str, step: str) -> bool:
    through = "3.8" if through == "4" else through
    return STEPS.index(through) >= STEPS.index(step)


# --------------------------------------------------------------------------
# Paths (4.1). Each rule is (step, source repo, old prefix, new prefix);
# new prefix None deletes. The first matching rule of the latest applicable
# step wins, so a later step can re-map an earlier destination.

S0_MOVES_1A = [
    ("CMakeLists.txt", "async/CMakeLists.txt"),
    ("README.md", "async/README.md"),
    ("include/", "async/include/"),
    ("tests/", "async/tests/"),
    ("bench/", "async/bench/"),
    ("examples/", "async/examples/"),
    ("notebooks/", "async/notebooks/"),
    ("tools/capi/", "async/capi/"),
    ("tools/compare_shim/", "async/tools/compare_shim/"),
    ("cmake/r8brain.cmake", "async/cmake/r8brain.cmake"),
    ("docs/PERFORMANCE.md", "async/docs/PERFORMANCE.md"),
    ("docs/COMPARISON.md", "async/docs/COMPARISON.md"),
    ("docs/HARDWARE_TESTING.md", "async/docs/HARDWARE_TESTING.md"),
]

# RatioTap files that do not survive under bridge/, with the step that
# removes them. Everything else in R0 moves to bridge/ at 1b.
R0_DROPS = [
    ("1c", ".gitmodules"),  # 1b: merged into root .gitmodules (--ours)
    ("1c", ".clang-format"),  # 1b: byte-identical to root
    ("1c", ".clang-tidy"),
    ("1c", "STYLE.md"),
    ("1c", ".pre-commit-config.yaml"),
    ("1c", ".claude/"),
    ("1c", "scripts/tidy.sh"),
    ("1c", ".github/pull_request_template.md"),
    ("1c", ".github/workflows/"),  # 1c: ported into the root workflows
    ("1c", ".gitignore"),
    ("1c", "LICENSE"),  # 1c: root LICENSE carries D14's line
    ("1c", "requirements.lock"),  # 1c: identical root lockfile (P.3)
    ("1c", "requirements.in"),
    ("2", "cmake/"),  # 2: root copies
    ("2", "platform/"),
    ("2", "tools/qemu_insn_plugin/"),
    ("2", "scripts/icount.py"),
]
R0_RELOCATE_1C = [("scripts/fetch_hexagon_toolchain.sh", "scripts/fetch_hexagon_toolchain.sh")]


def map_s0_path(path: str, through: str) -> str | None:
    for old, new in S0_MOVES_1A:
        if path == old or (old.endswith("/") and path.startswith(old)):
            path = new + path[len(old):]
            break
    return map_later_paths(path, through)


def map_r0_path(path: str, through: str) -> str | None:
    if path.startswith("submodules/"):
        return None  # 1b: gitlinks removed at the merge
    for step, old in R0_DROPS:
        if at_least(through, step) and (path == old or (old.endswith("/") and path.startswith(old))):
            return None
    for old, new in R0_RELOCATE_1C:
        if path == old:
            return map_later_paths(new, through)
    return map_later_paths("bridge/" + path, through)


def map_later_paths(path: str, through: str) -> str | None:
    if not at_least(through, "3.1"):
        return path
    # 3.1: header paths, asrc.h -> converter.h (D15), srt.h -> async.h.
    if path == "async/include/srt/detail/kaiser.h":
        return None
    if path.startswith("async/include/srt/"):
        rest = path[len("async/include/srt/"):]
        rest = {"asrc.h": "converter.h", "srt.h": "async.h"}.get(rest, rest)
        return "async/include/tap/sr/async/" + rest
    if path.startswith("bridge/include/tap/ratio/"):
        return "bridge/include/tap/sr/bridge/" + path[len("bridge/include/tap/ratio/"):]
    if path.startswith("bridge/tools/capi/"):
        path = "bridge/capi/" + path[len("bridge/tools/capi/"):]
    if at_least(through, "3.5"):
        path = re.sub(r"\bsrt_capi\.", "tap_sr_async_capi.", path)
        path = re.sub(r"\bratio_capi\.", "tap_sr_bridge_capi.", path)
    return path


# --------------------------------------------------------------------------
# Text substitutions per class. (step, pattern, replacement); patterns are
# regexes, applied in order.

# Token start: not inside another identifier, except right after a CMake
# "-D" (so -DSRT_WERROR=ON renames with SRT_WERROR).
LB = r"(?:(?<=-D)|(?<![A-Za-z0-9_]))"


def w(name: str) -> str:
    return LB + re.escape(name) + r"(?![A-Za-z0-9_])"


SUBS = [
    # 3.1 paths inside files: include directives and path citations.
    ("3.1", r"srt/detail/kaiser\.h", "tap/dsp/kaiser.h"),
    # 3.1 deletes the kaiser.h re-export; its two users requalify.
    ("3.1", r"(?<![\w:])detail::(design_prototype_compensated|design_prototype|kaiser_beta)\b",
     r"tap::dsp::\1", "path:async/include/*/polyphase_filter.h"),
    ("3.1", r"using namespace tap::samplerate::detail;", "using namespace tap::dsp;",
     "path:async/tests/test_kaiser.cpp"),
    ("3.1", r"include/srt/asrc\.h", "include/tap/sr/async/converter.h"),
    ("3.1", r"include/srt/srt\.h", "include/tap/sr/async/async.h"),
    ("3.1", r"include/srt\b", "include/tap/sr/async"),
    ("3.1", r"include/tap/ratio\b", "include/tap/sr/bridge"),
    ("3.1", r"(?<![\w/])srt/asrc\.h", "tap/sr/async/converter.h"),
    ("3.1", r"(?<![\w/])srt/srt\.h", "tap/sr/async/async.h"),
    ("3.1", r"(?<![\w/])srt/", "tap/sr/async/"),
    ("3.1", r"(?<![\w/])tap/ratio/", "tap/sr/bridge/"),
    ("3.1", r"(?<![\w/])tools/capi/ratio_capi", "capi/ratio_capi"),
    # bridge's C ABI moves up one level (bridge/tools/capi -> bridge/capi):
    # its standalone build and the notebook binding follow.
    ("3.1", r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./\.\.", "${CMAKE_CURRENT_SOURCE_DIR}/..",
     "path:bridge/*capi/CMakeLists.txt"),
    ("3.1", r"cmake -S tools/capi ", "cmake -S capi ", "path:bridge/*capi/CMakeLists.txt"),
    ("3.1", r'ROOT / "tools" / "capi"', 'ROOT / "capi"', "path:bridge/notebooks/*.py"),
    ("3.1", r"`tools/capi/`", "`capi/`", "path:bridge/notebooks/*.ipynb"),
    ("1c", r"add_subdirectory\(tools/capi\)", "add_subdirectory(capi)", "path:async/CMakeLists.txt"),
    # 1c: the book's anchor includes follow the 1a moves.
    ("1c", r"(\{\{#(?:include|rustdoc_include) (?:\.\./)+)include/srt/", r"\1async/include/srt/", "path:book/src/*"),
    ("1c", r"(\{\{#(?:include|rustdoc_include) (?:\.\./)+)tests/", r"\1async/tests/", "path:book/src/*"),
    ("1c", r"(\{\{#(?:include|rustdoc_include) (?:\.\./)+)tools/capi/", r"\1async/capi/", "path:book/src/*"),
    ("3.1", r"add_subdirectory\(tools/capi\)", "add_subdirectory(capi)", "path:bridge/CMakeLists.txt"),
    # 3.2 namespaces and names; D15.
    ("3.2", w("tap::samplerate"), "tap::sr::async", "code"),
    ("3.2", w("tap::ratio"), "tap::sr::bridge", "code"),
    ("3.2", r'"async_sample_rate_converter: ', '"tap::sr::async::converter: '),
    ("3.2", w("basic_async_sample_rate_converter"), "basic_converter"),
    ("3.2", w("async_sample_rate_converter_q15"), "converter_q15"),
    ("3.2", w("async_sample_rate_converter_q31"), "converter_q31"),
    ("3.2", w("async_sample_rate_converter"), "converter"),
    ("3.2", w("srt_test"), "async_test"),
    # 3.3 macros. Guest icount markers (SRT_ICOUNT_DONE, RATIO_ICOUNT_DONE)
    # are deliberately NOT renamed; the host plugin marker is (step 2).
    ("2", w("SRT_INSN_COUNT"), "TAP_SR_INSN_COUNT"),
    ("2", w("RATIO_INSN_COUNT"), "TAP_SR_INSN_COUNT"),
    ("3.3", w("SRT_VERSION_MAJOR"), "TAP_SR_VERSION_MAJOR"),
    ("3.3", w("SRT_VERSION_MINOR"), "TAP_SR_VERSION_MINOR"),
    ("3.3", w("SRT_VERSION_PATCH"), "TAP_SR_VERSION_PATCH"),
    ("3.3", LB + r"SRT_VERSION_\*", "TAP_SR_VERSION_*"),
    ("3.3", w("TAP_RATIO_VERSION_MAJOR"), "TAP_SR_VERSION_MAJOR"),
    ("3.3", w("TAP_RATIO_VERSION_MINOR"), "TAP_SR_VERSION_MINOR"),
    ("3.3", w("TAP_RATIO_VERSION_PATCH"), "TAP_SR_VERSION_PATCH"),
    # The three SRT_ aliases of TAP_DSP_ macros: drop their #defines, then
    # use the originals.
    ("3.3", r"(?m)^#define SRT_(RESTRICT|Q15_SMLALD|CHANNEL_PARALLEL) TAP_DSP_\1\n", ""),
    ("3.3", w("SRT_RESTRICT"), "TAP_DSP_RESTRICT"),
    ("3.3", w("SRT_Q15_SMLALD"), "TAP_DSP_Q15_SMLALD"),
    ("3.3", w("SRT_CHANNEL_PARALLEL"), "TAP_DSP_CHANNEL_PARALLEL"),
    ("3.3", w("TAP_RATIO_MIRRORED_DOT_ATTR"), "TAP_SR_BRIDGE_MIRRORED_DOT_ATTR"),
    ("3.3", w("SRT_CP_MIN_CHANNELS"), "TAP_SR_ASYNC_CP_MIN_CHANNELS"),
    ("3.3", LB + r"SRT_SC_", "TAP_SR_ASYNC_SC_"),
    ("3.3", LB + r"RATIO_SC_", "TAP_SR_BRIDGE_SC_"),
    ("3.3", LB + r"SRT_CMP_", "TAP_SR_ASYNC_CMP_"),
    ("3.3", w("SRT_TESTS_COMPLETE"), "TAP_SR_TESTS_COMPLETE"),
    ("3.3", w("TAP_RATIO_TESTS_COMPLETE"), "TAP_SR_TESTS_COMPLETE"),
    ("3.3", w("SRT_BARE_METAL"), "TAP_SR_BARE_METAL"),
    ("3.3", w("TAP_RATIO_BARE_METAL"), "TAP_SR_BARE_METAL"),
    ("3.3", LB + r"SRT_PICO2_", "TAP_SR_PICO2_"),
    ("3.3", w("SRT_GD"), "TAP_SR_ASYNC_GD"),
    # D13: the family version the bridge notebook prints (ratio_demo.ipynb);
    # G11 executes both trees, so the step-0 output maps to the new value.
    ("3.3", r"RatioTap 0\.3\.0", "RatioTap 0.4.0"),
    # 3.4 CMake: options (D9), targets, projects, test prefix and labels (D16).
    ("3.4", w("SRT_WERROR"), "TAP_SR_ASYNC_WERROR"),
    ("3.4", w("TAP_RATIO_WERROR"), "TAP_SR_BRIDGE_WERROR"),
    ("3.4", w("SRT_BUILD_TESTS"), "TAP_SR_BUILD_TESTS"),
    ("3.4", w("TAP_RATIO_BUILD_TESTS"), "TAP_SR_BUILD_TESTS"),
    ("3.4", w("SRT_BUILD_EXAMPLES"), "TAP_SR_BUILD_EXAMPLES"),
    ("3.4", w("TAP_RATIO_BUILD_EXAMPLES"), "TAP_SR_BUILD_EXAMPLES"),
    ("3.4", w("SRT_BUILD_CAPI"), "TAP_SR_BUILD_CAPI"),
    ("3.4", w("TAP_RATIO_BUILD_CAPI"), "TAP_SR_BUILD_CAPI"),
    ("3.4", w("SRT_BUILD_ICOUNT_BENCH"), "TAP_SR_BUILD_ICOUNT_BENCH"),
    ("3.4", w("TAP_RATIO_BUILD_ICOUNT_BENCH"), "TAP_SR_BUILD_ICOUNT_BENCH"),
    ("3.4", w("SRT_BUILD_BENCHMARKS"), "TAP_SR_BUILD_BENCHMARKS"),
    ("3.4", w("SRT_BUILD_COMPARE_BENCH"), "TAP_SR_BUILD_COMPARE_BENCH"),
    ("3.4", w("SRT_BUILD_COMPARE_SHIM"), "TAP_SR_BUILD_COMPARE_SHIM"),
    ("3.4", w("SRT_ICOUNT_COMPARE"), "TAP_SR_ICOUNT_COMPARE"),
    ("3.4", w("tap::samplerate"), "tap::sr::async", "cmake"),
    ("3.4", w("tap::ratio"), "tap::sr::bridge", "cmake"),
    # The legacy alias becomes the family target; the 3.4 commit then drops
    # the now-duplicate add_library(... ALIAS) line (residual).
    ("3.4", w("SampleRateTap::SampleRateTap"), "tap::sr::async"),
    ("3.4", w("srt_warnings"), "tap_sr_async_warnings"),
    ("3.4", w("tap_ratio_warnings"), "tap_sr_bridge_warnings"),
    ("3.4", w("srt_tests_emulated"), "tap_sr_async_tests_emulated"),
    ("3.4", w("tap_ratio_tests_emulated"), "tap_sr_bridge_tests_emulated"),
    ("3.4", w("srt_tests"), "tap_sr_async_tests"),
    ("3.4", w("tap_ratio_tests"), "tap_sr_bridge_tests"),
    ("3.4", w("tap_ratio"), "tap_sr_bridge"),
    ("3.4", w("srt_bench_compare"), "tap_sr_async_bench_compare"),
    ("3.4", w("srt_bench"), "tap_sr_async_bench"),
    ("3.4", w("srt_alsa_bridge"), "tap_sr_async_alsa_bridge"),
    ("3.4", w("srt_r8brain"), "tap_sr_async_r8brain"),
    ("3.4", w("srt_headers"), "tap_sr_async_headers"),
    ("3.4", r'TEST_PREFIX "ratio\."', 'TEST_PREFIX "bridge."'),
    # The QEMU legs select the engine by label (D16).
    ("3.4", r"(?m)^(\s*label: )ratio$", r"\1bridge", "path:.github/workflows/*.yml"),
    ("3.4", r"LABELS ratio\b", "LABELS bridge"),
    # 3.5 C ABI (D8): functions, handle types, libraries, headers, shim.
    ("3.5", w("SrtHandle"), "tap_sr_async_converter"),
    ("3.5", w("srt_r8b_shim"), "tap_sr_async_r8b_shim"),
    ("3.5", w("srt_r8b_oneshot"), "tap_sr_async_r8b_oneshot"),
    ("3.5", w("srt_r8b_latency_frames"), "tap_sr_async_r8b_latency_frames"),
    ("3.5", w("libsrt_capi"), "libtap_sr_async_capi"),
    ("3.5", w("libratio_capi"), "libtap_sr_bridge_capi"),
    ("3.5", w("srt_capi"), "tap_sr_async_capi"),
    ("3.5", w("ratio_capi"), "tap_sr_bridge_capi"),
    ("3.5", w("RatioTapCapi"), "tap_sr_bridge_capi_standalone"),
] + [
    ("3.5", w("srt_" + f), "tap_sr_async_" + f)
    for f in ["version", "create", "destroy", "push", "pull", "status",
              "designed_latency_seconds", "reset_from_consumer"]
] + [
    ("3.5", w("ratio_" + f), "tap_sr_bridge_" + f)
    for f in ["converter", "version", "create", "destroy", "reset", "process",
              "flush", "flush_output_frames", "outputs_for", "frames_needed",
              "latency_input_frames", "taps"]
] + [
    # 3.6 ratchet binaries: prefix only; workload names and baseline keys keep.
    ("3.6", r"(?<![A-Za-z0-9_])srt_icount_", "tap_sr_async_icount_"),
    ("3.6", r"(?<![A-Za-z0-9_])ratio_icount_", "tap_sr_bridge_icount_"),
]
# Class 3.7 (docs and prose) is not mechanical; its residual is reviewed by
# hand in residual/3.7.txt. Class 3.8 (banners) is below.


def is_cmake(path: str) -> bool:
    name = pathlib.PurePosixPath(path).name
    return name == "CMakeLists.txt" or name.endswith(".cmake")


def apply_subs(text: str, path: str, through: str) -> str:
    # A rule's optional fourth field scopes it: "code" skips CMake files,
    # "cmake" applies only to them (CMake targets rename at 3.4, C++ at 3.2),
    # and "path:<glob>" limits it to matching files.
    # Classes apply in step order whatever their position in SUBS (a 1c
    # rule listed after a 3.1 rule must still run first); stable within a
    # class, where list order matters.
    for step, pat, rep, *scope in sorted(SUBS, key=lambda r: STEPS.index(r[0])):
        if not at_least(through, step):
            continue
        if scope and scope[0].startswith("path:"):
            if not fnmatch.fnmatch(path, scope[0][5:]):
                continue
        elif scope and (scope[0] == "cmake") != is_cmake(path):
            continue
        text = re.sub(pat, rep, text)
    return text


# --------------------------------------------------------------------------
# 3.8 banners (D14). C/C++ and Python sources outside vendored code get
# exactly these two lines at the top (after a shebang); an existing
# Copyright line in the first five lines is rewritten in place.

BANNER_SKIP = ["third_party/*", "*/third_party/*", "submodules/*", "*/reference_vectors.h"]


def banner(text: str, path: str) -> str:
    suffix = pathlib.PurePosixPath(path).suffix
    if suffix in CXX_SUFFIXES:
        c = "//"
    elif suffix == ".py":
        c = "#"
    else:
        return text
    if any(fnmatch.fnmatch(path, g) for g in BANNER_SKIP):
        return text
    lines = text.split("\n")
    head = 1 if lines and lines[0].startswith("#!") else 0
    spdx = f"{c} SPDX-License-Identifier: MIT"
    copy = f"{c} Copyright 2026 {FAMILY_HOLDER}"
    window = lines[head:head + 5]
    idx = next((i for i, l in enumerate(window) if re.match(re.escape(c) + r"\s*Copyright\b", l)), None)
    if idx is not None:
        lines[head + idx] = copy
        if not any(l.strip() == spdx for l in window):
            lines.insert(head + idx, spdx)
    elif any(l.strip() == spdx for l in window):
        at = head + next(i for i, l in enumerate(window) if l.strip() == spdx)
        lines.insert(at + 1, copy)
    else:
        lines[head:head] = [spdx, copy]
    return "\n".join(lines)


# --------------------------------------------------------------------------

def read_tips() -> dict:
    tips = {}
    for line in (HERE / "tips.txt").read_text().splitlines():
        m = re.match(r"^(S0|R0)\s+([0-9a-f]{40})\b", line)
        if m:
            tips[m.group(1)] = m.group(2)
    return tips


def archive(repo: str, rev: str) -> dict[str, bytes]:
    data = subprocess.run(["git", "-C", repo, "archive", "--format=tar", rev],
                          check=True, capture_output=True).stdout
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        for m in tar.getmembers():
            if m.isfile():
                files[m.name] = tar.extractfile(m).read()
            elif m.issym():
                files[m.name] = b"@symlink " + m.linkname.encode()
    return files


def is_text(b: bytes) -> bool:
    return b"\0" not in b[:8192]


def build(args) -> pathlib.Path:
    tips = read_tips()
    out = pathlib.Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    tree: dict[str, bytes] = {}
    for key, repo, mapper in (("S0", args.s0_repo, map_s0_path), ("R0", args.r0_repo, map_r0_path)):
        for path, blob in archive(repo, tips[key]).items():
            new = mapper(path, args.through)
            if new is None:
                continue
            if new in tree and tree[new] != blob:
                sys.exit(f"path collision at {new} ({key}:{path})")
            tree[new] = blob
    changed_cxx = []
    for path, blob in tree.items():
        if path.startswith("docs/migration/") or not is_text(blob):
            continue
        text = blob.decode("utf-8", errors="surrogateescape")
        new = apply_subs(text, path, args.through)
        if at_least(args.through, "3.8"):
            new = banner(new, path)
        if new != text:
            tree[path] = new.encode("utf-8", errors="surrogateescape")
            if pathlib.PurePosixPath(path).suffix in CXX_SUFFIXES:
                changed_cxx.append(path)
    for path, blob in tree.items():
        dst = out / path
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(blob)
    if changed_cxx and not args.no_format:
        fmt = shutil.which("clang-format")
        if fmt is None:
            sys.exit("clang-format 18.1.3 is required (or pass --no-format)")
        subprocess.run([fmt, "-i", "--style=file"] + [str(out / p) for p in changed_cxx],
                       check=True, cwd=out)
    return out


# --------------------------------------------------------------------------
# check: diff the renamed step-0 tree against the tree under test, then
# match each residual hunk against residual/<step>.txt. Entry forms:
#   file <glob>  -- <reason>       every hunk in matching files is allowed
#   hunk <path> <hash12>  -- <reason>
# The hash is over the hunk's -/+ lines only (not its line numbers), so it
# survives unrelated edits elsewhere in the file.

def hunks(diff: str):
    path, body = None, []
    for line in diff.splitlines():
        if line.startswith("diff --git"):
            if path and body:
                yield path, body
            path, body = None, []
            # "a/renamed/<p> b/tree/<p>"; one side only for an added or
            # deleted file, where git names that side twice.
            m = re.match(r"diff --git a/(\S+) b/(\S+)", line)
            path = re.sub(r"^(renamed|tree)/", "", m.group(1)) if m else line
        elif line.startswith("@@"):
            if path and body:
                yield path, body
            body = []
        elif line[:1] in "+-" and not line.startswith(("+++", "---")):
            body.append(line)
    if path and body:
        yield path, body


def check(args) -> int:
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        args.out = str(tmp / "renamed")
        build(args)
        tree = tmp / "tree"
        tree.mkdir()
        for path, blob in archive(args.tree, "HEAD").items():
            if path.startswith("docs/migration/"):
                continue
            (tree / path).parent.mkdir(parents=True, exist_ok=True)
            (tree / path).write_bytes(blob)
        proc = subprocess.run(["git", "diff", "--no-index", "--no-color", "--no-renames",
                               "renamed", "tree"], cwd=tmp, capture_output=True, text=True,
                              errors="surrogateescape")
    allow_files, allow_hunks = [], set()
    allow = pathlib.Path(args.allow or HERE / "residual" / f"{args.through}.txt")
    if allow.exists():
        for line in allow.read_text().splitlines():
            parts = line.split("--")[0].split()
            if len(parts) >= 2 and parts[0] == "file":
                allow_files.append(parts[1])
            elif len(parts) >= 3 and parts[0] == "hunk":
                allow_hunks.add((parts[1], parts[2]))
    bad = 0
    for path, body in hunks(proc.stdout):
        h = hashlib.sha256("\n".join(body).encode()).hexdigest()[:12]
        if any(fnmatch.fnmatch(path, g) for g in allow_files) or (path, h) in allow_hunks:
            continue
        bad += 1
        print(f"RESIDUAL hunk {path} {h}")
        for line in body[:20]:
            print("    " + line)
        if len(body) > 20:
            print(f"    ... {len(body) - 20} more lines")
    print(f"G14 {args.through}: {bad} unlisted residual hunk(s)")
    return 1 if bad else 0


# --------------------------------------------------------------------------
# apply: make ONE rename class's change in a working tree, the way the
# step-3 commits are produced, so every commit is exactly what the map says
# (plus its reviewed residual). Paths move with git mv; text rules whose
# class is exactly STEP run over every tracked text file except the ones no
# step-0 tree contains and that record history (the plan, the migration
# kit, bridge/docs/HISTORY.md); changed C/C++ files are clang-formatted.

# The TapHouse-synced files are byte-checked by CI and change only through
# taphouse; a rule that matches their prose (STYLE.md's macro example)
# leaves a reviewed G14 residual instead.
APPLY_SKIP = ["docs/migration/*", "docs/MONOREPO_PLAN.md", "bridge/docs/HISTORY.md", "submodules/*",
              "STYLE.md", ".clang-format", ".clang-tidy", ".pre-commit-config.yaml", "scripts/tidy.sh",
              ".claude/hooks/session-start.sh"]


def apply(args) -> int:
    tree = pathlib.Path(args.tree).resolve()
    step_ = args.step
    files = subprocess.run(["git", "-C", str(tree), "ls-files", "-z"], check=True,
                           capture_output=True).stdout.decode().split("\0")
    files = [f for f in files if f and (tree / f).is_file()]
    moved = 0
    for f in files:
        if any(fnmatch.fnmatch(f, g) for g in APPLY_SKIP):
            continue
        new = map_later_paths(f, step_)
        if new == f:
            continue
        if new is None:
            subprocess.run(["git", "-C", str(tree), "rm", "-q", f], check=True)
        else:
            (tree / new).parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "-C", str(tree), "mv", f, new], check=True)
        moved += 1
    files = subprocess.run(["git", "-C", str(tree), "ls-files", "-z"], check=True,
                           capture_output=True).stdout.decode().split("\0")
    changed_cxx, edited = [], 0
    for f in files:
        if not f or any(fnmatch.fnmatch(f, g) for g in APPLY_SKIP):
            continue
        path = tree / f
        if not path.is_file():
            continue
        blob = path.read_bytes()
        if not is_text(blob):
            continue
        text = blob.decode("utf-8", errors="surrogateescape")
        new = text
        for rule in SUBS:
            st, pat, rep, *scope = rule
            if st != step_:
                continue
            if scope and scope[0].startswith("path:"):
                if not fnmatch.fnmatch(f, scope[0][5:]):
                    continue
            elif scope and (scope[0] == "cmake") != is_cmake(f):
                continue
            new = re.sub(pat, rep, new)
        if step_ == "3.8":
            new = banner(new, f)
        if new != text:
            path.write_bytes(new.encode("utf-8", errors="surrogateescape"))
            edited += 1
            if pathlib.PurePosixPath(f).suffix in CXX_SUFFIXES:
                changed_cxx.append(str(path))
    if changed_cxx and not args.no_format:
        subprocess.run([shutil.which("clang-format") or "clang-format", "-i", "--style=file"]
                       + changed_cxx, check=True, cwd=tree)
    print(f"apply {step_}: {moved} path(s) moved or removed, {edited} file(s) edited, "
          f"{len(changed_cxx)} C/C++ file(s) formatted")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("build", "check"):
        p = sub.add_parser(name)
        p.add_argument("--through", required=True, choices=STEPS + ["4"])
        p.add_argument("--s0-repo", default=os.environ.get("S0_REPO", "old-async"))
        p.add_argument("--r0-repo", default=os.environ.get("R0_REPO", "old-ratio"))
        p.add_argument("--no-format", action="store_true")
        if name == "build":
            p.add_argument("--out", required=True)
        else:
            p.add_argument("--tree", default=".")
            p.add_argument("--allow")
    p = sub.add_parser("apply", help="apply one rename class to a working tree")
    p.add_argument("--step", required=True, choices=STEPS)
    p.add_argument("--tree", default=".")
    p.add_argument("--no-format", action="store_true")
    args = ap.parse_args()
    if args.cmd == "apply":
        return apply(args)
    if args.cmd == "build":
        build(args)
        return 0
    return check(args)


if __name__ == "__main__":
    sys.exit(main())
