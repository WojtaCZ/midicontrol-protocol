## Owner answers 2026-11-16: implementation backlog (protocol batch, all repos)

Source: audit/OWNER-ANSWERS.md; plan: plans/2026-11-16-owner-answers-impl.md. Work stays on branch audit/step4-manual.

This section designs phase A (A1-A9). Other task lists (B, C, D, E in the PC, base and remote TODO.md files) refer to these IDs.
Paths: base = midicontrol-fw, remote = midicontroller-fw, PC = midicontrolpy-sw (C++), boot = midicontrol-bootloader
(shared lib, pinned in both fw repos as lib/bootloader; bump is C10). Line numbers are from branch audit/step4-manual on 2026-11-16.

### Shared design: ONE protocol version bump (v1 -> v2) for A1-A9
- Today there is no version field anywhere (VERIFY: `grep -rn -i version inc/protocol/*.hpp PROTOCOL.md` shows only FW_UPDATE_START's unused version bytes, PM-9). Today's wire format is "v1" (implicit).
- Add INFO tag `PROTO_VER = 0x08` (u8) in `inc/protocol/info.hpp` (0x08 is free; used: 0x01-0x07, 0x10-0x15, 0x17, 0x20-0x21, 0x30-0x34; 0x16 is the dead tag of Q2-27, keep it unused). Add `constexpr uint8_t PROTOCOL_VERSION = 2;` in `inc/protocol/constants.hpp`.
- Each node (base and remote) reports its own PROTO_VER in INFO_RESP. A node that does not send the tag is v1. The PC reads it from the GET_INFO poll (PC comm.cpp:118 InfoPoller, result parsed at comm.cpp:723-890) and gates every new behaviour per peer. The remote's INFO payload is "nearly full" (info.hpp comment at LINK_RSSI): put PROTO_VER with the other firmware fields near the top and check the total stays <= 128 B (remote src/main.cpp sendInfo ~:160; VERIFY by a host test that builds the full INFO and checks length).
- Version applies to the peers independently: base v2 + remote v1 and the reverse are valid combinations during an update. Rule: a feature that involves two peers is active only when both ends report v2 (the base relays; the PC knows both from INFO).
- New wire items, all additive (nothing changes meaning, nothing is removed):

  | Item | Where | Used by |
  |---|---|---|
  | INFO tag PROTO_VER 0x08 u8 | info.hpp | all |
  | INFO tag ORGAN_SRC 0x22 u8 (base) | info.hpp | A5 |
  | KEEPALIVE PC->Base payload `[nonce u16 LE]` (was empty) | PROTOCOL.md:196 | A1 |
  | KEEPALIVE Base->Remote payload `[pcAlive, rssi, nonce u16 LE]` (was 2 B) | PROTOCOL.md:196 | A1 |
  | KEEPALIVE_ACK Remote->Base payload `[rssi, boot_id u16 LE]` (was 1 B) | PROTOCOL.md:197 | A2 |
  | STATUS_RESP payload 4 B -> 7 B: `[ble, ssr, display, pcAlive, flags, boot_id u16 LE]` | PROTOCOL.md:180 | A2 |
  | NOW_PLAYING_INFO `[0x01]` = clear only if the PC wrote last | PROTOCOL.md:121 | A3 |
  | NakReason NO_TRANSFER 0x08 | types.hpp:59 | A4 |
  | FW_UPDATE_STATUS retried until ACKed (same PacketId) | PROTOCOL.md:51 table | A4 |
  | New MsgType SIGNAL_INFO 0x34 (PC/Base -> Remote), payload `[flags u8]` | types.hpp:22-25 | A6, A8 |
  | SIGNAL_RECORDING 0x31 gets a meaning (was reserved) | PROTOCOL.md:157 | A7 |
  | RECORD_SONG 0x12 gets a meaning (was reserved): Base -> PC `[0x01 start / 0x00 stop]` | PROTOCOL.md:104 | A7 |
  | GET_SONG_LIST offset may be 2 bytes `[lo, hi]` (1 byte stays valid) | PROTOCOL.md:144 | A9 |

- General backward-compat rules used below (all VERIFIED in code unless marked):
  - Unknown MsgType to the base -> NAK UNKNOWN_MSG (router.cpp:449 default); to the remote -> NAK UNKNOWN_MSG (main.cpp default). So the PC must not send a new MsgType to a v1 peer (gate on PROTO_VER), not because it breaks anything but because it would produce a NAK and a log line.
  - Longer payloads on existing types are read by old code as the old prefix: old base KEEPALIVE ignores the payload; old PC STATUS parser takes `size() >= 3` (comm.cpp:781); old remote reads KEEPALIVE byte 0 and 1 only (link.cpp:186-200, "an older remote reads byte 0 only" PROTOCOL.md:196). VERIFY the old base really ignores a PC KEEPALIVE payload (router.cpp:422 sends KEEPALIVE_ACK without reading it: confirmed by reading, still add a host test).
  - NOW_PLAYING_INFO with an unknown length (<5, not 0) is silently ignored by the old base (display.cpp applyNowPlaying `len < 5 return`). So the new 1-byte clear must be sent only to a v2 base, or the stop would not clear at all.
- Files that change for the bump in the protocol repo: `inc/protocol/{types,constants,info}.hpp`, `src/` if any table of names exists (PC has its own src/protocol/names.cpp for MsgType names, add SIGNAL_INFO), `PROTOCOL.md` (tables listed above, a new "Protocol version" section), `INDICATOR-BUS.md` (A3), `CLAUDE.md` if it lists IDs. Then bump the submodule/copy in: base lib/protocol, remote lib/protocol, PC include/midicontrol/protocol + src/protocol (VERIFY how the PC copy is synced: `ls -la midicontrolpy-sw/include/midicontrol/protocol` and any sync script; the worktrees under wt/ have their own copies, ignore them).
- Test host for the protocol repo: it has no tests directory (VERIFY `ls midicontrol-protocol`: inc, src, docs only). Tests live in the consumers: base test/host (gtest), PC tests/ (VERIFY name), bootloader tests/test_main.cpp.

### A1 Session nonce: base/remote dedup reset on PC hello; re-enable the 2 DISABLED PM-13 tests (Q2-11, Q3-3)
- Goal: a restarted PC process must never have its first reliable commands swallowed as "duplicates" because its PacketId repeats the last one the base saw. Then switch the 2 DISABLED PM-13 base tests on. Done NOW (owner Q2-11), together with the bump.
- Current code:
  - base src/logic/router_core.hpp:39-55 `RxDedup` keeps only the LAST packetId per source; `forget(source)` exists. Used at src/router.cpp:93 (`_dedup`), :355 (`isRepeat` -> ACK again, no execute), :528 (`forget(ADDR_PC)` only when the PC was declared lost, Q1-8 b).
  - base test/host/test_router_core.cpp:140 `PM13.DISABLED_StaleRetryOfOnAfterOffMustNotPowerTheOrgan` (pid 5 on, pid 6 off, retry of pid 5 -> must not execute) and :152 `PM13.DISABLED_NewPcSessionCommandIsNotSwallowedAsDuplicate` (pid 1 old session, pid 1 new session -> must execute). Existing, keep: :163 `NewPcSessionAfterPcLossIsExecuted`, `ForgetPcKeepsRemoteFilter`, `ImmediateDuplicateSuppressed`.
  - remote src/link.cpp:215-225 has its own last-only filter `s_lastRx[source]` (LastRx) with the same weakness; the remote learns about a PC restart only through the base KEEPALIVE `pcAlive` edge (link.cpp:186-200).
  - PC src/comm.cpp:345 `initialPacketId()` is randomised, :364 `nextPacketId`; KEEPALIVE to the base is sent at comm.cpp:1002 every KEEPALIVE_PERIOD_MS (loop :1025). This only makes a collision unlikely, it does not remove it (the two tests say so).
- Change (the two tests need two different fixes):
  1. Test :152 (new session reuses pid): session nonce.
     - PC: pick a 16-bit nonce at process start (from `std::random_device`; 0 is reserved for "no nonce / v1 PC"). Send it in every KEEPALIVE to the base: payload `[nonce lo, hi]` (comm.cpp:1002 `send(ADDR_BASE, MsgType::KEEPALIVE)` -> with payload). The first KEEPALIVE must go out BEFORE any reliable frame after the port opens (VERIFY the startup order in comm.cpp around :1002 and the open path; if a reliable frame can precede it, send a hello KEEPALIVE synchronously in the open path). A new instance with the same nonce has chance 1/65535 per restart; mitigated further because the PacketId start is random too.
     - base: new small class in src/logic/router_core.{hpp,cpp}: `PcSession { bool observe(uint16_t nonce); }` returns true when the nonce is non-zero and differs from the stored one (first non-zero nonce also counts as changed). In the `case MsgType::KEEPALIVE` branch (router.cpp:422), when source is ADDR_PC and payloadLen >= 2: `if (_pcSession.observe(nonce)) _dedup.forget(ADDR_PC);`. KEEPALIVE is not ACKed so it never goes through the dedup (needsAck, router.cpp:106). Also forget the nonce when the PC is declared lost (next to router.cpp:528) so a restarted PC with the SAME nonce after a loss still works (the loss path already forgets the dedup).
     - base -> remote: the base KEEPALIVE to the remote (router.cpp ~:263-269) appends the PC nonce: `[pcAlive, rssi, nonce lo, hi]`. remote link.cpp:186: if payloadLen >= 4 and the nonce changed, clear `s_lastRx[ADDR_PC]` (`valid = false`). Old remotes read bytes 0-1 only.
  2. Test :140 (stale retry after a newer command, same session): not a session problem. Replace the one-slot filter by a small window: `RxDedup` remembers the last N=8 packetIds per source (ring buffer; `isRepeat` true if the id is anywhere in the window; `forget` clears the ring). The PC supersede rule (comm.cpp:40-50, group 1 SET_CURRENT) already drops older pending SET_CURRENT frames, so a legitimately retried older frame is always a repeat of something already executed. N=8 is far below the 256-id wrap. Use the same window in the remote link.cpp `LastRx` (keep its stored `reply` per slot for the re-ACK: `sendReply(pkt, last->reply)` at link.cpp:222; VERIFY memory: N x Reply per source on STM32G071, otherwise N ids but the reply only for the newest and an empty ACK for older ones).
  3. Remove the `DISABLED_` prefix and the "RESIDUAL" comments on both tests (test_router_core.cpp:138-150).
- Depends on: none (the version bump itself is introduced here: land PROTOCOL_VERSION and the PROTO_VER INFO tag in the first commit, see "Recommended order").
- Tests:
  - base test/host/test_router_core.cpp: un-disable both; add `PM13.NonceChangeForgetsPcDedup`, `PM13.SameNonceKeepsDedup`, `PM13.ZeroNonceNeverResets` (old PC), `PM13.WindowSuppressesOlderRetry`, `PM13.WindowDoesNotHideNewIdAfterWrap` (ids 1..9 then 1 again after >8 other ids). The `Base` fixture in that file must get a `keepalive(nonce)` helper that mimics the router branch (the fixture duplicates router logic; keep it in sync, or move the KEEPALIVE nonce handling into a function in router_core used by both).
  - remote: host test for `LastRx` window (src/logic tests dir, VERIFY name; peer_guard has one).
  - PC: unit test that the first outgoing frame after open is the nonce KEEPALIVE (tests/ + the fake serial; VERIFY file) and that the nonce stays constant for the process lifetime.
  - Sim: fw-sim flow "PC restart without loss": two PC processes in a row with the same first PacketId.
- Manual/facts: none user-visible. Update PROTOCOL.md (KEEPALIVE rows 196-197, dedup section ~:62-80 "last packet only" -> window), SYSTEM.md (PM-13 / Q1-8 rows, drop the "residual" notes), facts/base.md and facts/pc.md only if they mention the duplicate filter (VERIFY by grep "duplicate").
- Risk / open points:
  - A hello-less old PC sends no nonce: base behaviour stays as today (reset only on loss).
  - VERIFY the PC web/CLI helpers (tools/basetest python, each process numbers PacketIds from 1, learning 2026-11-07) still work: they send no nonce, so their first pid-1 GET_INFO is dropped as before. Give tools/basetest a hello KEEPALIVE with a random nonce (that learning says it already works around the first-pid drop by ACKing INFO_RESP; the nonce removes the cause).
  - The window changes the meaning of "ImmediateDuplicateSuppressed" only for older ids; keep that test.

### A2 Remote boot id in STATUS (Q3-14)
- Goal: the PC detects a remote reboot that is shorter than the 5 s STATUS poll (no ble 0->1 edge seen, so no resync; finding R33 / F3-2) and resyncs the remote (SIGNAL_PLAYING, SIGNAL_ANCHOR, flags). Done with A1 (owner Q3-14).
- Current code:
  - remote src/diag.cpp:300 `bootId()` returns the u16 boot id (epoch from TAMP BKP1R/BKP2R + counter * 0x9E37, logic/diag_ring.hpp:21-28). It is computed lazily at the first call; the comment at diag.cpp:188-191 says the epoch is completed at the first `bootId()` using the PC connection time (VERIFY: if the first call now happens at the first KEEPALIVE_ACK (no PC data yet) the entropy of the epoch changes; either call `bootId()` at the same moment as today or accept; check diag.cpp:188-215 before deciding).
  - remote KEEPALIVE_ACK is `sendSimple(..., &rssi, 1)` at link.cpp:200.
  - base src/router.cpp:426 KEEPALIVE_ACK handler reads `payload[0]` (rssi) only; STATUS_RESP built at :371-379 with 4 bytes; PeerPresence in router_core for the remote.
  - PC comm.cpp:781-830 STATUS_RESP parse (`size() >= 3`); the resync trigger is the ble 0->1 edge -> `doRemoteResync` comm.cpp:480 (also :92 in diag_poller.cpp "forget the link state so the next STATUS_RESP with ble=1 is a fresh edge"). DIAG_RESP already carries the boot_id but only every 10 s (PROTOCOL.md:245) and only feeds the diag poller (diag_poller.cpp:57-69).
- Change:
  1. remote link.cpp:200: KEEPALIVE_ACK payload `[rssi, bootId lo, bootId hi]` (3 B).
  2. base router.cpp:426: if `pkt.source == ADDR_REMOTE && payloadLen >= 3` store `_remoteBootId` (u16) + `_remoteBootValid = true`; clear valid when the remote is declared lost (where PeerPresence reports the loss, same place as the A1 forget) so a stale id is never reported.
  3. base router.cpp:371-379: STATUS_RESP gets `payload[4] = flags` (bit0 = remote boot id valid) and `payload[5..6] = boot id LE`; `payloadLen = 7`. `payload[4]` bits 1-7 are reserved 0.
  4. PC comm.cpp ~:781-830: if `size() >= 7 && (flags & 1)` and `ble == 1`: compare with `_lastRemoteBootId` (std::optional<uint16_t>, reset when the base link drops or `ble` goes 0). Different value (not first) -> log "Remote rebooted (boot id a -> b)", call the same resync as the 0->1 edge (`doRemoteResync`, send once, not while updating, comm.cpp:482). First value just stored.
  5. Gate: PC acts only on a base reporting PROTO_VER >= 2 (a v1 base sends 4 bytes, so `size() >= 7` already gates; the explicit gate keeps the intent readable). The remote can be v1 (no boot id in the ACK): then `valid = 0` and nothing changes.
- Depends on: A1 (same KEEPALIVE/STATUS edit, version bump).
- Tests:
  - base test/host: KEEPALIVE_ACK with 3 B stores id; with 1 B keeps invalid; loss clears; STATUS_RESP layout 7 B (new test next to the router fixture; if the STATUS build is inline in router.cpp:371 move it into a function in router_core.cpp to test it: `buildStatus(...)`).
  - PC tests: STATUS_RESP 4 B (old base) no resync; 7 B same id no resync; 7 B changed id with ble=1 -> exactly one resync; first id -> none; id change with ble=0 -> none.
  - fw-sim: remote reboot shorter than 5 s (flow in remote-sim) -> PC resyncs the playing state.
- Manual/facts: none visible (D7 "Stav neznamy" is separate). PROTOCOL.md rows 180, 197; SYSTEM.md note under the link/resync rows; close finding R33 / F3-2 in audit/STATE.md notes only on the owner's word.
- Risk / open points:
  - KEEPALIVE_ACK payload size on the BLE link: 3 B, no issue.
  - boot id repeats only after 65536 boots with the same epoch; with a power loss the epoch is re-drawn, so a collision on a power-cycle is 1/65536 (acceptable, documented in diag_ring.hpp).
  - Counts as a reboot if the remote came back as the SAME boot id after a pure BLE drop: no resync (correct, state kept by the remote).

### A3 Base tracks the last indicator writer; clear on stop only own number (Q2-19, R4-3, SYS-BUS-07)
- Goal: on stop/song end the base clears the church indicator only if the number on the bus is still the one this PC/base wrote. If another master (the manual controller) wrote after us, leave it alone. Owner Q2-19 = b: base support, protocol + fw change.
- Current code:
  - PC comm.cpp:1182-1190 `displayClear()`: sends NOW_PLAYING_INFO with an empty payload (`clearUpdate()`, unconditional) when `_displaySet` was true; `_displaySet.store(!u.clear)` at comm.cpp:1574. PC part of SYS-BUS-07 (P1-10) is done; "another master wrote since" is not checked (SYSTEM.md:304, gap 27 at :373).
  - base src/display.cpp:36 `currentState[9]` (what the base wants on the bus), `applyNowPlaying` :211 (len 0 -> `clear()` :258 unconditionally; len < 5 ignored), RX path: frames are received by DMA into `rxBuffer[9]`, accepted by `indicator::frameAccepted` (src/logic/indicator_bus.hpp, 9 B + 0xB0 flag, counted in `rxAccepted`), `rxFrameReady` set by the ISR, then echoed to the PC as SysEx (`midiEchoPending`, process() :170-200). Nothing else uses the received content (SYS-BUS-01 "partial").
- Change:
  1. Protocol: NOW_PLAYING_INFO length 1, `[0x01]` = "clear only if still ours" (CLEAR_IF_OWN, named constant `NPI_CLEAR_IF_OWN` in constants.hpp next to NPI_BLANK / NPI_NO_CHANGE). Length 0 stays the unconditional clear (used by the base's own menu paths and v1 PCs). Document in PROTOCOL.md:121-135 and INDICATOR-BUS.md.
  2. base display.cpp: add `enum class Writer { None, Own, Foreign }` and a static `_lastWriter`.
     - `sendState()` (called by applyNowPlaying/clear/setters) sets `Own` after the frame is queued; the base's own menu writes (setSong etc.) count as Own too.
     - On an accepted RX frame (the same place `rxAccepted` is incremented) that is NOT the echo of our own TX: set `Foreign`. How to tell own echo from foreign: VERIFY first whether the base RX even hears its own TX frames (separate disp_tx/disp_rx lines, see the 2026-11-12 learning on LA channels and `display::rxFrameReady` use). If the bus is a loop and the base hears itself, compare the received 9 bytes with `txBuffer`: equal -> own, different -> foreign. If it does not hear itself, every accepted frame is foreign.
     - `applyNowPlaying(len==1, payload[0]==0x01)`: `if (_lastWriter == Own) clear(); /* sets Own->None after clear */ else { skip; log "indicator: foreign number kept" }`. After a conditional skip leave `_lastWriter = Foreign`.
     - The decision logic (writer state machine) goes into src/logic/indicator_bus.hpp as a pure class `WriterTracker` (`onOwnWrite()`, `onRxFrame(const uint8_t*, ownTx)`, `bool mayClear()`), for host tests.
  3. PC comm.cpp:1182 `displayClear()`: if the base reports PROTO_VER >= 2 send `[NPI_CLEAR_IF_OWN]` (1 B), else keep the empty payload (a v1 base would ignore a 1-byte NPI and never clear). `mc::display::clearUpdate()/encodeNowPlaying` (src/display*.cpp, VERIFY name) gets a `conditional` flag. Keep `_displaySet` logic (P1-10: the PC still never clears a number it did not set).
  4. base: the PC-lost path and Nahraj/menu "Stop" use the unconditional `clear()` (nothing of ours to protect when the PC is gone? VERIFY with owner: after a PC loss the base clears the number it holds; if a foreign master wrote after, use the same conditional rule: recommended, tell the owner).
- Depends on: version bump (A1). Related: C11 (short codes right-aligned) touches the same function, do after or together.
- Tests:
  - base test/host: new `test_indicator_writer.cpp` (WriterTracker): own write -> clear allowed; foreign frame after own write -> clear skipped; own write after foreign -> allowed again; unconditional clear always works; echo of own TX not counted foreign.
  - PC: displayClear sends the 1-byte form to a v2 base and the empty form to a v1 base (tests next to the existing P1-10 display tests, VERIFY file); no clear when `_displaySet` is false (existing).
  - Sim: fw-sim indicator flow (VERIFY the sim has a bus model: tools/basetest la.py is bench only): injected foreign frame between play and stop.
  - BENCH: with the real indicator and the manual controller (owner present, indicator is not on the bench: see learning "Bench has no church display").
- Manual/facts: tabule chapter (docs/manual, line ~42 "clearing on stop"): rewrite the stop behaviour: "the number is cleared only if nobody else changed it since"; facts/base.md and facts/pc.md; SYSTEM.md SYS-BUS-07 and gap 27 -> implemented (E2/E1 own the text).
- Risk / open points:
  - VERIFY the own-echo question above before designing the comparison.
  - A foreign frame that equals our number exactly is treated as ours only if it matches `currentState`; OK, the effect (clearing a number the other master also shows) is harmless.
  - SYS-BUS-02/03 (mirror and record foreign frames) are NOT part of this task.

### A4 FW_UPDATE_STATUS gets an ACK + retry; clearer PM-8 error (Q2-28)
- Goal: a lost FW_UPDATE_STATUS must not make the PC fail an update. Status frames are ACKed (already by the PC) and RETRIED by the sender. NAK BUSY for DATA/END without a transfer (PM-8) becomes a clear, non-retried error.
- Current code:
  - boot src/fw_receiver.cpp:30-47 `status()` stores one pending status; `takeStatus()` builds the FW_UPDATE_STATUS packet (`out.packetId = _pid++`, 1 payload byte); sent once: bootloader main (base) midicontrol-fw/bootloader/src/main.cpp:156 `if (s_rx.takeStatus(out)) sendPacket(out)`; remote src/fwupdate.cpp:112-123 `link::sendRaw(status)` (one-shot, link.hpp:49 comment). (PM-7.)
  - PC: dispatch.cpp:133 ACKs FW_UPDATE_STATUS; comm.cpp:966-992 puts the byte into `_fwStatus` (one slot) and `waitFwStatus` (comm.cpp:637) waits; firmware_update.cpp:114-155 waits 500 ms / 1 s / 300 ms / 3 s for it. READY after START is "tolerated if absent" (:120), CRC_OK/CRC_FAIL after END is waited 3 s (:155).
  - PM-8: boot src/fw_receiver.cpp:131 (DATA) and :178 (END) answer NAK BUSY when no transfer is active; the PC retries BUSY (PM-6: DATA 1.5 s x 8) so a device that lost its transfer (reset) takes ~12 s to give an unclear error.
  - The base forwards remote FW traffic end to end: router.cpp:497-508 (`_remoteFw.observe`).
- Change:
  1. boot lib (fw_receiver.{hpp,cpp}): keep the pending status after `takeStatus()` as "awaiting ACK": `_statusPid`, `_statusSentMs`, `_statusTries`. New `bool FwReceiver::pollStatusRetry(uint32_t nowMs, protocol::Packet &out)` returns the same packet (SAME PacketId) every 500 ms, up to 3 retries, like all other reliable sends (PROTOCOL.md retry rule). New `void FwReceiver::onAck(const protocol::Packet&)` clears it when the ACK comes from the PC with `payload[0] == _statusPid` (VERIFY the ACK payload format: ACK carries the acked PacketId, see PROTOCOL.md ACK row). A newer status replaces an older pending one. `Action::Apply` case: the CRC_OK status is the last frame before the reboot; send it, wait up to 250 ms for the ACK, then apply (do not delay the reboot beyond that: VERIFY the apply delay in main.cpp and fwupdate applyRequested).
  2. base bootloader main.cpp:150-160 call `pollStatusRetry` from the main loop and `onAck` when an ACK arrives; remote fwupdate.cpp: `handlePacket` passes ACKs from link.cpp:204 (`case KEEPALIVE_ACK: return;` area; ACKs are consumed by link's own retry queue: VERIFY how link.cpp handles `MsgType::ACK` and whether a hook is needed) and `process()` (:130) calls `pollStatusRetry`.
  3. NakReason `NO_TRANSFER = 0x08` ("no transfer active: DATA/END without a START") in types.hpp:59; fw_receiver.cpp:131 and :178 use it instead of BUSY. BUSY stays for "a transfer from another source is active" if that case exists (VERIFY fw_receiver.cpp:83-100).
  4. PC firmware_update.cpp: on NAK NO_TRANSFER fail at once, no retries; error text: "device lost the transfer (reset or timeout); start the update again". The NAK reaches `sendReliable` in comm.cpp (~:525); add the reason to the retry decision (BUSY retried, NO_TRANSFER not) and to the NAK text table (VERIFY where NakReason names are printed: src/protocol/names.cpp). Old PC with the new bootloader: reason 8 is unknown; VERIFY comm.cpp sendReliable treats an unknown reason as final (not retried).
  5. PC: the status mailbox dedups itself (a retry repeats the same PacketId and the same byte: harmless); keep waits as they are; they now can be shortened never, only extended is not needed.
- Depends on: version bump; C10 (lib/bootloader bump in both fw repos, otherwise the new receiver does not reach them); A8 uses the same files.
- Tests:
  - boot tests/test_main.cpp (line 167 helper `hasStatus`/`status` tests): status retried 3x with the same PacketId; ACK with matching id stops it; ACK with another id does not; newer status replaces; DATA before START -> NAK NO_TRANSFER.
  - PC: firmware_update test with a fake link that drops the first FW_UPDATE_STATUS (existing fake in tests/; VERIFY file) and NAK NO_TRANSFER -> immediate failure with the clear text.
  - Sim: remote-sim OTA flow with a dropped status frame.
- Manual/facts: sprava chapter (update section): the new error text; facts/pc.md "no NAK text" row. PROTOCOL.md rows 51 (FW_UPDATE_STATUS) and NakReason table; protocol-matrix PM-7/PM-8 rows -> done (audit file, owner closes).
- Risk / open points:
  - Time budget when the remote is the target: ACK goes PC -> base -> remote; ~100-300 ms over BLE; 500 ms retry interval is fine.
  - If the ACK never comes the receiver gives up after 3 retries and continues (the update is not aborted by a missing ACK).
  - VERIFY memory/time in the bootloader (base): its main loop has no scheduler; use `DWT->CYCCNT` or SysTick like `s_lastPacketCycles` (bootloader main.cpp:152).

### A5 INFO: organ on "by PC" vs "by menu" (Q2-33)
- Goal: after a PC crash/restart the PC can tell whether the organ power was switched on by itself (SET_CURRENT from a PC) or by the base menu, so BE-4 (organ on from a crashed session treated as a manual switch-on, stays on 30 min) is decided correctly. Owner Q2-33 = a.
- Current code: base src/router.cpp:362 `base::current::set(pkt.payload[0] != 0)` for the PC command; the menu "Napajeni varhan" toggles through `base::current` from src/menu.cpp (VERIFY callers: `grep -n "current::" src/menu.cpp src/router.cpp`); base `buildInfo` router.cpp:312-350 emits SSR tag 0x21; tags list info.hpp:42-45. PC reads GET_INFO from the base (comm.cpp:118, parse :723-890) and decides at startup in the Q1-1 b logic around `_organClaimed` / `armIdleOff` (comm.cpp:1186-1195, :1476; VERIFY the startup code: grep `organClaimed`, `armIdleOff`).
- Change:
  1. info.hpp: `ORGAN_SRC = 0x22` (u8): 0 = off, 1 = on, switched on by the PC (SET_CURRENT from ADDR_PC), 2 = on, switched on by the base menu, 3 = on, source unknown (e.g. after a base reboot while the SSR state is restored; VERIFY whether the base keeps the SSR on over a reboot at all).
  2. base: `base::current` (src/current.{hpp,cpp}, VERIFY path) gets `setSource(Src)`; router SET_CURRENT passes PC, menu passes Menu; `disable()` sets 0. Add the tag in `buildInfo` after SSR 0x21. The base reboots clear it: after a base reboot the SSR is off (VERIFY) so 3 is likely never produced; keep the value reserved.
  3. PC: at connect (after the first INFO_RESP from a v2 base) if the organ is on: source PC -> the PC treats it as its own (BE-4 case): arm the normal idle-off exactly as after its own power-on (do not set `_organClaimed`); source menu -> manual switch-on as today (30 min rule / B1 / B3 owner key). Decide what "its own" means for the idle timer with B1 (organIdleOffMs, owner Q2-1).
- Depends on: version bump. B1/B3 (PC idle timer keys) consume it.
- Tests: base host test for `current` source tracking (src/logic/, VERIFY there is a host-testable part); PC test with a fake INFO_RESP (tests for devinfo, VERIFY name) for both sources + v1 base (no tag -> today's behaviour); sim: crash-restart flow (PC process killed while organ on).
- Manual/facts: sprava/zakladna chapters about idle-off after a PC restart (E1 owns the text, M-* list), facts/pc.md idle-off fact, SYSTEM.md Q1-1 rows.
- Risk / open points: the owner chose INFO (not STATUS); INFO is polled by the InfoPoller, not at every STATUS: make sure the PC polls base INFO once at connect BEFORE the first idle decision (VERIFY the poller schedule, comm.cpp:118 area).

### A6 "Song has sections" flag so the remote hides add-verse (M-add-verse)
- Goal: if the song file has no sections (verses), the remote must not offer "add verse". The remote needs to know it from the PC. Remote UI part is D8 (and D2 depends on the same flag: VERIFY with the D2 entry).
- Current code:
  - PC comm.cpp:1503 `sections = _songSections`, :1522 cleared on a new play, :1583 `_songSections.push_back(label)` (labels from the song's markers; anchors with " +" are extra passes), :1588 `sendRemoteSignal(SIGNAL_ANCHOR, ...)` (remembered for resync), :1592 "SIGNAL_PLAYING goes out before the first SIGNAL_ANCHOR". `isRemoteSignal` comm.cpp:48-50, `supersedeGroup` :36-46. The PC knows the sections of a file when it parses the MIDI (`onAnchor`, comm.cpp ~:1530-1600): VERIFY whether the markers are known BEFORE the first anchor fires (the song is parsed before play? check songs.cpp / the player: if not, the PC must pre-scan the file for markers at play start).
  - remote src/main.cpp:80-111 `handleAppPacket`: SIGNAL_PLAYING parser NAKs every character outside `[0-9A-Z]` (so the flag must NOT be appended to SIGNAL_PLAYING: an old remote would NAK the whole frame); SIGNAL_ANCHOR :103 takes a label. add-verse is the REPLAY_ANCHOR button in src/player_screen.cpp (VERIFY exact lines; remote link.cpp:71 `case REPLAY_ANCHOR: return 0`).
- Change:
  1. New MsgType `SIGNAL_INFO = 0x34` (PC/Base -> Remote), payload `[flags u8]` (longer payloads allowed later, receivers read the bytes they know). Flags: bit0 `SONG_HAS_SECTIONS`, bit1 `BASE_UPDATING` (A8), bits 2-7 reserved 0. State message: ACKed, goes into supersede group 2 like the other state signals? No: it must not drop and must not be dropped by SIGNAL_PLAYING/STOPPED. Give it its own group 3 (newest wins among SIGNAL_INFO). Add to `isRemoteSignal` (comm.cpp:48), `supersedeGroup` (:36), `src/protocol/names.cpp`, and the PC resync list (`doRemoteResync` comm.cpp:480-510: re-send the last SIGNAL_INFO).
  2. PC: when a song starts, after SIGNAL_PLAYING send `SIGNAL_INFO [SONG_HAS_SECTIONS = markers found]`. A mid-song resync resends it. On stop the remote resets the bit itself when it handles SIGNAL_STOPPED (the PC does not need to send a cleared flag).
  3. Gate: PC sends it only to a remote with PROTO_VER >= 2 (INFO via the base). A v1 remote gets nothing and keeps showing add-verse (today's behaviour).
  4. base: dest is the remote, so the base forwards it by destination (router.cpp forward ~:437). VERIFY that the base forwards an unknown MsgType to the remote: needsAck (router.cpp:106) must say "ACK needed" for 0x34 (default = needs ACK, VERIFY) and forward() must not check the type against a list.
  5. remote main.cpp:80: `case MsgType::SIGNAL_INFO:` read `payload[0]` (len >= 1 else NAK INVALID_PAYLOAD), `GUI::notifyFlags(flags)`; `SONG_HAS_SECTIONS` cleared on SIGNAL_STOPPED. If the remote never receives the message (v1 PC) it assumes "has sections" (add-verse visible) = unchanged. A v2 remote with the flag 0 hides add-verse and `REPLAY_ANCHOR` is not sent. Details of the screen: D8.
  6. PC still answers a REPLAY_ANCHOR for a song without sections with NAK NOT_PLAYING? Today: NOT_PLAYING when nothing plays; for "no sections" the PC currently adds a pass of the whole song? VERIFY the PC behaviour in comm.cpp (REPLAY handler, grep `REPLAY_ANCHOR`/`replayAnchor`) and keep it unchanged for v1 remotes.
- Depends on: version bump. Used by D8, D2 (remote UI), A8 (same message).
- Tests: PC test (fake remote link): play a song with sections -> SIGNAL_INFO bit0 = 1 after SIGNAL_PLAYING; without -> 0; resync resends; v1 remote -> not sent; supersede group 3 does not drop PLAYING. Remote host test for `notifyFlags` state (logic dir); remote-sim flow: song without sections -> no add-verse entry on the screen (screens export: tools/screens, learning "export has its own Bluetooth stub").
- Manual/facts: ovladac chapter add-verse section (E1 M-add-verse), facts/remote.md add-verse fact, SYSTEM.md SYS-USE rows for add-verse.
- Risk / open points: VERIFY the pre-scan question (item 1 above). VERIFY D2 really needs this flag (it was listed as depending on A6 by the owner's plan; the base entry author may use it differently).

### A7 Recording started only from base "Nahraj"; recording state to remote; one at a time (M-recording-start, Q2-22)
- Goal: SYS-REC-01 recording starts and stops ONLY from the base (`Nahraj` menu item, C3). The remote has no record button (D9). While recording, the remote shows a recording info screen (D10). Only one recording at a time; a second request is refused. The PC console `record` stays as an admin tool (B11).
- Current code:
  - protocol: RECORD_SONG 0x12 reserved (types.hpp:15, PROTOCOL.md:104); SIGNAL_RECORDING 0x31 reserved (types.hpp:23, PROTOCOL.md:157); SIGNAL_RECORDING is already in `supersedeGroup` group 2 and `isRemoteSignal` (comm.cpp:42, :49); base router.cpp:415-416 routes SIGNAL_* locals to `observeSignal`.
  - PC comm.cpp:1192-1260 `recordSong()` (PCC-1: one recorder at a time: `_recorderStarting` / `recorderAliveLocked()`; second call returns false with "Already recording; Stop first."), recorder = `arecordmidi` child process; `doStop(bool signal)` :1620 stops playback and recorder (VERIFY the recorder stop path: grep `_recorderPid` in comm.cpp); console `record` only (facts/pc.md: "recording console-only"). `dispatch.cpp:85` PLAY_SONG; no RECORD_SONG case (default NAK UNKNOWN_MSG).
  - base: "Nahraj" menu item is dead (facts/base.md bug row: no callers; C3 owns the menu), no RECORD_SONG sender.
  - remote: no recording handling, handleAppPacket default NAK UNKNOWN_MSG for 0x31 (main.cpp).
- Change:
  1. RECORD_SONG 0x12: Base -> PC, payload `[0x01]` start / `[0x00]` stop (1 byte; empty = start for tolerance). ACKed. PC answers NAK BUSY if a recording is running or starting (PCC-1) or a song is playing (one at a time; recording while playing would capture the organ output as the song plays), NAK BUSY also during a firmware update (A8). Otherwise ACK, start `recordSong()` asynchronously (it takes seconds for the recorder to warm up: ACK first, state signals after).
  2. SIGNAL_RECORDING 0x31: PC -> Base and PC -> Remote, payload empty (state "recording started"). Sent when the recorder is really running. "Recording ended" = `SIGNAL_STOPPED` (existing), sent on stop, on recorder death (`recorderAliveLocked` false), on PC shutdown. It is remembered for the remote resync like SIGNAL_PLAYING (same group 2, so a later STOPPED supersedes a pending RECORDING).
  3. Play vs record exclusion on the PC: `playSong()` (comm.cpp:1063) returns BUSY while recording (it probably does so already through `busy` at :1476; VERIFY and add the NAK path for a PLAY_SONG from the remote: the remote shows "Zaneprazdneno" or its BUSY text, see D10). STOP_SONG while recording stops the recording (VERIFY owner intent: remote has no record control; Stop on the base stops both; recommended: any STOP_SONG stops the recording, because the remote's info screen needs a way out; tell the owner).
  4. base: `RECORD_SONG` sending from the Nahraj item (C3): reliable via `sendReliable` to ADDR_PC; menu shows "Nahravam" screen on SIGNAL_RECORDING (base `observeSignal` router.cpp:300-310 already receives it); NAK BUSY -> "Zaneprazdneno". SIGNAL_RECORDING from the PC to the base passes `handleLocal` (router.cpp:415) which calls `observeSignal(pkt)`. The base-side screen is C3.
  5. remote main.cpp:80-111: `case MsgType::SIGNAL_RECORDING:` -> `GUI::notifyRecording()` + ACK (no payload, tolerate 0..n bytes). The screen is D10; Play on the remote while recording is blocked locally (D10) and by the PC NAK BUSY.
  6. PC web/console: `record` admin command (B11) goes through the same `recordSong()` and therefore also emits SIGNAL_RECORDING; no protocol difference.
  7. Gate: PC sends SIGNAL_RECORDING to the remote only if the remote is PROTO_VER >= 2 (a v1 remote would NAK UNKNOWN_MSG... it is not a new type, so a v1 remote NAKs with UNKNOWN_MSG at main.cpp default; still gate); RECORD_SONG only arrives from a v2 base, so no gate needed on the PC side. A v1 PC answers RECORD_SONG with NAK UNKNOWN_MSG: the base shows "PC nema nahravani" (C3 text; use Czech without diacritics).
- Depends on: version bump; A8 (BUSY refusal shares the PC state check), A6 (SIGNAL_INFO is not needed here). C3 (base menu), D9, D10 (remote), B11 (PC console).
- Tests: PC unit tests (fake link + fake recorder, the existing PCC-1 tests): RECORD_SONG start -> ACK + SIGNAL_RECORDING to base and remote; second start -> NAK BUSY; start while playing -> NAK BUSY; STOP_SONG -> STOPPED to both; recorder dies -> STOPPED; resync resends RECORDING; v1 remote gets nothing. Base host test: RECORD_SONG built by the Nahraj path, NAK BUSY handling (src/logic). Remote host test: `notifyRecording` state. fw-sim flow: base Nahraj -> recording info screen on the remote (tools/screens export for the new screen).
- Manual/facts: postupy chapter (recording procedure) and zakladna chapter (Nahraj), ovladac chapter (info screen text), sprava chapter (M-recording-to-song: the admin renames the file; unchanged), facts/base.md, facts/remote.md, facts/pc.md; SYSTEM.md SYS-REC-01 "base only" (E2).
- Risk / open points:
  - VERIFY who owns the MIDI input for the recording when the base started it: the PC records from the organ's MIDI-out through ALSA (`_midiFinder`, comm.cpp:1201-1215); no base data path is needed.
  - Time between NAK/ACK and the real recorder start (arecordmidi warm-up): SIGNAL_RECORDING must follow the ACK, not precede it; the base shows a "Priprava" state until then (C3).
  - VERIFY the BE: SIGNAL_RECORDING handled in `supersedeGroup` group 2 means a PLAYING/STOPPED drop of a pending RECORDING is correct (newest state wins).

### A8 Update in progress: Play refused with BUSY; the other device shows "<device> updating" (Q2-17)
- Goal: while the PC updates a device, Play from the remote/base is refused with NAK BUSY (instead of starting a song), and the device that is not updating shows an info screen "<device> updating", e.g. the remote shows that the base is being updated instead of only "Odpojeno". Owner expects the remote to show "Odpojeno" during a base update today; not verified (BENCH list: Q2-17 remote during base update).
- Current code:
  - PC firmware_update.cpp (update flow: START :114, DATA :134, END :153, ENTER_BOOTLOADER :207) with `_comm` pause flags: comm.cpp:259 `_remotePaused` ("no GET_INFO to the remote during its update either"), comm.cpp:482 "No send during a firmware update (base or remote)". PLAY_SONG path dispatch.cpp:85 -> `playSong()` comm.cpp:1063. VERIFY whether `playSong` already refuses during an update: grep `updating`/`_updating` in comm.cpp, firmware_update.cpp.
  - base: ENTER_BOOTLOADER handler router.cpp:438-445 (reset into the USB bootloader after 250 ms); `_remoteFw.observe` router.cpp:497-508 shows the OLED progress for the remote update ("Prijem dat..." in the bootloader style, base src/ GUI::showFwProgress); during the bootloader the base is a USB device with no BLE, so the remote just loses the link.
  - remote: src/fwupdate.cpp shows its own progress screen (GUI::showFwProgress, fwupdate.cpp:130-140); link loss shows "Odpojeno"; main.cpp:80 dispatch.
- Change:
  1. PC: while an update of ANY device runs (`FirmwareUpdater` sets a flag in CommHandler, one place: the existing pause flags in comm.cpp:259), `playSong()` and RECORD_SONG (A7) return `NakReason::BUSY` (PLAY_SONG from a remote/base is answered with NAK BUSY, the same code path as PCC-1 "busy"). Keep it a BUSY and not a new reason (owner wording). VERIFY the senders' reaction to BUSY: PROTOCOL.md says "NAK BUSY is retried" by PC sendReliable only; remote/base (protocol-matrix PM-6) do not retry BUSY, they show the generic error: D11/D12 check the text.
  2. Remote shows "Zakladna: aktualizace" while the BASE is updated: the base cannot talk after it reboots into the bootloader, so announce before. Base `ENTER_BOOTLOADER` handler router.cpp:438 (before the 250 ms timer) sends `SIGNAL_INFO [BASE_UPDATING]` (new message from A6, bit1) to the remote via the normal path (no ACK wait; best effort, one resend at 100 ms) and the remote keeps the state until the link returns or 5 min pass; the remote shows the screen "Zakladna: aktualizace" instead of "Odpojeno" (D11). Both devices must support SIGNAL_INFO: the base only sends it to a remote it knows is v2 (the base cannot know the remote's PROTO_VER: base tracks it from the KEEPALIVE_ACK? Add one reserved bit: remote sets bit7 of the KEEPALIVE_ACK rssi? No. Simplest: send it anyway; a v1 remote NAKs UNKNOWN_MSG, which the base ignores).
  3. Base shows "<remote> updating" on its OLED while the PC updates the remote: already the case via `_remoteFw` progress (VERIFY the text in src/display/menu: "Prijem dat" / "Aktualizace ovladace"); check against the owner's wording "Ovladac: aktualizace" and align (C-task for base).
  4. Remote is the updater itself (remote OTA): remote already shows its own progress; the base already shows progress. Nothing new on the wire.
  5. Flag lifetime: `BASE_UPDATING` is set by the base at ENTER_BOOTLOADER and cleared by the remote on the next KEEPALIVE from a booted base (after a successful update the base reconnects: the first KEEPALIVE arriving clears it) or after the 5 min limit (D11).
- Depends on: A6 (SIGNAL_INFO 0x34), version bump. D11 (remote screen). A7 uses the same refusal.
- Tests: PC unit test: PLAY_SONG during a (fake) update -> NAK BUSY, after the update ends -> ACK (tests for firmware_update, VERIFY file); base host test: ENTER_BOOTLOADER emits SIGNAL_INFO to the remote once (router fixture in test_router_core.cpp, if the handler is movable into router_core); remote host test: flag set/cleared on KEEPALIVE/timeout; remote-sim: SIGNAL_INFO flag -> screen export.
- Manual/facts: sprava chapter (update procedure: what the other device shows), ovladac chapter (screen "Zakladna: aktualizace"), facts/remote.md string table, facts/base.md; SYSTEM.md Q2-17 rows.
- Risk / open points:
  - BENCH first: check what the remote really shows during a base update today (owner's expectation is not verified).
  - The 250 ms before the bootloader reset is short for a BLE send with an ACK; the message is best effort. If it is lost the remote falls back to "Odpojeno" (acceptable).
  - VERIFY the base can send to the remote at that moment (`sendSimple` or `sendReliable` in router.cpp:438 area).

### A9 Song list without a fixed cap, real paging (Q2-14, B5)
- Goal: the song list on base and remote is effectively unlimited (owner Q2-14: "not limited to 33"); today the base cuts it at 32 (B5) and the remote has the offset bug RF-18. UI details (one-action Back, WaitingMore timeout, remote offset fix) are D12; base menu paging is a base task (C-list: VERIFY its ID; the plan lists B5 as the finding).
- Current code:
  - PC dispatch.cpp:105-114: `GET_SONG_LIST`, `offset = payload.empty() ? 0 : payload[0]` (ONE byte, max 255); `songListPage(offset)` comm.cpp:1054-1062 -> songs.cpp:111 `buildSongListPage(codes, offset, count, MAX_PAYLOAD_SIZE)`: loops `for i = offset; i < codes.size()`, packs as many `;`-joined codes as fit, byte 0 = hasMore. The song count is bounded by the code rule (1-4 digits <= 1999 plus A-D: up to ~8000 codes in theory; the library has far fewer).
  - base src/menu.cpp:227-229 `MAX_SONGS = 32` static arrays `_songNames[32][24]` and `_songListItems[33]`; :689 Down at the end requests the next page only `_songCount < MAX_SONGS`; :869 parse loop stops at MAX_SONGS; `SongListState` Idle/WaitingFirst/WaitingMore (:235, :455, :692, :794-801); the base's request offset is `_songCount` (VERIFY :688-700).
  - remote src/song_list_screen.cpp:74-125 offset logic: RF-18: the offset counts only entries that fit on the screen buffer, not the entries received (entries lost between pages). WaitingMore has no timeout (PM-4).
- Change:
  1. Protocol: GET_SONG_LIST payload `[offset u16 LE]` (2 bytes; a 1-byte payload is still a valid offset < 256; empty = 0). Senders send 2 bytes only when the offset >= 256 (so a v1 PC still works for the first 256 songs; accept that an older PC mishandles lists > 255, VERIFY and mention in the manual's compatibility note: PC and base are updated together in practice). Offset = index of the first entry in the PC's sorted list (unchanged meaning). SONG_LIST_RESP unchanged `[hasMore, "code;code;..."]`. Add rule (PROTOCOL.md:144-145): a requester must advance its offset by the NUMBER OF CODES RECEIVED in the previous response (count the `;`-separated entries), never by the number it displayed (fixes RF-18 at the protocol level); an offset past the end returns `[0x00]` + empty list; `hasMore` byte: bit0 = more entries follow; bits 1-7 reserved (the base tests `!= 0` today, router.cpp:403, so a future bit would break it: keep them 0).
  2. PC dispatch.cpp:111: `offset = size >= 2 ? payload[0] | payload[1] << 8 : size == 1 ? payload[0] : 0`. songs.cpp already takes `size_t`.
  3. PC cache: `listSongs()` is called per request (comm.cpp:1056: reads the folder for every page: VERIFY the cost for > 500 songs; add a short-lived cache keyed by the folder mtime, optional).
  4. base menu.cpp: replace the fixed 32-entry array with a WINDOW of N=24 entries around the cursor (`_windowStart` = absolute index of item 0, `_songNames[24][24]`). Down at the last item and hasMore -> request `offset = _windowStart + count`; Up at the first item and `_windowStart > 0` -> request `offset = max(0, _windowStart - 12)` and re-fill (the page returned from the PC may hold fewer entries than 12, it does not matter: the window is rebuilt from what arrives and the cursor lands on the previous item by absolute index). The `_songListItems` array and the "Back" item stay; `SongListState::WaitingMore` gets the timeout from D12 (3 s, same scheduler `songListTimeoutScheduler`, menu.cpp:34-35). The absolute index is part of the state because the base must send the right offset after a back-and-forth.
  5. remote song_list_screen.cpp: D12 owns; protocol side: same window/offset rules (offset in received entries, u16).
- Depends on: version bump (no, the 2-byte offset is compatible without it; keep it in the bump for the docs). Used by D12 (remote UI) and the base paging task.
- Tests:
  - PC: test for the 2-byte offset (songs.cpp `buildSongListPage` with 600 codes; offset 300; offset past the end; hasMore; response count advance) in the existing songs tests (VERIFY file name: tests/test_songs*.cpp).
  - base host test: pure paging state machine in src/logic (new `song_window.hpp`: window start, request offsets for Down/Up, timeout) + test file in test/host.
  - remote host test: same with the remote's own list logic (D12).
  - sim: 300-song folder in the PC test data and a flow that scrolls to the end on both devices.
- Manual/facts: zakladna chapter ">32 songs" (line ~202 per the audit), ovladac chapter song list; facts/base.md bug row "song list cut at 32" -> removed; SYSTEM.md SYS-USE song list rows.
- Risk / open points:
  - RAM of the base: 24 x 24 B names = 576 B (current 32 x 24 + 33 items). VERIFY the STM32G431 headroom (`size` against sizeBudget in .mcu-profile.json).
  - The 1999 cap stays (Q1-2 song code rule, intended); "unlimited" means no firmware array limit, not more codes than the rule allows.
  - Variable page sizes make "Up by 12" inexact; the window rebuild by absolute index makes it harmless. VERIFY the Up/Down feel on the bench.
  - Old PC with 2-byte offset: reads byte 0 only; first 255 songs fine, then wrong pages (loop). Document; or make the base refuse to page past 255 unless the PC announced PROTO_VER >= 2 via KEEPALIVE_ACK-like info (the base does not read the PC's INFO; leave as documented risk).

### Recommended order inside phase A
1. A1 (with the version bump: PROTOCOL_VERSION, INFO PROTO_VER, constants/types/info headers, RxDedup window, nonce, re-enable both PM-13 tests). First because every later gate (`PROTO_VER >= 2`) needs it and Q2-11 says "NOW".
2. A2 (same KEEPALIVE_ACK / STATUS_RESP edit surface and tests as A1; Q3-14 says "with the nonce work").
3. A5 (small, INFO only; needed by B1/B3).
4. A3 (base display tracker, PC clear form; independent of the others but touches base display.cpp which C-tasks edit; do before C11).
5. A9 (independent; unblocks D12 and the base paging).
6. A4 (needs C10 lib/bootloader bump first; touches the bootloader and both fw update paths; do together with C10).
7. A6 (introduces SIGNAL_INFO 0x34; unblocks D8, D2).
8. A8 (needs A6's message; unblocks D11).
9. A7 last (largest: PC recorder exclusion, base Nahraj C3, remote info screen D10; uses the BUSY refusal from A8).
One protocol commit per task in midicontrol-protocol, but a SINGLE version number (2): do not bump PROTOCOL_VERSION again between A tasks. Consumers (base, remote, PC) bump lib/protocol once per merged group (suggested: after steps 1-3, after 4-6, after 7-9) so they never see a half-defined v2.
