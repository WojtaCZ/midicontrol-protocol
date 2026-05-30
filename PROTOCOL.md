# midicontrol-protocol

Binary packet protocol between the three nodes: **PC** (0x01), **Base** (0x02), **Remote** (0x03).

## Frame format

```
SOH  [header + payload, escaped]  EOT
0x01                               0x04
```

Bytes `0x01` (SOH), `0x04` (EOT), `0x1B` (ESC), `0x24` ($), `0x25` (%) inside the payload are escaped as `ESC 0x2X`:

| Raw byte | Escaped sequence |
|----------|-----------------|
| 0x01     | ESC 0x21        |
| 0x04     | ESC 0x22        |
| 0x1B     | ESC 0x23        |
| 0x24 ($) | ESC 0x26        |
| 0x25 (%) | ESC 0x27        |

## Header (4 bytes, unescaped meaning)

| Offset | Field      | Size |
|--------|------------|------|
| 0      | Source     | 1 B  |
| 1      | Dest       | 1 B  |
| 2      | PacketId   | 1 B  |
| 3      | MsgType    | 1 B  |

Followed by 0–128 bytes of payload. Final byte after payload: CRC-8 over the unescaped header + payload.

## Node addresses

| Value | Node      |
|-------|-----------|
| 0x01  | PC        |
| 0x02  | Base      |
| 0x03  | Remote    |
| 0xFF  | Broadcast |

---

## Message types

### Acknowledgement (0x0x)

| Type | Code | Direction     | Payload                                     |
|------|------|---------------|---------------------------------------------|
| ACK  | 0x01 | any → any     | `[ackedPacketId]`                           |
| NAK  | 0x02 | any → any     | `[ackedPacketId, nakReason]`                |

NAK reasons: `0x01` unknown msg, `0x02` invalid payload, `0x03` CRC error, `0x04` busy.

---

### Playback commands (0x1x) — Remote → Base or PC → Base

| Type          | Code | Payload                                                    |
|---------------|------|------------------------------------------------------------|
| PLAY_SONG     | 0x10 | `[codeLo, codeHi]` — 16-bit song code, little-endian      |
| STOP_SONG     | 0x11 | `[mode]` — `0x00` stop, `0x01` pause                      |
| RECORD_SONG   | 0x12 | *(reserved)*                                               |
| NOW_PLAYING_INFO | 0x13 | *(reserved)*                                            |
| REPLAY_ANCHOR | 0x14 | *(empty)* — replay the section between the last two anchors |

**REPLAY_ANCHOR** instructs the base to jump back to the position of the most recently received anchor and replay forward to the current playback position. Intended for live services: when a speaker runs long, the operator presses the designated key repeatedly to loop the current musical section.

---

### Song list (0x2x) — Remote/PC ↔ Base

| Type          | Code | Payload              |
|---------------|------|----------------------|
| GET_SONG_LIST | 0x20 | *(empty)*            |
| SONG_LIST_RESP| 0x21 | *(TBD)*              |

---

### Playback signals (0x3x) — Base → Remote

| Type             | Code | Payload                                                   |
|------------------|------|-----------------------------------------------------------|
| SIGNAL_PLAYING   | 0x30 | *(empty)*                                                 |
| SIGNAL_RECORDING | 0x31 | *(empty)*                                                 |
| SIGNAL_STOPPED   | 0x32 | *(empty)*                                                 |
| SIGNAL_ANCHOR    | 0x33 | null-terminated UTF-8 string, max 127 bytes + `\0`        |

**SIGNAL_ANCHOR** is sent by the base whenever playback crosses a named anchor point in a song. The remote displays the anchor name and enables the replay function. Anchors are defined in the song data on the PC/base side; the remote has no knowledge of their positions.

Example anchor names: `"Verse 1"`, `"Chorus"`, `"Bridge"`, `"Interlude"`.

The remote ACKs this message. If no anchor has been received since playback started, REPLAY_ANCHOR is silently ignored by the base.

---

### Current position (0x4x)

| Type        | Code | Payload   |
|-------------|------|-----------|
| SET_CURRENT | 0x40 | *(TBD)*   |

---

### Status (0x5x)

| Type        | Code | Payload   |
|-------------|------|-----------|
| GET_STATUS  | 0x50 | *(empty)* |
| STATUS_RESP | 0x51 | *(TBD)*   |

---

### Keepalive (0x6x)

| Type          | Code | Payload   |
|---------------|------|-----------|
| KEEPALIVE     | 0x60 | *(empty)* |
| KEEPALIVE_ACK | 0x61 | *(empty)* |

Sent by the base periodically. Remote replies with KEEPALIVE_ACK; no ACK/NAK otherwise.

---

### Firmware update (0x7x) — PC → Remote

| Type             | Code | Payload                                              |
|------------------|------|------------------------------------------------------|
| FW_UPDATE_START  | 0x70 | `[target(1), size(4 LE), crc32(4 LE), version[...]]`|
| FW_UPDATE_DATA   | 0x71 | `[offset(4 LE), chunk(≤120 B)]`                     |
| FW_UPDATE_END    | 0x72 | *(empty)* — finalize, verify, apply                  |
| FW_UPDATE_STATUS | 0x73 | `[statusCode]` — device → PC progress/result         |
| ENTER_BOOTLOADER | 0x74 | *(empty)* — base reboots into USB DFU bootloader     |

FW_UPDATE_STATUS codes: `0x00` ready, `0x01` in-progress, `0x02` CRC OK (applying), `0x03` CRC fail, `0x04` write error, `0x05` bad size.
