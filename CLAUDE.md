# CLAUDE.md: MIDIcontrol protocol

Packet protocol shared by the PC software, base and remote (submodule `lib/protocol` in both firmware repos). `PROTOCOL.md` is authoritative for the wire format; `docs/SYSTEM.md` says what the system does with it.

## Read first: system spec and hardware

`docs/SYSTEM.md` is the owner's specification of what the whole MIDIcontrol system (PC software, base, remote,
bootloader, protocol) is expected to do, with requirement IDs, owner decisions and a gap list. Its main copy lives in
`midicontrolpy-sw/docs/SYSTEM.md`; the copies in the other repos are kept identical by
`midicontrolpy-sw/tools/sync-system-doc.sh` (`--check` reports drift). Edit only the main copy. When the code and the spec
disagree, the spec says what is wanted: ask the owner instead of changing the spec to match the code.
`docs/HARDWARE-base.md` / `docs/HARDWARE-remote.md` (or `docs/HARDWARE.md` in the matching firmware repo) describe the
base (STM32G431) and remote (STM32G071) hardware. The firmware pin maps are authoritative; the KiCad repos are historical.

The files in `docs/` here are generated copies. Do not edit them here.
