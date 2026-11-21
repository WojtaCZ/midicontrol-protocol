# CLAUDE.md: MIDIcontrol protocol

Packet protocol shared by the PC software, base and remote (submodule `lib/protocol` in both firmware repos). `PROTOCOL.md` is authoritative for the wire format; `docs/SYSTEM.md` says what the system does with it.

## Read first: system spec and hardware

`docs/SYSTEM.md` is the owner's specification of what the whole MIDIcontrol system (PC software, base, remote,
bootloader, protocol) is expected to do, with requirement IDs, owner decisions and a gap list. Its main copy lives in
`midicontrol-sw/docs/SYSTEM.md`; the copies in the other repos are kept identical by
`midicontrol-sw/tools/sync-system-doc.sh` (`--check` reports drift). Edit only the main copy. When the code and the spec
disagree, the spec says what is wanted: ask the owner instead of changing the spec to match the code.
`docs/HARDWARE-base.md` / `docs/HARDWARE-remote.md` (or `docs/HARDWARE.md` in the matching firmware repo) describe the
base (STM32G431) and remote (STM32G071) hardware. The firmware pin maps are authoritative; the KiCad repos are historical.

- **User-visible changes** (screen text, menus, keys, timeouts, web page, CLI output, config keys, install steps): in the same commit series update midicontrol-sw/docs/SYSTEM.md (status/glossary/timing; then run midicontrol-sw/tools/sync-system-doc.sh), this CLAUDE.md/README, midicontrol-sw/docs/status-web-and-service.md for the PC, the screen scenarios in tools/screens/main.cpp, and the Czech user manual in `midicontrol-sw/docs/manual/` (its `facts/*.md` too; pictures: `docs/manual/README.md`).

## Indicator bus

`INDICATOR-BUS.md` is the main reference for the church indicator bus (physical layer, 9-byte frame, mapping to
NOW_PLAYING_INFO and the display SysEx). It translates the owner's original V2 document, kept unchanged in
`docs/indicator-bus/` (`.odt` + images). Edit `INDICATOR-BUS.md` here; never edit the `.odt`.

The other files in `docs/` (SYSTEM.md, HARDWARE-*.md) are generated copies. Do not edit them here.
