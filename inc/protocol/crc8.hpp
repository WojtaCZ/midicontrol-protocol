#ifndef PROTOCOL_CRC8_HPP
#define PROTOCOL_CRC8_HPP

#include <cstdint>
#include <cstddef>

namespace protocol {

	// CRC-8: poly 0x31, init 0x00, MSB first (no reflection), no final XOR; check("123456789") = 0xA2.
	// Not CRC-8/MAXIM (reflected, check 0xA1).
	uint8_t crc8(const uint8_t *data, size_t len);

} // namespace protocol

#endif
