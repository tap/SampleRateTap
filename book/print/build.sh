#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
#
# Builds the book as a PDF: mdBook (with this directory's backend) -> one
# Markdown file -> pandoc -> Typst -> PDF. The excerpts are the same live
# includes the HTML build carries, because mdBook expands them before any
# renderer runs; the title page states the tree the PDF was built from.
#
# Usage:  book/print/build.sh [out.pdf]          (from the repository root)
# Needs:  mdbook, pandoc (>= 3.1, for its typst writer), typst, python3.
#         MDBOOK, PANDOC and TYPST name the binaries when they are not on
#         PATH (CI pins them by SHA256 and sets these).
# Output: the PDF (default book/print-build/SampleRateTap.pdf); with
#         PRINT_PREVIEW=<pages> (typst page syntax, e.g. "1-4,120") also one
#         PNG per listed page beside it, for looking at the result.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MDBOOK="${MDBOOK:-mdbook}"
PANDOC="${PANDOC:-pandoc}"
TYPST="${TYPST:-typst}"
BUILD="$ROOT/book/print-build"
OUT="${1:-$BUILD/SampleRateTap.pdf}"

cd "$ROOT"
rm -rf "$BUILD"

# mdBook with the print backend added for this run only (the checked-in
# book.toml keeps the HTML backend alone, so the site and CI builds are
# untouched); its outputs go to a build directory of their own.
MDBOOK_BUILD__BUILD_DIR="$BUILD" \
MDBOOK_OUTPUT__PRINT__COMMAND="python3 $ROOT/book/print/backend.py" \
  "$MDBOOK" build book 2>&1 | tee "$BUILD.log" >&2
if grep -qiE 'warning|error' "$BUILD.log"; then
  echo "::error::mdbook reported warnings/errors (stale anchor or broken include?)" >&2
  exit 1
fi
rm -f "$BUILD.log"

# The tree this PDF states on its title page.
built_from="$(git -C "$ROOT" log -1 --format='%h, %cs' 2>/dev/null || echo "unknown tree")"
if ! git -C "$ROOT" diff --quiet -- book 2>/dev/null; then built_from="$built_from, with local changes"; fi

cd "$BUILD/print"
"$PANDOC" book.md \
  --from markdown+pipe_tables+inline_notes+strikeout-smart \
  --to typst \
  --template "$ROOT/book/print/template.typ" \
  --toc --toc-depth=2 \
  --metadata title="SampleRateTap" \
  --metadata subtitle="The Story of a Sample Rate Converter Family" \
  --metadata author="The SampleRateTap project" \
  --metadata date="Built from $built_from" \
  --variable papersize=us-letter \
  --output book.typ
"$TYPST" compile --root "$BUILD" book.typ "$OUT"
echo "wrote $OUT ($(stat -c %s "$OUT") bytes)" >&2

if [ -n "${PRINT_PREVIEW:-}" ]; then
  "$TYPST" compile --root "$BUILD" --format png --ppi 110 --pages "$PRINT_PREVIEW" \
    book.typ "$(dirname "$OUT")/page-{p}.png"
  echo "preview pages $PRINT_PREVIEW beside the PDF" >&2
fi
