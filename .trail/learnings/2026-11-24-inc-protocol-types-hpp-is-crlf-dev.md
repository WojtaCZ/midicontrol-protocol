---
title: inc/protocol/types.hpp is CRLF; an LF rewrite turns every merge into a whole-file conflict
author: Dev
date: 2026-11-24
branch: main
cost: { minutes: 31, tokens: 164526 }
id: l-04856a7a
scope: repo
status: active
src: agent
touches:
  - PROTOCOL.md
  - inc/protocol/pairing.hpp
  - inc/protocol/types.hpp
---

## Watch out
inc/protocol/types.hpp has CRLF line endings. On 2026-11-24 the D3 pairing worker (backlog/pair-1124) saved it as LF, so rebasing onto the abort commit conflicted on all 170 lines while the real change was 5 added enum lines (PAIR_REQ..PAIR_CONFIRM 0x90..0x92).
Check with `file inc/protocol/types.hpp` before committing. Resolve such conflicts with `git diff -w --ignore-cr-at-eol <c>^ <c>`, taking ours and re-adding only the real lines with CRLF.
## Decided
Worktrees (wt-*-pair) cannot fetch unpushed submodule commits from GitHub. Fetch them from the sibling local repo instead: `git -C lib/protocol fetch ../midicontrol-protocol <branch>`. For lib/ui, fetch from the main checkout's lib/ui.
