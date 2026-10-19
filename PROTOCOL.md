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

## Frame content (unescaped)

| Offset | Field    | Size     | Notes |
|--------|----------|----------|-------|
| 0      | Length   | 1 B      | `4 + payloadLen` (header + payload, excludes itself and CRC) |
| 1      | Source   | 1 B      | |
| 2      | Dest     | 1 B      | |
| 3      | PacketId | 1 B      | one 8-bit counter per node, shared by all messages it sends |
| 4      | MsgType  | 1 B      | |
| 5..    | Payload  | 0–128 B  | |
| last   | CRC-8    | 1 B      | CRC-8/MAXIM (poly 0x31, init 0x00) over Length..Payload |

Escaping applies to Length through CRC. Worst case on the wire is `2 + 134 * 2 = 270` bytes
(`MAX_FRAME_SIZE`). Receivers must bounds-check the unescaped length; frames that do not fit are dropped.

## Node addresses

| Value | Node      |
|-------|-----------|
| 0x01  | PC        |
| 0x02  | Base      |
| 0x03  | Remote    |
| 0xFF  | Broadcast |

---

## Routing

- The **base forwards by destination**: a frame from USB whose `Dest` is the remote goes out over BLE,
  a frame from BLE whose `Dest` is the PC goes out over USB-CDC. Only `Dest = Base` is handled locally.
- The **remote addresses the PC directly** for playback and song-list commands (`PLAY_SONG`, `STOP_SONG`,
  `REPLAY_ANCHOR`, `GET_SONG_LIST`). The PC is the player; the base holds no song data.
- The **PC sends playback signals to the remote** (`SIGNAL_*`). The base may observe signals it forwards to
  keep its own OLED in step, but it does not consume them.
- If the base cannot forward (BLE link down, PC not enumerated) it answers the sender with
  `NAK UNREACHABLE` so the sender stops retrying.

## Reliability

- ACKs are **end to end**: the destination node ACKs, the base never ACKs a frame it only forwards.
- Every message requires `ACK` or `NAK` **except** `ACK`, `NAK`, `KEEPALIVE`, `KEEPALIVE_ACK`,
  `NOW_PLAYING_INFO` and broadcasts. A response message (e.g. `SONG_LIST_RESP`, `STATUS_RESP`) is sent
  in addition to the ACK of the request.
- The sender retransmits the **same PacketId** after `RETRY_TIMEOUT_MS` (500 ms), at most `MAX_RETRIES` (3)
  times. A receiver that sees the same `(Source, PacketId)` as the previous frame ACKs it again but does not
  execute it twice.
- Unknown `MsgType` is answered with `NAK UNKNOWN_MSG`, a bad payload with `NAK INVALID_PAYLOAD`.
- `KEEPALIVE` is sent every 2 s by the PC to the base and by the base to the remote; the peer answers
  `KEEPALIVE_ACK`. A peer is considered lost after 6 s without an answer.

---

## Message types

### Acknowledgement (0x0x)

| Type | Code | Direction     | Payload                                     |
|------|------|---------------|---------------------------------------------|
| ACK  | 0x01 | any → any     | `[ackedPacketId]`                           |
| NAK  | 0x02 | any → any     | `[ackedPacketId, nakReason]`                |

NAK reasons: `0x01` unknown msg, `0x02` invalid payload, `0x03` CRC error, `0x04` busy, `0x05` not playing, `0x06` unreachable (base could not forward), `0x07` not found (unknown song code).

---

### Playback commands (0x1x)

| Type             | Code | Direction       | Payload |
|------------------|------|-----------------|---------|
| PLAY_SONG        | 0x10 | Remote/Base → PC | ASCII song code, 1–8 chars `[0-9A-Z]`, e.g. `"406A"`. No terminator required; a trailing `0x00` is tolerated |
| STOP_SONG        | 0x11 | Remote/Base → PC | *(empty)* or `[mode]`: `0x00` stop, `0x01` pause (the PC may treat pause as stop) |
| RECORD_SONG      | 0x12 | –               | *(reserved)* |
| NOW_PLAYING_INFO | 0x13 | PC → Base       | see below |
| REPLAY_ANCHOR    | 0x14 | Remote → PC     | *(empty)* — **add verse** |

