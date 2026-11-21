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
| last   | CRC-8    | 1 B      | CRC-8 over Length..Payload: poly `0x31`, init `0x00`, MSB first (no reflection), no final XOR; check value `0xA2` for ASCII `123456789`. **Not** CRC-8/MAXIM (that is the reflected form, check `0xA1`); no reveng catalogue name matches exactly. Example: `07 01 02 0A 10 61 62 00` has CRC `0x59` |

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
  `NAK UNREACHABLE` so the sender stops retrying. If the link is usable (BLE connected, stream open, peer accepted; or the PC enumerated) but the TX path is briefly occupied (e.g. the BLE module is in command mode), it answers `NAK BUSY` instead and the sender retries.

## Reliability

- ACKs are **end to end**: the destination node ACKs, the base never ACKs a frame it only forwards.
- Every message requires `ACK` or `NAK` **except** `ACK`, `NAK`, `KEEPALIVE`, `KEEPALIVE_ACK`,
  `NOW_PLAYING_INFO` and broadcasts. A response message (e.g. `SONG_LIST_RESP`, `STATUS_RESP`) is sent
  in addition to the ACK of the request.
- The sender retransmits the **same PacketId** after `RETRY_TIMEOUT_MS` (500 ms), at most `MAX_RETRIES` (3)
  times. A receiver remembers the last 8 PacketIds per source (a window); a frame whose `(Source, PacketId)` is anywhere in
  that window is a retry: it is ACKed again but not executed twice. A new PC session is announced by the nonce in
  `KEEPALIVE` (see below): the receiver then forgets the window of the PC.
- Unknown `MsgType` is answered with `NAK UNKNOWN_MSG`, a bad payload with `NAK INVALID_PAYLOAD`.
- `NAK BUSY` is not final *(implemented on branch audit/step2-review, pending owner decision Q-D-8)*: the
  sender (base or remote) keeps the entry and resends with the **same PacketId**, inside the same
  `MAX_RETRIES` budget. The base retries only frames it originated; a frame it forwarded is retried by its
  originator.
- Supersede *(implemented on audit/step2-review, pending owner decision Q-D-9, Q-D-6, Q-D-7)*: a sender may drop an
  older un-ACKed frame when a newer one makes it meaningless; the dropped PacketId is simply never resent.
  - A newer `SET_CURRENT` supersedes older pending `SET_CURRENT` to the same node.
  - Signal group 2 = `SIGNAL_PLAYING`, `SIGNAL_RECORDING`, `SIGNAL_STOPPED`: a newer one supersedes older pending
    ones of the group, and also a pending `SIGNAL_ANCHOR`.
  - Signal group 3 = `SIGNAL_INFO` (v2): a newer one supersedes older pending ones of the group only.
- Remote STOP displacement (RF-11) *(implemented on audit/step2-review, pending owner decision Q-RF-4)*: a
  `STOP_SONG` from the remote always gets a send slot; when all slots are busy the remote drops its least
  valuable pending request, which then ends as if answered `NAK BUSY` (no further resend).
- `KEEPALIVE` is sent every 2 s by the PC to the base and by the base to the remote; the peer answers
  `KEEPALIVE_ACK`. A peer is considered lost after 6 s without an answer.

---

## Protocol version

`PROTOCOL_VERSION` (`inc/protocol/constants.hpp`, currently **2**) is reported by each node in `INFO_RESP` as tag
`PROTO_VER` (0x08, u8). A node that does not send the tag speaks v1 (the wire format before the version existed).
The PC reads it from `GET_INFO` per peer and enables each new behaviour only when the peers involved report v2 (a feature
between two peers is active only when both ends are v2; the base relays). Base v2 + remote v1 and the reverse are valid.
All v2 items are additive: nothing changes meaning, nothing is removed. v2 so far: session nonce in `KEEPALIVE`, duplicate window, `ORGAN_SRC` INFO tag, `SIGNAL_INFO` (0x34).

---

## Message types

### Acknowledgement (0x0x)

| Type | Code | Direction     | Payload                                     |
|------|------|---------------|---------------------------------------------|
| ACK  | 0x01 | any → any     | `[ackedPacketId]`                           |
| NAK  | 0x02 | any → any     | `[ackedPacketId, nakReason]`                |

