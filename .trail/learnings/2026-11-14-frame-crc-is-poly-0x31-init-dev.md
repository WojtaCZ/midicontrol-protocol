---
title: "Frame CRC is poly 0x31, init 0, non-reflected (check 0xA2): not CRC-8/MAXIM"
author: Dev
date: 2026-11-14
branch: audit/step1-docs
cost: { minutes: 14, tokens: 42263 }
id: l-3aa4afd8
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - INDICATOR-BUS.md
  - PROTOCOL.md
  - inc/protocol/crc8.hpp
  - src/crc8.cpp
---

## Decided
The frame CRC in inc/protocol/crc8.hpp / src/crc8.cpp is poly 0x31, init 0x00, MSB-first (not reflected), xorout 0x00; check value over "123456789" is 0xA2. PROTOCOL.md and the code comments now describe it by these parameters (audit step 1, 2026-11-14).
## Watch out
Older docs (PROTOCOL.md, crc8 headers, the PC vendored copy, midicontrolpy-sw CLAUDE.md) called it CRC-8/MAXIM. MAXIM/Dallas is the reflected variant with check 0xA1, so a generic "CRC-8/MAXIM" library would reject every frame. Do not "fix" the code to match the old name: all three codecs (base, remote, PC) and every deployed unit use 0xA2.