The song code is also the file name on the PC (`406A.mid`). A 16-bit number is not used because it cannot
express letter variants (406A / 406B). The PC answers an unknown code with `NAK NOT_FOUND`.

**REPLAY_ANCHOR (add verse)** means "play one more verse": the current verse is played to its end, then
played once more before playback continues. Each press adds one extra pass; presses accumulate.
The PC answers `NAK NOT_PLAYING` when nothing is playing.

**NOW_PLAYING_INFO** drives the church indicator through the base. Not ACKed.

| Byte | Field   | Encoding |
|------|---------|----------|
| 0    | song_h  | BCD, digits 3 and 2 (high nibble = leftmost digit) |
| 1    | song_l  | BCD, digits 1 and 0 |
| 2    | verse   | BCD, tens and units |
| 3    | letter  | `0x0A`–`0x0D` = A–D, `0x0F` = none |
| 4    | led     | `0x00`/`0x0F` off, `0x01` red, `0x02` green, `0x03` blue, `0x04` yellow |

- A BCD nibble `0xF` is a **blank digit**, so digits are shown exactly where they are placed
  (`" 401"` = `0xF4 0x01`, `"513 "` = `0x51 0x3F`, verse `"1 "` = `0x1F`).
- `0xFF` in a field means **no change**. For the song, it is unchanged only when **both** `song_h` and `song_l`
  are `0xFF`, so a short number such as `"  12"` (`0xFF 0x12`) is still expressible.
- `0xFE` in `song_h` (with any `song_l`) or in `verse` **blanks** that field (e.g. verse blank during the prelude).
- An **empty payload clears** the whole indicator (all digits blank, no letter, LED off).

---

### Song list (0x2x)

| Type           | Code | Direction              | Payload |
|----------------|------|------------------------|---------|
| GET_SONG_LIST  | 0x20 | Remote/Base → PC       | *(empty)* or `[offset]` — index of the first entry, default 0 |
| SONG_LIST_RESP | 0x21 | PC → requester         | `[hasMore, "code;code;..."]` — ASCII codes separated by `;`, no terminator |

The PC fills a page up to the payload limit; the requester asks for the next page with
`offset = previous offset + entries received` while `hasMore != 0`.

---

### Playback signals (0x3x) — PC → Remote

| Type             | Code | Payload |
|------------------|------|---------|
| SIGNAL_PLAYING   | 0x30 | ASCII song code that started playing |
| SIGNAL_RECORDING | 0x31 | *(reserved)* |
| SIGNAL_STOPPED   | 0x32 | *(empty)* — playback ended or was stopped |
| SIGNAL_ANCHOR    | 0x33 | null-terminated ASCII label, max 127 bytes + `\0` |

**SIGNAL_ANCHOR** is sent every time playback enters a new section, e.g. `"Predehra"` (prelude),
`"Sloka 2"`, or `"Sloka 2 +1"` while add-verse repeats are pending. Labels are ASCII so a small OLED
font can draw them. The remote enables the add-verse key after the first anchor.

---

### Organ power (0x4x) — PC/Remote → Base

| Type        | Code | Payload |
|-------------|------|---------|
| SET_CURRENT | 0x40 | `[on]` — `0x01` switch the SSR (organ mains) on, `0x00` off |

---

### Status (0x5x)

| Type        | Code | Payload |
|-------------|------|---------|
| GET_STATUS  | 0x50 | *(empty)* |
| STATUS_RESP | 0x51 | `[bleConnected, ssrOn, displayConnected, pcAlive]`, one byte each, `0`/`1` |

---

### Keepalive (0x6x)

| Type          | Code | Payload   |
|---------------|------|-----------|
| KEEPALIVE     | 0x60 | *(empty)* |
| KEEPALIVE_ACK | 0x61 | *(empty)* |

PC → Base and Base → Remote every 2 s; the receiver answers `KEEPALIVE_ACK`. Neither is ACKed.

---

### Firmware update (0x7x) — PC → Base or Remote

