#include "protocol/parser.hpp"
#include "protocol/codec.hpp"

namespace protocol {

	bool Parser::feed(uint8_t byte) {
		switch (_state) {
			case State::IDLE:
				if (byte == FRAME_START) {
					_state = State::RECEIVING;
					_bufferIndex = 0;
				}
				// Ignore other bytes outside a frame
				return false;

			case State::RECEIVING:
				if (byte == FRAME_END) {
					_state = State::IDLE;
					// Try to decode the buffer contents
					if (_bufferIndex > 0 && decodeFrame(_buffer, _bufferIndex, _packet)) {
						return true;
					}
					// Invalid frame — discard it
					return false;
				}

				if (byte == FRAME_START) {
					// A new frame is starting — discard the one in progress
					_bufferIndex = 0;
					return false;
				}

				// Normal data byte
				if (_bufferIndex < MAX_FRAME_SIZE) {
					_buffer[_bufferIndex++] = byte;
				} else {
					// Overflow — reset
					_state = State::IDLE;
					_bufferIndex = 0;
				}
				return false;
		}

		return false;
	}

	void Parser::reset() {
		_state = State::IDLE;
		_bufferIndex = 0;
	}

} // namespace protocol
