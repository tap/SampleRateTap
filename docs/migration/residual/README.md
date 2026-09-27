# Residual allowlists (G14)

`rename.py check --through <step>` applies the mechanical rename map to the
step-0 trees and diffs the result against the tree under test. Every hunk
left over is a *residual*: a change the map does not explain. Each must be
listed in `<step>.txt` here, and each entry is reviewed in the PR that adds
it.

Entry forms (one per line; `--` starts the reason, which is required):

    file <path-glob>  -- <reason>          every hunk in matching files
    hunk <path> <hash12>  -- <reason>      one hunk, by the hash `check` prints

Prefer `hunk` entries: a `file` entry lets any later edit to that file
through, so it is for files that are rewritten wholesale (the root
`CMakeLists.txt`, workflows) or that are expected to be hand-edited
throughout a step (prose at 3.7). Allowlists are cumulative in content but
not in effect: each step's file is complete on its own, because the check at
a step compares the whole tree.

`1c.txt` is the seed written at step 0 from the plan's 1c/2 change lists;
the 1c commit replaces its `file` entries with `hunk` entries wherever a
file changes only in a few places.