| Type             | Code | Payload                                              |
|------------------|------|------------------------------------------------------|
| FW_UPDATE_START  | 0x70 | `[target(1), size(4 LE), crc32(4 LE), version[...]]`|
| FW_UPDATE_DATA   | 0x71 | `[offset(4 LE), chunk(≤120 B)]`                     |
| FW_UPDATE_END    | 0x72 | *(empty)* — finalize, verify, apply                  |
| FW_UPDATE_STATUS | 0x73 | `[statusCode]` — device → PC progress/result         |
| ENTER_BOOTLOADER | 0x74 | *(empty)* — base reboots into its USB-CDC bootloader |

FW_UPDATE_STATUS codes: `0x00` ready, `0x01` in-progress, `0x02` CRC OK (applying), `0x03` CRC fail, `0x04` write error, `0x05` bad size.

The base updates over **USB-CDC** with this custom protocol, not USB DFU: the app handles
`ENTER_BOOTLOADER`, sets a magic word in a TAMP backup register and resets into the bootloader.
`FW_UPDATE_DATA` chunks for the base must be a **multiple of 8 bytes** (flash double-word programming),
except the last one, which the bootloader pads with `0xFF`. The remote stages the image in its external
flash; `FW_UPDATE_START` may take several seconds there (sector erase), so the PC should wait up to 15 s
for the first `FW_UPDATE_STATUS`.

---

### Diagnostics (0x8x) — PC → node, node → PC

| Type      | Code | Direction     | Payload |
|-----------|------|---------------|---------|
| DIAG_GET  | 0x80 | PC → node     | `[cursor u16 LE]` — ACKed |
| DIAG_RESP | 0x81 | node → PC     | `[boot_id u16 LE, next_cursor u16 LE, n u8, n × record]`, `n` ≤ 8 (≤ 101 B) — sent in addition to the ACK, like `STATUS_RESP` |

The node keeps a ring of diagnostic records (see `inc/protocol/diag.hpp`). `DIAG_GET` asks for the records whose
`seq` is at or after `cursor`; the response carries up to 8 of them and `next_cursor` (the `seq` after the last
record returned, or the node's next `seq` when nothing is new). `boot_id` changes on every boot of the node, so the
PC can tell a reset from a cursor wrap and restarts at cursor 0 when it changes. Records the ring has already
overwritten are skipped; the node then inserts a `RECORDS_LOST` record with the number dropped.

**Record** — 12 B, little-endian:

| Offset | Field     | Size | Notes |
|--------|-----------|------|-------|
| 0      | seq       | u16  | increments per record since boot |
| 2      | code      | u8   | table below |
| 3      | level     | u8   | `1` error, `2` warning, `3` info, `4` debug |
| 4      | uptime_ms | u32  | ms since boot |
| 8      | arg       | u32  | meaning depends on `code` |

**Codes**

| Code | Name              | arg |
|------|-------------------|-----|
| 1    | BOOT              | reset cause |
| 2    | FAULT             | fault kind (node specific) |
| 3    | FAULT_DETAIL      | follows FAULT: pc, lr, xpsr, ipsr as four records |
| 4    | LOOP_STALL        | pc of the stuck main loop |
| 5    | IWDG_RESET        | – |
| 6    | BLE_CONNECT       | – |
| 7    | BLE_DISCONNECT    | – |
| 8    | BLE_SECURED       | – |
| 9    | LINK_LOST         | – |
| 10   | RETRIES_EXHAUSTED | msgType of the frame that was dropped |
| 11   | CRC_ERRORS        | count |
| 12   | CHARGER_FAULT     | charger REG09 |
| 13   | LOW_BATTERY       | mV |
| 14   | OTA_RESULT        | FwStatus |
| 15   | SHUTDOWN          | reason |
| 16   | RECORDS_LOST      | number of records dropped |

**Polling rule (PC).** Send `DIAG_GET` when the node's link comes up and then every 10 s while it is alive; never
during a firmware update; once a response carries `n = 0`, ask again only on the next timer tick. Use the previous
`next_cursor` as the cursor (0 on the first request and after a `boot_id` change).

**Old firmware** does not know `0x80` and answers `NAK UNKNOWN_MSG (0x01)`. The PC then stops polling that node
until it reconnects.

The base forwards both messages like any other frame (`Dest` = PC or Remote); the router needs no change.
