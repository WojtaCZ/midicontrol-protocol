#ifndef PROTOCOL_CONSTANTS_HPP
#define PROTOCOL_CONSTANTS_HPP

#include <cstdint>

namespace protocol {

	// Oddělovače rámce (neescapované)
	static constexpr uint8_t FRAME_START = 0x01; // SOH
	static constexpr uint8_t FRAME_END   = 0x04; // EOT

	// Escape prefix
	static constexpr uint8_t ESC = 0x1B;

	// Escapované sekvence (bajty po ESC)
	static constexpr uint8_t ESC_FRAME_START = 0x21; // ESC 0x21 → 0x01
	static constexpr uint8_t ESC_FRAME_END   = 0x22; // ESC 0x22 → 0x04
	static constexpr uint8_t ESC_ESC         = 0x23; // ESC 0x23 → 0x1B
	static constexpr uint8_t ESC_DOLLAR      = 0x26; // ESC 0x26 → 0x24 ($)
	static constexpr uint8_t ESC_PERCENT     = 0x27; // ESC 0x27 → 0x25 (%)

	// Zakázané bajty na lince
	static constexpr uint8_t FORBIDDEN_DOLLAR  = 0x24; // $ — RN4870 Command Mode
	static constexpr uint8_t FORBIDDEN_PERCENT = 0x25; // % — RN4870 Status delimiter

	// Adresy uzlů
	static constexpr uint8_t ADDR_PC        = 0x01;
	static constexpr uint8_t ADDR_BASE      = 0x02;
	static constexpr uint8_t ADDR_REMOTE    = 0x03;
	static constexpr uint8_t ADDR_BROADCAST = 0xFF;

	// Velikosti
	static constexpr uint8_t  MAX_PAYLOAD_SIZE = 128;
	static constexpr uint16_t MAX_FRAME_SIZE   = 2 + (1 + 4 + MAX_PAYLOAD_SIZE + 1) * 2; // FRAME_START + worst-case escaped (Length + header + payload + CRC) + FRAME_END
	static constexpr uint8_t  HEADER_SIZE      = 4; // Source + Dest + PacketID + MsgType

	// Opakování a timeout
	static constexpr uint8_t  MAX_RETRIES       = 3;
	static constexpr uint16_t RETRY_TIMEOUT_MS   = 500;

	// Keepalive: PC → Base a Base → Remote, protistrana je ztracena po KEEPALIVE_LOSS_MS bez odpovědi
	static constexpr uint16_t KEEPALIVE_PERIOD_MS = 2000;
	static constexpr uint16_t KEEPALIVE_LOSS_MS   = 6000;

	// Kód písně v PLAY_SONG / SIGNAL_PLAYING: ASCII [0-9A-Z], např. "406A"
	static constexpr uint8_t MAX_SONG_CODE_LEN = 8;

	// NOW_PLAYING_INFO: hodnoty polí
	static constexpr uint8_t NPI_NO_CHANGE = 0xFF; // pole se nemění
	static constexpr uint8_t NPI_BLANK     = 0xFE; // pole zhasnout (číslo písně / sloka)
	static constexpr uint8_t NPI_DIGIT_BLANK = 0x0F; // BCD nibble: prázdná číslice

} // namespace protocol

#endif
