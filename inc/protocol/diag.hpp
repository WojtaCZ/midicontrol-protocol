#ifndef PROTOCOL_DIAG_HPP
#define PROTOCOL_DIAG_HPP

#include <cstddef>
#include <cstdint>

// Diagnostic records in DIAG_RESP (headers only, no dependencies).
//
// Record on the wire, 12 B, little-endian:
//   seq u16, code u8, level u8, uptimeMs u32, arg u32
// DIAG_GET  payload: [cursor u16 LE]
// DIAG_RESP payload: [boot_id u16 LE, next_cursor u16 LE, n u8, n × record], n <= MAX_RECORDS

namespace protocol::diag {

	static constexpr size_t RECORD_SIZE = 12;
	static constexpr size_t MAX_RECORDS = 8;
	static constexpr size_t RESP_HEADER_SIZE = 5;
	static constexpr size_t MAX_RESP_PAYLOAD = RESP_HEADER_SIZE + MAX_RECORDS * RECORD_SIZE; // 101

	struct Record {
		uint16_t seq;
		uint8_t code;
		uint8_t level;
		uint32_t uptimeMs;
		uint32_t arg;
	};

	enum class Code : uint8_t {
		BOOT             = 1,  // arg = reset cause
		FAULT            = 2,  // arg = fault kind (node-specific), pc is in FAULT_DETAIL
		FAULT_DETAIL     = 3,  // pc, lr, xpsr, ipsr follow as records of this code
		LOOP_STALL       = 4,  // arg = pc of the stalled main loop
		IWDG_RESET       = 5,
		BLE_CONNECT      = 6,
		BLE_DISCONNECT   = 7,
		BLE_SECURED      = 8,
		LINK_LOST        = 9,
		RETRIES_EXHAUSTED = 10, // arg = msgType
		CRC_ERRORS       = 11, // arg = count
		CHARGER_FAULT    = 12, // arg = charger REG09
		LOW_BATTERY      = 13, // arg = mV
		OTA_RESULT       = 14, // arg = FwStatus
		SHUTDOWN         = 15, // arg = reason
		RECORDS_LOST     = 16, // arg = number of lost records
	};

	enum class Level : uint8_t { ERROR = 1, WARNING = 2, INFO = 3, DEBUG = 4 };

	inline void encodeRecord(const Record &r, uint8_t *out12) {
		out12[0] = static_cast<uint8_t>(r.seq);
		out12[1] = static_cast<uint8_t>(r.seq >> 8);
		out12[2] = r.code;
		out12[3] = r.level;
		for (int i = 0; i < 4; ++i) {
			out12[4 + i] = static_cast<uint8_t>(r.uptimeMs >> (8 * i));
			out12[8 + i] = static_cast<uint8_t>(r.arg >> (8 * i));
		}
	}

	inline Record decodeRecord(const uint8_t *in12) {
		Record r{};
		r.seq = static_cast<uint16_t>(in12[0] | (in12[1] << 8));
		r.code = in12[2];
		r.level = in12[3];
		for (int i = 0; i < 4; ++i) {
			r.uptimeMs |= static_cast<uint32_t>(in12[4 + i]) << (8 * i);
			r.arg |= static_cast<uint32_t>(in12[8 + i]) << (8 * i);
		}
		return r;
	}

} // namespace protocol::diag

#endif
