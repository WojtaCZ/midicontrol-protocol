#ifndef PROTOCOL_PARSER_HPP
#define PROTOCOL_PARSER_HPP

#include <cstdint>
#include "protocol/types.hpp"
#include "protocol/constants.hpp"

namespace protocol {

	class Parser {
	public:
		// Accepts one byte from the line. Returns true when a complete frame is ready.
		bool feed(uint8_t byte);

		// Returns the most recently received and decoded packet.
		const Packet &getPacket() const { return _packet; }

		// Resets the parser to its initial state
		void reset();

	private:
		enum class State : uint8_t {
			IDLE,       // Waiting for FRAME_START
			RECEIVING,  // Receiving data between FRAME_START and FRAME_END
		};

		State _state = State::IDLE;
		uint8_t _buffer[MAX_FRAME_SIZE];
		uint16_t _bufferIndex = 0;
		Packet _packet = {};
	};

} // namespace protocol

#endif
