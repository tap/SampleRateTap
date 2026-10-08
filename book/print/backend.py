#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
"""An mdBook renderer backend that writes the whole book as one Markdown file
for the print build (book/print/build.sh: this file -> pandoc -> typst).

mdBook runs it after its preprocessors, so every `{{#include path:anchor}}`
has already been expanded: the PDF's code excerpts are the same live
excerpts the HTML carries, from the same tree. What this backend changes is
only what print needs:

- part titles become level-1 headings and every chapter heading is demoted
  one level, so the document is parts > chapters > sections;
- links become print links: an external URL becomes a footnote after the
  link text, a link to another chapter becomes its text alone;
- image paths are rewritten to the copy of src/img this backend makes
  beside its output.

mdBook invokes it as `[output.print] command`, with the render context (its
`root`, `config`, `destination` and the `book` itself) as JSON on stdin.
"""

import json
import pathlib
import re
import shutil
import sys

HEADING = re.compile(r"^(#{1,6})(?=\s)", re.MULTILINE)
FENCE = re.compile(r"^(```|~~~)", re.MULTILINE)
# [text](target) that is not an image (no leading !); link text and alt text
# may run over several lines, so these run on whole prose segments, never
# line by line.
LINK = re.compile(r"(?<!\!)\[([^\]]+)\]\(([^)\s]+)\)")
IMAGE = re.compile(r"(!\[[^\]]*\]\()([^)\s]+)(\))")


def print_prose(text, chapter_dir):
    """Headings demoted a level, external links to footnotes, chapter links to
    plain text, image paths to the img/ copy."""

    def image(m):
        target = m.group(2)
        if target.startswith(("http://", "https://")):
            return m.group(0)
        resolved = (chapter_dir / target).resolve()
        return m.group(1) + "img/" + resolved.name + m.group(3)

    def link(m):
        text, target = m.group(1), m.group(2)
        if target.startswith(("http://", "https://")):
            return f"{text}^[{target}]"
        return text  # a chapter, a heading anchor: the text stands alone in print

    text = HEADING.sub(r"#\1", text)
    text = IMAGE.sub(image, text)
    return LINK.sub(link, text)


def chapter_markdown(content, chapter_dir):
    """The chapter with print_prose applied outside its code fences."""
    out = []
    prose = []
    in_fence = None
    for line in content.splitlines():
        fence = FENCE.match(line)
        if fence and in_fence is None:
            out.append(print_prose("\n".join(prose), chapter_dir))
            prose = []
            in_fence = fence.group(1)
            out.append(line)
        elif in_fence is not None:
            out.append(line)
            if fence and line.startswith(in_fence):
                in_fence = None
        else:
            prose.append(line)
    out.append(print_prose("\n".join(prose), chapter_dir))
    return "\n".join(out) + "\n"


def main():
    context = json.load(sys.stdin)
    book = context["book"]
    root = pathlib.Path(context["root"])
    src = root / context["config"]["book"].get("src", "src")
    dest = pathlib.Path(context["destination"])
    dest.mkdir(parents=True, exist_ok=True)
    if (src / "img").is_dir():
        shutil.copytree(src / "img", dest / "img", dirs_exist_ok=True)

    parts = []
    chapters = 0
    for item in book["sections"]:
        if item == "Separator":
            continue
        if isinstance(item, dict) and "PartTitle" in item:
            parts.append(f"# {item['PartTitle']}\n")
            continue
        chapter = item["Chapter"]
        if chapter.get("path") is None:  # a draft chapter: nothing to print
            continue
        chapter_dir = (src / chapter["path"]).parent
        parts.append(chapter_markdown(chapter["content"], chapter_dir))
        chapters += 1
        if chapter.get("sub_items"):
            sys.exit("print backend: nested chapters are not expected in this book")

    (dest / "book.md").write_text("\n".join(parts), encoding="utf-8")
    print(f"print backend: {chapters} chapters -> {dest / 'book.md'}", file=sys.stderr)


if __name__ == "__main__":
    main()
