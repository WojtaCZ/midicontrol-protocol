# midicontrol-protocol

Packet protocol shared by the MIDIcontrol system: the PC software, the base (STM32G431) and the remote (STM32G071).
The same framing carries USB-CDC (PC to base), the base-remote BLE link and the firmware update.

## Contents

| Path | What |
|---|---|
| `PROTOCOL.md` | Authoritative description of the wire format: framing, byte stuffing, CRC-8, message types, NAK reasons, firmware update messages. |
| `INDICATOR-BUS.md` | The church indicator bus (physical layer, 9-byte frame) and its mapping to `NOW_PLAYING_INFO` and the display SysEx. Translation of the owner's V2 document kept in `docs/indicator-bus/`. |
| `inc/protocol/`, `src/` | C++ codec for firmware: `encode`, `decodeFrame`, the streaming `Parser`, CRC-8, message and NAK types, constants, `info` (tag/length/value status) and `diag` records. |
| `docs/SYSTEM.md`, `docs/HARDWARE-*.md` | Generated copies of the system spec and hardware notes. Do not edit here; the main copy is `midicontrolpy-sw/docs/SYSTEM.md`, synchronised by `midicontrolpy-sw/tools/sync-system-doc.sh`. |
| `CLAUDE.md` | Working notes for the AI assistant in this repo. |

The repo has no tests of its own. The PC software tests its own codec; the firmware repos exercise this one on the bench.

## How other repos use it

- **midicontrol-fw (base)** and **midicontroller-fw (remote)** vendor this repo as the git submodule `lib/protocol`
  (`git@github.com:WojtaCZ/midicontrol-protocol.git`). Never edit the submodule copy: change this repo, then bump the
  submodule pointer in the firmware repo.
- **midicontrol-bootloader** builds its host tests against these headers (`../midicontrol-protocol/inc`, or
  `-DPROTOCOL_INC=...`).
- **midicontrolpy-sw (PC)** does not link this code. It has its own C++ implementation in `src/protocol/`
  (`crc8`, `codec`, `parser`), which must stay wire-compatible with this one. When you change the wire format, change
  `PROTOCOL.md` first, then both implementations.

## Build

Header plus a few source files (`src/*.cpp`), C++20, no exceptions or RTTI, no dependencies. Add `inc` to the include
path and compile `src/*.cpp` into your target.
