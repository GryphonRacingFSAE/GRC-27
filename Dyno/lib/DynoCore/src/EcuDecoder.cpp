#include "EcuDecoder.h"

// Little-endian (Intel) extraction: bit n of the frame is bit (n % 8) of
// byte (n / 8), and the signal's lowest bit is at start_bit.
static uint32_t extractBits(const uint8_t* data, uint8_t start_bit, uint8_t length) {
    uint64_t all = 0;
    for (int i = 7; i >= 0; --i) {
        all = (all << 8) | data[i];
    }
    const uint64_t mask = (uint64_t{1} << length) - 1;
    return static_cast<uint32_t>((all >> start_bit) & mask);
}

// Signed signals are two's complement within their own length: if the top
// bit is set, the value is bits - 2^length. Unsigned values are kept as-is
// (never squeezed into a signed 16-bit type: 60000 rpm stays 60000).
static int32_t toRaw(uint32_t bits, const EcuSignalDef& sig) {
    if (sig.is_signed && sig.length < 32 && (bits >> (sig.length - 1)) & 1u) {
        return static_cast<int32_t>(static_cast<int64_t>(bits) - (int64_t{1} << sig.length));
    }
    return static_cast<int32_t>(bits);
}

DecodeResult decodeEcuFrame(const CanFrame& frame, EcuState& state) {
    // Every check happens before anything in `state` is touched.
    if (frame.extended) {
        return DecodeResult::Extended; // 0x520 as a 29-bit ID is a different message
    }
    if (frame.remote) {
        return DecodeResult::Remote;
    }
    const EcuMessageDef* msg = findEcuMessage(frame.id);
    if (msg == nullptr) {
        return DecodeResult::UnknownId;
    }
    if (frame.dlc != 8) {
        return DecodeResult::BadLength;
    }
    if (frame.data == nullptr) {
        return DecodeResult::NullPayload;
    }

    for (const EcuSignalDef& sig : kEcuSignals) {
        if (sig.message == msg->message) {
            state.raw_values[static_cast<size_t>(sig.signal)] =
                toRaw(extractBits(frame.data, sig.start_bit, sig.length), sig);
        }
    }
    EcuMessageStatus& status = state.messages[static_cast<size_t>(msg->message)];
    status.received = true;
    status.last_rx_ms = frame.rx_ms;
    return DecodeResult::Accepted;
}
