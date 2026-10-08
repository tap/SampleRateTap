// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The pandoc template for the print build (book/print/build.sh): pandoc
// renders the backend's book.md to Typst through this file, and typst sets
// the PDF. Parts are level-1 headings (a title page each), chapters level 2
// (a page each, with a running head), sections level 3 and below. The fonts
// are the ones typst embeds — Libertinus Serif for text, DejaVu Sans Mono for
// code — so the build needs no font installation and renders the book's
// Unicode (arrows, ceilings, sub- and superscripts) the same everywhere.

#let conf(
  title: none,
  subtitle: none,
  authors: (),
  date: none,
  paper: "us-letter",
  toc: false,
  toc-depth: 2,
  doc,
) = {
  set document(title: title, author: authors.map(a => a.name))
  set page(
    paper: paper,
    margin: (top: 1in, bottom: 1in, inside: 1.1in, outside: 0.9in),
    numbering: "1",
    number-align: center,
    header: context {
      // The running head: the current chapter, except on a chapter's first page.
      let here-page = here().page()
      let starts = query(selector(heading.where(level: 1)).or(heading.where(level: 2)))
        .filter(h => h.location().page() == here-page)
      if starts.len() > 0 { return }
      let before = query(selector(heading.where(level: 2)).before(here()))
      if before.len() > 0 {
        set text(size: 9pt, fill: luma(90))
        emph(before.last().body)
      }
    },
  )
  set text(font: ("Libertinus Serif",), size: 10.5pt, lang: "en")
  set par(justify: true, leading: 0.62em)
  set heading(numbering: none)

  // Code: the embedded monospace; inline code follows the surrounding size,
  // block code is 8pt and wraps at the margin (the house style's 100-column
  // lines fit unwrapped; a longer line of tool output folds).
  show raw: set text(font: ("DejaVu Sans Mono",), size: 0.88em)
  show raw.where(block: true): it => block(
    width: 100%, fill: luma(247), inset: (x: 8pt, y: 7pt), radius: 2pt, breakable: true,
    text(size: 8pt, it),
  )

  // Parts: a page of their own.
  show heading.where(level: 1): it => {
    pagebreak(weak: true)
    v(32%)
    align(center, text(size: 26pt, weight: "bold", it.body))
    pagebreak()
  }
  // Chapters: start a page.
  show heading.where(level: 2): it => {
    pagebreak(weak: true)
    v(1.2em)
    text(size: 21pt, weight: "bold", it.body)
    v(0.9em)
  }
  show heading.where(level: 3): it => { v(0.6em); text(size: 13.5pt, weight: "bold", it.body); v(0.3em) }
  show heading.where(level: 4): it => { v(0.4em); text(size: 11pt, weight: "bold", it.body); v(0.2em) }

  // Epigraphs and quotations.
  show quote.where(block: true): it => pad(left: 1.6em, right: 1.6em, text(size: 10pt, it))

  // Tables: a rule under the header row and hairlines between rows.
  set table(inset: 5pt, stroke: (x, y) => if y == 0 { (bottom: 0.6pt) } else { (bottom: 0.3pt + luma(200)) })
  show table: set text(size: 9pt)
  show table: set align(left + top)
  show figure.where(kind: table): set figure.caption(position: top)

  show link: set text(fill: rgb("#1a4f9c"))
  show footnote.entry: set text(size: 8.5pt)

  // Title page.
  page(numbering: none, header: none)[
    #v(28%)
    #align(center)[
      #text(size: 34pt, weight: "bold", title)
      #v(0.8em)
      #if subtitle != none { text(size: 17pt, subtitle) }
      #v(2.5em)
      #for a in authors { text(size: 12pt, a.name); linebreak() }
      #v(1em)
      #if date != none { text(size: 10pt, fill: luma(90), date) }
    ]
  ]
  if toc {
    // Set as a chapter: its own page, the running head on the pages after.
    heading(level: 2, outlined: false)[Contents]
    outline(title: none, depth: toc-depth, indent: 1.2em)
  }
  doc
}

$if(smart)$
$else$
#set smartquote(enabled: false)
$endif$

#show: doc => conf(
$if(title)$
  title: [$title$],
$endif$
$if(subtitle)$
  subtitle: [$subtitle$],
$endif$
$if(author)$
  authors: (
$for(author)$
    ( name: "$author$", ),
$endfor$
  ),
$endif$
$if(date)$
  date: [$date$],
$endif$
$if(papersize)$
  paper: "$papersize$",
$endif$
$if(toc)$
  toc: true,
  toc-depth: $toc-depth$,
$endif$
  doc,
)

$body$
