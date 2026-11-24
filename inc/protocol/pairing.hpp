#ifndef PROTOCOL_PAIRING_HPP
#define PROTOCOL_PAIRING_HPP

#include <cstddef>
#include <cstdint>

// Pairing confirmation handshake between the base (initiator) and the remote (responder), headers only, no dependencies.
// Runs on a link that was just secured in a user-started pairing; until it succeeds the peer is only "pending" and nothing but
// PAIR_REQ / PAIR_RSP / PAIR_CONFIRM is accepted from it. Both users must press "Ano" and see the same code.
//
//   base   -> remote  PAIR_REQ     [ver u8 = 1, nonceB[4]]
//   remote -> base    PAIR_RSP     [ver u8 = 1, nonceR[4]]      (both now show the code and the Ano/Ne popup)
//   either -> other   PAIR_CONFIRM [accept u8, code u32 LE]     (after the local user decided; accept = 0 on Ne / timeout)
//   success = local Ano + a PAIR_CONFIRM(accept = 1) with the same code from the peer.
// This is user-intent confirmation on top of the module's Just Works pairing (SA,2), not protection against an active MITM.

namespace protocol::pairing {

	static constexpr uint8_t	 VERSION		 = 1;
	static constexpr size_t		 NONCE_LEN		 = 4;
	static constexpr size_t		 REQ_SIZE		 = 1 + NONCE_LEN;
	static constexpr size_t		 CONFIRM_SIZE	 = 5;
	static constexpr uint32_t	 REPLY_TIMEOUT_MS = 5000;  // the peer must answer (REQ / RSP) within this
	static constexpr uint32_t	 CONFIRM_TIMEOUT_MS = 30000; // the user must decide within this (counted from the shown code)
	static constexpr uint32_t	 CODE_MODULO	 = 1000000;

	// 6-digit code from both nonces (FNV-1a + avalanche); identical on both sides
	inline uint32_t code(const uint8_t nonceB[NONCE_LEN], const uint8_t nonceR[NONCE_LEN]) {
		uint32_t h = 2166136261u;
		const uint8_t tag[4] = {'M', 'C', 'P', '1'};
		for (uint8_t b : tag) h = (h ^ b) * 16777619u;
		for (size_t i = 0; i < NONCE_LEN; i++) h = (h ^ nonceB[i]) * 16777619u;
		for (size_t i = 0; i < NONCE_LEN; i++) h = (h ^ nonceR[i]) * 16777619u;
		h ^= h >> 15;
		h *= 2246822519u;
		h ^= h >> 13;
		return h % CODE_MODULO;
	}

	// Nonce from two weak entropy sources (tick counter, device id, ...): splitmix-style mixing. Not cryptographic, see above.
	inline void makeNonce(uint32_t seedA, uint32_t seedB, uint8_t out[NONCE_LEN]) {
		uint32_t x = seedA * 2654435761u ^ (seedB + 0x9E3779B9u);
		x ^= x >> 16;
		x *= 2146121005u;
		x ^= x >> 15;
		x *= 2221713035u;
		x ^= x >> 16;
		for (size_t i = 0; i < NONCE_LEN; i++) out[i] = static_cast<uint8_t>(x >> (8 * i));
	}

	enum class Role : uint8_t { INITIATOR, RESPONDER }; // base / remote
	enum class State : uint8_t { IDLE, AWAIT_PEER, CONFIRM, SUCCESS, FAILED };
	enum class Fail : uint8_t { NONE, TIMEOUT, REJECTED_LOCAL, REJECTED_PEER, MISMATCH, BAD_FRAME };
	enum class Step : uint8_t {
		NONE,
		SHOW,			// the code is known: show the popup (initiator: after PAIR_RSP)
		REPLY_AND_SHOW, // responder: send PAIR_RSP (rspPayload) and show the popup
		SUCCESS,		// both confirmed: store the bond
		FAIL,			// see fail(); notifyPeer() tells whether to send PAIR_CONFIRM(accept = 0)
	};

	// Message type codes duplicated from MsgType so this header stays dependency free (static_assert-ed in the host tests)
	static constexpr uint8_t T_REQ = 0x90, T_RSP = 0x91, T_CONFIRM = 0x92;

	class Session {
	  public:
		State state() const { return _state; }
		Fail  fail() const { return _fail; }
		bool  active() const { return _state == State::AWAIT_PEER || _state == State::CONFIRM; }
		uint32_t code() const { return _code; }
		bool localDecided() const { return _localDone; }

