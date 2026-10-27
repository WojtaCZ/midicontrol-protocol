#ifndef PROTOCOL_INFO_HPP
#define PROTOCOL_INFO_HPP

#include <cstddef>
#include <cstdint>

// Device information in INFO_RESP (headers only, no dependencies).
//
// GET_INFO  payload: (empty)
// INFO_RESP payload: a sequence of TLV fields [tag u8, len u8, value[len]], values little-endian.
// Every field is optional; a receiver skips tags it does not know (len tells how far), so fields can be added
// without a new message type. The whole payload stays within MAX_PAYLOAD_SIZE.

namespace protocol::info {

	enum class Tag : uint8_t {
		// Firmware / system (both nodes)
		REVISION      = 0x01, // ASCII git revision, e.g. "c880b14"
		BRANCH        = 0x02, // ASCII git branch (may be cut short)
		BUILT         = 0x03, // ASCII build or commit date + time, e.g. "2026/10/25 16:03:47"
		DIRTY         = 0x04, // u8: 1 = built from a working tree with uncommitted changes
		UPTIME_MS     = 0x05, // u32: ms since this boot (wraps after 49 days)
		RESET_CAUSE   = 0x06, // u8: (RCC_CSR >> 24) & 0xFE, bit1 OBL, 2 PIN, 3 POWER (BOR/PWR), 4 SOFTWARE, 5 IWDG, 6 WWDG, 7 LOWPOWER
		FAULT_COUNT   = 0x07, // u8: faults since power-on (base: count of the saved fault record, 0 = none)

		// Bluetooth (both nodes)
		BLE_FLAGS     = 0x10, // u8: see BleFlag
		BLE_PEER      = 0x11, // ASCII 12 hex digits: base = stored remote address, remote = address of the connected base
		BLE_NAME      = 0x12, // ASCII own device name
		BLE_PAIRING   = 0x13, // u8: base pairing state (0 idle, 1 scanning, 2 connecting, 3 bonding, 4 done, 5 failed)

		// Base
		DISPLAY       = 0x20, // u8: church display connected
		SSR           = 0x21, // u8: organ power (SSR) on

		// Remote
		BATTERY_MV    = 0x30, // u16: battery voltage, mV (0 = not measured yet)
		BATTERY_LEVEL = 0x31, // u8: battery icon step 0 = empty .. 4 = full (for display use BATTERY_PERCENT)
		CHARGER       = 0x32, // u8: see ChargerFlag
		VBUS_MV       = 0x33, // u16: USB 5 V rail, mV
		BATTERY_PERCENT = 0x34, // u8: state of charge 0..100 % (from the filtered voltage)
	};

	enum BleFlag : uint8_t {
		BLE_CONNECTED   = 1u << 0, // module reports a connection
		BLE_STREAM_OPEN = 1u << 1, // transparent UART stream open
		BLE_SECURED     = 1u << 2, // base: peer accepted (secured link with the stored remote); remote: link secured
		BLE_BONDED      = 1u << 3, // %BONDED% seen / bond stored
		BLE_MODULE_UP   = 1u << 4, // RN4870 configured and usable
		BLE_PEER_ALIVE  = 1u << 5, // base: remote heard within the keepalive window; remote: base keepalive fresh
		BLE_PC_ALIVE    = 1u << 6, // the PC is alive as this node sees it
	};

	enum ChargerFlag : uint8_t {
		CHG_CHARGING = 1u << 0,
		CHG_DONE     = 1u << 1,
		CHG_VBUS     = 1u << 2, // USB supply present
		CHG_CRITICAL = 1u << 3, // battery below the cut-off
	};

	// Writes TLV fields into a caller buffer; a field that does not fit is dropped whole (len() stays valid).
	class Writer {
	public:
		Writer(uint8_t *buf, size_t cap) : _buf(buf), _cap(cap) {}

		bool bytes(Tag tag, const void *data, size_t n) {
			if (n > 255 || _len + 2 + n > _cap) return false;
			_buf[_len++] = static_cast<uint8_t>(tag);
			_buf[_len++] = static_cast<uint8_t>(n);
			const uint8_t *p = static_cast<const uint8_t *>(data);
			for (size_t i = 0; i < n; ++i) _buf[_len++] = p[i];
			return true;
		}
		bool u8(Tag tag, uint8_t v) { return bytes(tag, &v, 1); }
		bool u16(Tag tag, uint16_t v) {
			const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
			return bytes(tag, b, 2);
		}
		bool u32(Tag tag, uint32_t v) {
			const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
			return bytes(tag, b, 4);
		}
		// NUL-terminated string, cut to maxLen characters (not sent when empty)
		bool str(Tag tag, const char *s, size_t maxLen = 32) {
			size_t n = 0;
			while (s && s[n] && n < maxLen) ++n;
			return n ? bytes(tag, s, n) : false;
		}

		size_t len() const { return _len; }

	private:
		uint8_t *_buf;
		size_t _cap;
		size_t _len = 0;
	};

} // namespace protocol::info

#endif