NAK reasons: `0x01` unknown msg, `0x02` invalid payload, `0x03` CRC error, `0x04` busy (receiver or the base's TX path momentarily occupied, retry later; sent by the base when forwarding while the link is up), `0x05` not playing, `0x06` unreachable (base could not forward), `0x07` not found (unknown song code), `0x08` no transfer (protocol v2: `FW_UPDATE_DATA` / `FW_UPDATE_END` arrived while no firmware transfer is active, e.g. the device reset or timed out after `START`; unlike `BUSY` it is final: the sender must not retry, the update has to be started again; a v1 sender treats the unknown reason as final too).

---

### Playback commands (0x1x)

| Type             | Code | Direction       | Payload |
|------------------|------|-----------------|---------|
| PLAY_SONG        | 0x10 | Remote/Base → PC | ASCII song code, e.g. `"406A"` (rule below; the frame bound is `MAX_SONG_CODE_LEN` = 8 chars). No terminator required; a trailing `0x00` is tolerated |
| STOP_SONG        | 0x11 | Remote/Base → PC | *(empty)* or `[mode]`: `0x00` stop, `0x01` pause (the PC may treat pause as stop) |
| RECORD_SONG      | 0x12 | Base → PC       | `[0x01]` start, `[0x00]` stop *(protocol v2, A7)*; empty = start. ACKed; see "Recording" below |
| NOW_PLAYING_INFO | 0x13 | PC → Base       | see below |
| REPLAY_ANCHOR    | 0x14 | Remote → PC     | *(empty)* — **add verse** |

The song code is also the file name on the PC (`406A.mid`). A 16-bit number is not used because it cannot
express letter variants (406A / 406B). The PC answers an unknown code with `NAK NOT_FOUND`.

**Song code rule** (owner decision Q1-2 a, 2026-11-14): 1–4 digits, value at most 1999, plus an optional letter
`A`–`D`. Every node checks it; the PC answers a `PLAY_SONG` that breaks the rule with `NAK INVALID_PAYLOAD`.
`MAX_SONG_CODE_LEN` (8) stays the frame bound for the payload length, not the rule. Open: whether `"012"` and `"12"`
are the same code (pending, Q-W2-8).

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
- A **1-byte payload `[0x01]`** (`NPI_CLEAR_IF_OWN`, protocol v2) clears the indicator **only if this system wrote the
  number last**. The base tracks the writer: its own writes (NOW_PLAYING_INFO, menu) mark the bus "ours", any accepted
  frame received from the bus marks it "foreign" (another master, e.g. a manual controller, wrote after us). On a
  foreign number the base keeps it and does nothing; the message is not ACKed in either case. The PC sends this form
  when a song stops or ends and only to a base that reports `PROTO_VER >= 2`; to a v1 base it sends the empty payload,
  because a v1 base ignores every payload shorter than 5 bytes and would never clear. Any other length below 5 is
  ignored. The base's own conditional rule also applies when it clears the indicator after a PC loss.

---

### Song list (0x2x)

| Type           | Code | Direction              | Payload |
|----------------|------|------------------------|---------|
| GET_SONG_LIST  | 0x20 | Remote/Base → PC       | *(empty)*, `[offset]` or `[offset_lo, offset_hi]` — index of the first entry in the PC's sorted list (u16 LE; 1 byte = offset < 256; empty = 0). Senders use 2 bytes only when the offset is >= 256 (protocol v2 PC; a v1 PC reads byte 0 only, so it serves just the first 256 songs correctly) |
| SONG_LIST_RESP | 0x21 | PC → requester         | `[hasMore, "code;code;..."]` — ASCII codes separated by `;`, no terminator |

The PC fills a page up to the payload limit. Paging rules:

- The requester advances its offset by the **number of codes received** in the previous response (count the
  `;`-separated entries), never by the number it displayed or kept (RF-18). A requester that holds only a window of
  the list (the base keeps 24 entries) may also request an earlier offset to scroll back.
- An offset at or past the end of the list is answered with `[0x00]` and an empty list.
- `hasMore`: bit 0 = more entries follow the page; bits 1-7 are reserved and **must be 0** (the base tests `!= 0`).
- There is no fixed cap on the list length in the protocol; it is bounded only by the song code rule (Q1-2).

---

### Recording (A7, protocol v2)

Recording starts and stops only from the base (its `Nahraj` item). The base sends `RECORD_SONG [0x01]` to the PC
(reliable, `PacketId` as usual); a v1 PC answers `NAK UNKNOWN_MSG` (no recording from the base). The PC answers:

- `NAK BUSY` when a recording is running or starting, a song is playing or about to play, or any device is being
  updated (the same check that refuses `PLAY_SONG`); the base shows its busy text and sends nothing more;
- `ACK` otherwise, and starts the recorder asynchronously (the organ warm-up takes seconds). Only when the recorder
  is really running the PC sends `SIGNAL_RECORDING` (empty, reliable, signal group 2) to the **base and the remote**
  (the remote only if it reports `PROTO_VER >= 2`; the base also observes the copy forwarded to the remote).
  If the start fails or is cancelled, the PC sends `SIGNAL_STOPPED` to the base instead.

`RECORD_SONG [0x00]` ends the recording (ACKed; a no-op when nothing records). The end of a recording is signalled by
`SIGNAL_STOPPED` to the base and the remote, also when the recorder dies on its own or the PC shuts down. While a
recording runs the PC answers `PLAY_SONG` with `NAK BUSY`, and a `STOP_SONG` **from the remote does not end it** (ACKed,
ignored): only the base ends a recording. `SIGNAL_RECORDING` is remembered and resent in the PC's remote resync like
`SIGNAL_PLAYING`.

### Playback signals (0x3x) — PC → Remote

| Type             | Code | Payload |
|------------------|------|---------|
| SIGNAL_PLAYING   | 0x30 | ASCII song code that started playing |
| SIGNAL_RECORDING | 0x31 | *(empty)* *(protocol v2, A7)* — the recorder is running; a remote ignores any payload. The end is `SIGNAL_STOPPED` |
| SIGNAL_STOPPED   | 0x32 | *(empty)* — playback ended or was stopped |
| SIGNAL_ANCHOR    | 0x33 | null-terminated ASCII label, max 127 bytes + `\0` |
| SIGNAL_INFO      | 0x34 | `[flags u8]` *(protocol v2)* — state flags of the current song/system, see below |

**SIGNAL_ANCHOR** is sent every time playback enters a new section, e.g. `"Predehra"` (prelude),
`"Sloka 2"`, or `"Sloka 2 +1"` while add-verse repeats are pending. Labels are ASCII so a small OLED
font can draw them. The remote enables the add-verse key after the first anchor.

**SIGNAL_INFO** *(protocol v2, PC or base → remote)* carries a flags byte; a receiver reads the bytes and bits it
knows and ignores the rest (longer payloads and new bits may be added later), reserved bits are sent as 0. It is an
ACKed state message; the sender always sends the complete flags byte, and the newest one supersedes older pending
`SIGNAL_INFO` (supersede group 3, independent of the group-2 playback signals: a `SIGNAL_PLAYING` neither drops nor is
dropped by it). A payload shorter than 1 byte is answered `NAK INVALID_PAYLOAD`. The base forwards it by destination
like any other type; a v1 remote answers `NAK UNKNOWN_MSG`, so the PC sends it only to a remote that reports
`PROTO_VER >= 2`.

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | `SONG_HAS_SECTIONS` | the song that plays has verse markers (sections). 0 = it has none: the remote hides add-verse and does not send `REPLAY_ANCHOR` |
| 1 | `BASE_UPDATING` | the base is about to reboot into its bootloader for a firmware update; the remote shows "Zakladna: aktualizace" instead of "Odpojeno" |
| 2-7 | | reserved, 0 |

`BASE_UPDATING` (A8) is sent by the **base** itself, from its `ENTER_BOOTLOADER` handler right after the ACK and once
more 100 ms later (no ACK wait, best effort; the base resets after 250 ms and sends no `KEEPALIVE` in between). The flags
byte of this message is `0x03` (the base does not know the song flag, so it sends the default "has sections"). A remote
that never receives it (lost, or a v1 remote that answers `NAK UNKNOWN_MSG`, which the base ignores) just shows the link
loss. The remote keeps the state until the first `KEEPALIVE` from the base after it is back, a `SIGNAL_INFO` with the bit
clear (the PC's resync) or 5 minutes, whichever is first. The PC never sets bit 1, and its flag bytes must not clear or set
it on their own account: it keeps sending bit 0 only. While any device is being updated the PC answers `PLAY_SONG` (and,
from A7, `RECORD_SONG`) with `NAK BUSY`.

Sequence: the PC sends `SIGNAL_INFO` right after `SIGNAL_PLAYING` of a song and before its first `SIGNAL_ANCHOR`, and
repeats it after the `SIGNAL_PLAYING` of a resync. The remote resets `SONG_HAS_SECTIONS` to "has sections" on
`SIGNAL_PLAYING` and on `SIGNAL_STOPPED`; a remote that never receives the message (v1 PC) keeps today's behaviour,
add-verse visible.

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
| STATUS_RESP | 0x51 | `[bleConnected, ssrOn, displayConnected, pcAlive, flags, remote_boot_id u16 LE]` (7 B, v2; a v1 base sends the first 4 B only), one byte each `0`/`1` for the first four; `bleConnected` is 1 only for a secured link (connected, stream open, peer accepted; SYS-LINK-07). `flags` bit0 = the remote's boot id is valid (the base has seen a v2 remote's `KEEPALIVE_ACK` since the link came up; `remote_boot_id` is 0 otherwise), bits 1-7 reserved 0 |
| GET_INFO    | 0x52 | *(empty)* — PC → Base or Remote, ACKed |
| INFO_RESP   | 0x53 | TLV fields `[tag, len, value[len]]...` — node → PC, sent in addition to the ACK like `STATUS_RESP` |

`INFO_RESP` carries firmware and device details for the PC's status page (authoritative tag list: `inc/protocol/info.hpp`; it also has the link-quality tags `0x14` LINK_RSSI, `0x15` LINK_PARAMS, `0x17` LINK_COUNTERS):
revision, branch, build time, dirty flag, uptime, reset cause, fault count, Bluetooth state flags, peer address,
device name, pairing state, and node-specific fields (base: display, SSR, ORGAN_SRC; remote: battery voltage, percent and icon level,
charger state, USB voltage). Values are little-endian. Every field is optional and a receiver skips unknown tags,
so new fields need no new message type. A node without `GET_INFO` (older firmware) answers `NAK UNKNOWN_MSG`.

Base tag `ORGAN_SRC` (0x22, u8, v2) says who switched the organ power on: `0` off, `1` on by the PC (`SET_CURRENT` from the PC), `2` on by the base menu, `3` on with unknown source (reserved: the SSR is always off after a base reset). It lets a restarted PC tell its own power-on from a manual one. The last switch-on wins (an explicit `SET_CURRENT 1` from the PC while the menu had it on makes the PC the owner); switching off clears it. A v1 base does not send the tag (the PC then treats a power-on as manual).

---

### Keepalive (0x6x)

| Type          | Code | Payload   |
|---------------|------|-----------|
| KEEPALIVE     | 0x60 | PC → Base: `[nonce u16 LE]` (v2; *empty* from a v1 PC); Base → Remote: `[pcAlive, rssi i8, nonce u16 LE]` (`pcAlive` `0`/`1`; `rssi` = the base's own RSSI of the BLE link in dBm, `0x80` = unknown; `nonce` = the PC's current nonce, 0 = none; an older remote reads byte 0 only) |
| KEEPALIVE_ACK | 0x61 | Remote → Base: `[rssi i8, boot_id u16 LE]` (v2; the remote's own RSSI in dBm, `0x80` = unknown; `boot_id` = the remote's boot id, the same value as `DIAG_RESP`). A v1 remote sends `[rssi]` only, an older one *(empty)*; a receiver reads the bytes that are present. Base → PC / PC → Base: *(empty)* |

PC → Base and Base → Remote every 2 s; the receiver answers `KEEPALIVE_ACK`. Neither is ACKed.
The base keeps the remote's last `boot_id` and reports it in `STATUS_RESP` (flags bit0 = valid); it forgets it when the remote is declared lost.
The PC compares it with the previous value while `bleConnected = 1`: a change (not the first value) means the remote rebooted, even if
the reboot was shorter than the 5 s `STATUS` poll and no `bleConnected` 0 → 1 edge was seen, and the PC resyncs the remote (playing state,
anchor, flags) once. The same `boot_id` after a pure BLE drop means the remote kept its state: no resync. A power loss redraws the
epoch, so two boots collide with probability 1/65536.
The **session nonce** is a 16-bit number the PC picks at process start (random, `0` is reserved for "no nonce") and keeps
until it exits; the PC sends a `KEEPALIVE` with it before any reliable frame after the port opens. A receiver that sees a non-zero
nonce different from the stored one (the first one counts) forgets the duplicate window of the PC, so a restarted PC whose first
PacketId repeats an old one is not swallowed as a duplicate. The base also forgets the stored nonce when it declares the PC lost.
A zero or missing nonce (v1 PC) never resets anything.
The base's `KEEPALIVE` to the remote carries whether the PC is alive (the same as `STATUS_RESP` `pcAlive`); the base also
sends one at once when that changes. The remote treats an empty payload (older base) as `pcAlive = 1` and a missing second byte as RSSI unknown.
`pcAlive = 0` means "PC gone". *(Implemented on audit/step2-review, pending owner decision Q-D-15:)* receiving it
(a change from 1 to 0) is the remote's only trigger to reset its player state; a BLE drop or a keepalive that ages
out does not reset it.

---

### Firmware update (0x7x) — PC → Base or Remote

| Type             | Code | Payload                                              |
|------------------|------|------------------------------------------------------|
| FW_UPDATE_START  | 0x70 | `[target(1), size(4 LE), crc32(4 LE), version[...]]`|
| FW_UPDATE_DATA   | 0x71 | `[offset(4 LE), chunk(≤120 B)]`                     |
| FW_UPDATE_END    | 0x72 | *(empty)* — finalize, verify, apply                  |
| FW_UPDATE_STATUS | 0x73 | `[statusCode]` — device → PC progress/result; the PC answers `ACK`, the device resends the same PacketId until ACKed (v2) |
| ENTER_BOOTLOADER | 0x74 | *(empty)* — base reboots into its USB-CDC bootloader |

FW_UPDATE_STATUS codes: `0x00` ready, `0x01` in-progress (**reserved, no device sends it**; receivers filter it out), `0x02` CRC OK (applying), `0x03` CRC fail, `0x04` write error, `0x05` bad size.

Receiver rules (`FwReceiver`, see the bootloader README "Firmware update protocol notes" and "Image format"):

- Every `FW_UPDATE_*` frame gets `ACK` or `NAK`; `FW_UPDATE_STATUS` follows where the step has a result: `READY` after `START`;
  `CRC_OK` / `CRC_FAIL` / `WRITE_ERROR` / `BAD_SIZE` after `END`; `WRITE_ERROR` / `BAD_SIZE` after a failed chunk.
- `FW_UPDATE_STATUS` is acknowledged like any other frame (protocol v2): the PC answers `ACK` (payload = the PacketId of the
  status). The device keeps the status until that `ACK` arrives and resends the same frame (same PacketId) every
  `RETRY_TIMEOUT_MS` (500 ms), at most `MAX_RETRIES` (3) times; a newer status replaces an unACKed older one. A missing
  `ACK` never aborts the update: after the last retry the device simply drops the status. After `CRC_OK` the device
  applies the image at once (base: waits at most 250 ms for the `ACK`; remote: reboots 500 ms after staging), so that
  last status may go unACKed. The PC treats a repeated status as a duplicate (same byte).
- `FW_UPDATE_DATA` / `FW_UPDATE_END` without an active transfer are answered `NAK NO_TRANSFER` (v2; v1 devices sent `NAK BUSY`,
  which the PC retried), except an `END` repeated after a successful commit, which is ACKed again.
- `START`'s `target` byte must equal the receiving node, else `NAK` + `BAD_SIZE`. The image size must fit the application
  region (base 94 KB = `0x17800`, remote 110 KB).
- Chunks must arrive in order. A retransmitted chunk is ACKed again and not written twice; a gap or partial overlap is
  `NAK INVALID_PAYLOAD`.
- The file is the application `.bin`, padded with `0xFF` to a multiple of 8 and followed by a 24-byte trailer; a wrong
  trailer ends in `BAD_SIZE` or `CRC_FAIL` at `END`.

The base updates over **USB-CDC** with this custom protocol, not USB DFU: the app handles
`ENTER_BOOTLOADER`, sets a magic word in a TAMP backup register and resets into the bootloader.
`FW_UPDATE_DATA` chunks for the base must be a **multiple of 8 bytes** (flash double-word programming),
except the last one, which the bootloader pads with `0xFF`. The remote stages the image in its external
flash and erases the staging area (64 KB blocks, typically under 1 s, up to ~8 s worst case) **before** it sends the `ACK` of
`FW_UPDATE_START`, so the PC should wait up to 15 s for that `ACK`; `STATUS READY` follows it.

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
| 3    | FAULT_DETAIL      | follows FAULT or LOOP_STALL: pc, lr, xpsr, ipsr as four records |
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
| 17   | BLE_MODULE_REBOOT | remote: the BLE module reported `%REBOOT%` on its own (brown-out or module reset, not caused by the remote's own reset); ms since the previous one, `0` = first |
| 18   | BLE_ERR_CONN      | remote: `%ERR_CONN%` while connected = link lost (supervision timeout); last RSSI in dBm (i8 in the low byte, `0x80` = unknown) |

**Polling rule (PC).** Send `DIAG_GET` when the node's link comes up and then every 10 s while it is alive; never
during a firmware update; once a response carries `n = 0`, ask again only on the next timer tick. Use the previous
`next_cursor` as the cursor (0 on the first request and after a `boot_id` change).

**Old firmware** does not know `0x80` and answers `NAK UNKNOWN_MSG (0x01)`. The PC then stops polling that node
until it reconnects.

The base forwards both messages like any other frame (`Dest` = PC or Remote); the router needs no change.
