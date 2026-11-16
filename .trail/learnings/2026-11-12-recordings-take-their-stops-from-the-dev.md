---
title: Recordings take their stops from the recorded MIDI and cut the silence before the first note
author: Dev
date: 2026-11-12
branch: main
cost: { minutes: 31, tokens: 229346 }
id: l-181fcd68
scope: repo
status: active
src: agent
touches:
  - INDICATOR-BUS.md
  - docs/SYSTEM.md
  - docs/indicator-bus/interface-schematic.png
---

## Decided
Owner, 2026-11-12 (D29 in docs/SYSTEM.md, SYS-REC-01/05..08): recording is started and stopped from the base only (not the remote). The new file has no song number; name it from the start date/time (suggested REC_YYYY-MM-DD_HHMM.mid), the owner renames it later. Starting a recording switches the organ on. The organist sets the stops while recording: the PC parses the stop changes out of the recorded MIDI and writes <name>.conf from the registration in effect at the first note. The time between record start and the first note is cut.
## Tried and ruled out
Using the default registration (SYS-USE-20) for recorded songs: no, the organist's own stops are in the recording. Recording from the remote: not wanted.
## Watch out
Stop changes (program changes, ProgramOffset -1, octal numbering, unverified on the Opus 910) are not notes: do not treat them as the first note when trimming the leading silence, or the dead time stays in. Stop changes later in the song stay in the file.