		// The peer is pending (secured in a user-started pairing). The initiator then sends reqPayload().
		void start(Role role, const uint8_t localNonce[NONCE_LEN], uint32_t nowMs) {
			_role = role;
			for (size_t i = 0; i < NONCE_LEN; i++) _mine[i] = localNonce[i];
			_state = State::AWAIT_PEER;
			_fail = Fail::NONE;
			_since = nowMs;
			_localDone = _localAccept = _peerAccept = _codeKnown = false;
			_code = 0;
		}
		void reset() { _state = State::IDLE; _fail = Fail::NONE; }

		// REQ (initiator) or RSP (responder) payload: [ver, own nonce]
		size_t hello(uint8_t *out) const {
			out[0] = VERSION;
			for (size_t i = 0; i < NONCE_LEN; i++) out[1 + i] = _mine[i];
			return REQ_SIZE;
		}
		// PAIR_CONFIRM payload for the local verdict
		size_t confirm(bool accept, uint8_t *out) const {
			out[0] = accept ? 1 : 0;
			for (size_t i = 0; i < 4; i++) out[1 + i] = static_cast<uint8_t>(_code >> (8 * i));
			return CONFIRM_SIZE;
		}
		// Whether the peer should be told about a failure (it did not cause it itself and the code is known)
		bool notifyPeer() const {
			return _state == State::FAILED && (_fail == Fail::REJECTED_LOCAL || _fail == Fail::MISMATCH || (_fail == Fail::TIMEOUT && _codeKnown));
		}

		// A PAIR_* frame from the peer
		Step onFrame(uint8_t type, const uint8_t *p, size_t n, uint32_t nowMs) {
			if (!active()) return Step::NONE;
			if (type == T_REQ && _role == Role::RESPONDER && _state == State::AWAIT_PEER) {
				if (n < REQ_SIZE || p[0] != VERSION) return failWith(Fail::BAD_FRAME);
				computeCode(p + 1, _mine);
				_state = State::CONFIRM;
				_since = nowMs;
				return Step::REPLY_AND_SHOW;
			}
			if (type == T_RSP && _role == Role::INITIATOR && _state == State::AWAIT_PEER) {
				if (n < REQ_SIZE || p[0] != VERSION) return failWith(Fail::BAD_FRAME);
				computeCode(_mine, p + 1);
				_state = State::CONFIRM;
				_since = nowMs;
				return Step::SHOW;
			}
			if (type == T_CONFIRM && _state == State::CONFIRM) {
				if (n < CONFIRM_SIZE) return failWith(Fail::BAD_FRAME);
				if (p[0] == 0) return failWith(Fail::REJECTED_PEER);
				uint32_t c = static_cast<uint32_t>(p[1]) | static_cast<uint32_t>(p[2]) << 8 | static_cast<uint32_t>(p[3]) << 16 | static_cast<uint32_t>(p[4]) << 24;
				if (c != _code) return failWith(Fail::MISMATCH);
				_peerAccept = true;
				return finishIfBoth();
			}
			return failWith(Fail::BAD_FRAME); // anything else from a pending peer ends the handshake
		}

		// The local user pressed Ano / Ne (only meaningful in CONFIRM)
		Step onLocal(bool accept) {
			if (_state != State::CONFIRM || _localDone) return Step::NONE;
			_localDone = true;
			_localAccept = accept;
			if (!accept) return failWith(Fail::REJECTED_LOCAL);
			return finishIfBoth();
		}

		Step poll(uint32_t nowMs) {
			if (_state == State::AWAIT_PEER && nowMs - _since >= REPLY_TIMEOUT_MS) return failWith(Fail::TIMEOUT);
			if (_state == State::CONFIRM && nowMs - _since >= CONFIRM_TIMEOUT_MS) return failWith(Fail::TIMEOUT);
			return Step::NONE;
		}

	  private:
		void computeCode(const uint8_t *nonceB, const uint8_t *nonceR) {
			_code = pairing::code(nonceB, nonceR);
			_codeKnown = true;
		}
		Step failWith(Fail f) {
			_state = State::FAILED;
			_fail = f;
			return Step::FAIL;
		}
		Step finishIfBoth() {
			if (_localDone && _localAccept && _peerAccept) {
				_state = State::SUCCESS;
				return Step::SUCCESS;
			}
			return Step::NONE;
		}

		Role	 _role = Role::INITIATOR;
		State	 _state = State::IDLE;
		Fail	 _fail = Fail::NONE;
		uint32_t _since = 0;
		uint32_t _code = 0;
		uint8_t	 _mine[NONCE_LEN] = {};
		bool	 _codeKnown = false;
		bool	 _localDone = false, _localAccept = false, _peerAccept = false;
	};

} // namespace protocol::pairing

#endif
