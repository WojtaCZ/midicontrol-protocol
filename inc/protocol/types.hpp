#ifndef PROTOCOL_TYPES_HPP
#define PROTOCOL_TYPES_HPP

#include <cstdint>
#include "protocol/constants.hpp"

namespace protocol {

	enum class MsgType : uint8_t {
		ACK                = 0x01,
		NAK                = 0x02,

		PLAY_SONG          = 0x10,
		STOP_SONG          = 0x11,
		RECORD_SONG        = 0x12,
		NOW_PLAYING_INFO   = 0x13,
		REPLAY_ANCHOR      = 0x14, // remote → base: replay section from last anchor to now; no payload

		GET_SONG_LIST      = 0x20,
		SONG_LIST_RESP     = 0x21,

		SIGNAL_PLAYING     = 0x30,
		SIGNAL_RECORDING   = 0x31,
		SIGNAL_STOPPED     = 0x32,
		SIGNAL_ANCHOR      = 0x33, // base → remote: named replay anchor reached; payload = null-terminated UTF-8 name

		SET_CURRENT        = 0x40,

		GET_STATUS         = 0x50,
		STATUS_RESP        = 0x51,

		KEEPALIVE          = 0x60,
		KEEPALIVE_ACK      = 0x61,

		// Firmware update (OTA over BLE for the remote, USB for the base)
		FW_UPDATE_START    = 0x70, // payload: target(1) + size(4) + crc32(4) + version[...]
		FW_UPDATE_DATA     = 0x71, // payload: offset(4) + chunk(<=120)
		FW_UPDATE_END      = 0x72, // finalize + verify; device applies/reboots
		FW_UPDATE_STATUS   = 0x73, // device -> PC progress / result
		ENTER_BOOTLOADER   = 0x74, // base only: app reboots into its USB bootloader
	};

	// FW_UPDATE_STATUS payload codes (single byte)
	enum class FwStatus : uint8_t {
		READY      = 0x00, // ready to receive (response to FW_UPDATE_START)
		IN_PROGRESS= 0x01,
		CRC_OK     = 0x02, // image verified, will apply now
		CRC_FAIL   = 0x03,
		WRITE_ERROR= 0x04,
		BAD_SIZE   = 0x05,
	};

	enum class NakReason : uint8_t {
		UNKNOWN_MSG        = 0x01,
		INVALID_PAYLOAD    = 0x02,
		CRC_ERROR          = 0x03,
		BUSY               = 0x04,
	};

	struct Packet {
		uint8_t source;
		uint8_t dest;
		uint8_t packetId;
		MsgType msgType;
		uint8_t payload[MAX_PAYLOAD_SIZE];
		uint8_t payloadLen;
	};

} // namespace protocol

#endif
