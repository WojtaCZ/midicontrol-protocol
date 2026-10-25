#ifndef PROTOCOL_CODEC_HPP
#define PROTOCOL_CODEC_HPP

#include <cstdint>
#include "protocol/types.hpp"

namespace protocol {

	// Encodes a packet into the wire format (with escaping and framing).
	// Returns the number of bytes written to the output buffer.
	uint16_t encode(const Packet &pkt, uint8_t *output);

	// Decodes a raw frame (without FRAME_START/FRAME_END) back into a packet.
	// raw points to the data between FRAME_START and FRAME_END (escaped).
	// Returns true if the CRC validates.
	bool decodeFrame(const uint8_t *raw, uint16_t len, Packet &pkt);

} // namespace protocol

#endif
