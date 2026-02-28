#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/remote_base/remote_base.h"

namespace esphome {
namespace aok_rf {

static const char *const TAG = "aok_rf";

// ─── Timing constants (µs) ────────────────────────────────────────────────────
// Base pulse ≈ 300 µs
static const uint32_t AOK_BASE_US      = 300;

// Sync: HIGH = 17× base (≈5100 µs), LOW = 2× base (≈600 µs)
static const uint32_t AOK_SYNC_HIGH_US = AOK_BASE_US * 17;  // 5100 µs
static const uint32_t AOK_SYNC_LOW_US  = AOK_BASE_US * 2;   //  600 µs

// Bit '0': HIGH = 1× base (300 µs), LOW = 2× base (600 µs)
static const uint32_t AOK_ZERO_HIGH_US = AOK_BASE_US * 1;   //  300 µs
static const uint32_t AOK_ZERO_LOW_US  = AOK_BASE_US * 2;   //  600 µs

// Bit '1': HIGH = 2× base (600 µs), LOW = 1× base (300 µs)
static const uint32_t AOK_ONE_HIGH_US  = AOK_BASE_US * 2;   //  600 µs
static const uint32_t AOK_ONE_LOW_US   = AOK_BASE_US * 1;   //  300 µs

// Preamble: 7 '0' bits emitted before the sync pulse
static const uint8_t  AOK_PREAMBLE_BITS  = 7;

// Inter-frame gap: 5100 µs silence between the 3 repeated frames (= 17× base)
static const uint32_t AOK_INTER_FRAME_US = AOK_BASE_US * 17;  // 5100 µs

// ─── Commands (values confirmed by sniffing a real A-OK remote) ───────────────
enum AOKCommand : uint8_t {
  AOK_CMD_UP      = 0x0B,
  AOK_CMD_STOP    = 0x23,
  AOK_CMD_DOWN    = 0x43,
  AOK_CMD_PROGRAM = 0x53,
};

// ─── Packet data ──────────────────────────────────────────────────────────────
struct AOKData {
  uint32_t remote_id;  // 24 bits
  uint16_t address;    // 16 bits (channel bitmask)
  uint8_t  command;    //  8 bits

  uint8_t checksum() const {
    uint8_t s = 0;
    s += (remote_id >> 16) & 0xFF;
    s += (remote_id >>  8) & 0xFF;
    s += (remote_id)       & 0xFF;
    s += (address   >>  8) & 0xFF;
    s += (address)         & 0xFF;
    s += command;
    return s;
  }

  // Full 64-bit frame: 0xA3 | ID(24) | ADDR(16) | CMD(8) | CRC(8)
  uint64_t to_uint64() const {
    uint64_t v = 0;
    v |= (uint64_t)0xA3                     << 56;
    v |= (uint64_t)(remote_id & 0x00FFFFFF) << 32;
    v |= (uint64_t)address                  << 16;
    v |= (uint64_t)command                  <<  8;
    v |= (uint64_t)checksum();
    return v;
  }

  bool operator==(const AOKData &rhs) const {
    return remote_id == rhs.remote_id &&
           address   == rhs.address   &&
           command   == rhs.command;
  }
};

// ─── Protocol (encoder + decoder) ────────────────────────────────────────────
// Inherits from RemoteProtocol<AOKData> for encode/decode/dump interface.
// NOTE: DECLARE_REMOTE_PROTOCOL() is intentionally NOT used here — that macro
// requires remote_receiver_binary_sensor which is not available in this
// external-component context. AOKReceiver (aok_automation.h) handles
// reception as a plain RemoteReceiverListener instead.
class AOKProtocol : public remote_base::RemoteProtocol<AOKData> {
 public:
  void encode(remote_base::RemoteTransmitData *dst, const AOKData &data) override;
  optional<AOKData> decode(remote_base::RemoteReceiveData src) override;
  void dump(const AOKData &data) override;
};


}  // namespace aok_rf
}  // namespace esphome
