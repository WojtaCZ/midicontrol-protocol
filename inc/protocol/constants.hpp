#ifndef PROTOCOL_CONSTANTS_HPP
#define PROTOCOL_CONSTANTS_HPP

#include <cstdint>

namespace protocol {

	// Wire protocol version, reported by each node in INFO_RESP (info::Tag::PROTO_VER). v1 = no version field (implicit),
	// v2 = session nonce in KEEPALIVE and the other additions listed in PROTOCOL.md "Protocol version".
	static constexpr uint8_t PROTOCOL_VERSION = 2;

	// Frame delimiters (unescaped)
	static constexpr uint8_t FRAME_START = 0x01; // SOH
	static constexpr uint8_t FRAME_END   = 0x04; // EOT

	// Escape prefix
	static constexpr uint8_t ESC = 0x1B;

	// Escaped sequences (bytes following ESC)
	static constexpr uint8_t ESC_FRAME_START = 0x21; // ESC 0x21 → 0x01
	static constexpr uint8_t ESC_FRAME_END   = 0x22; // ESC 0x22 → 0x04
	static constexpr uint8_t ESC_ESC         = 0x23; // ESC 0x23 → 0x1B
	static constexpr uint8_t ESC_DOLLAR      = 0x26; // ESC 0x26 → 0x24 ($)
	static constexpr uint8_t ESC_PERCENT     = 0x27; // ESC 0x27 → 0x25 (%)

	// Forbidden bytes on the line
	static constexpr uint8_t FORBIDDEN_DOLLAR  = 0x24; // $ — RN4870 Command Mode
	static constexpr uint8_t FORBIDDEN_PERCENT = 0x25; // % — RN4870 Status delimiter

	// Node addresses
	static constexpr uint8_t ADDR_PC        = 0x01;
	static constexpr uint8_t ADDR_BASE      = 0x02;
	static constexpr uint8_t ADDR_REMOTE    = 0x03;
	static constexpr uint8_t ADDR_BROADCAST = 0xFF;

	// Sizes
	static constexpr uint8_t  MAX_PAYLOAD_SIZE = 128;
	static constexpr uint16_t MAX_FRAME_SIZE   = 2 + (1 + 4 + MAX_PAYLOAD_SIZE + 1) * 2; // FRAME_START + worst-case escaped (Length + header + payload + CRC) + FRAME_END
	static constexpr uint8_t  HEADER_SIZE      = 4; // Source + Dest + PacketID + MsgType

	// Retries and timeout
	static constexpr uint8_t  MAX_RETRIES       = 3;
	static constexpr uint16_t RETRY_TIMEOUT_MS   = 500;

	// Keepalive: PC → Base and Base → Remote; the peer is considered lost after KEEPALIVE_LOSS_MS without a response
	static constexpr uint16_t KEEPALIVE_PERIOD_MS = 2000;
	static constexpr uint16_t KEEPALIVE_LOSS_MS   = 6000;

	// Song code in PLAY_SONG / SIGNAL_PLAYING: 1-4 ASCII digits, value at most 1999, plus an optional letter A-D,
	// e.g. "406A" (rule in docs/SYSTEM.md SYS-USE-22, Q1-2 a). MAX_SONG_CODE_LEN is only the frame bound.
	static constexpr uint8_t MAX_SONG_CODE_LEN = 8;

	// NOW_PLAYING_INFO: field values
	static constexpr uint8_t NPI_NO_CHANGE = 0xFF; // field unchanged
	static constexpr uint8_t NPI_BLANK     = 0xFE; // blank the field (song number / verse)
	static constexpr uint8_t NPI_DIGIT_BLANK = 0x0F; // BCD nibble: blank digit

} // namespace protocol

#endif
