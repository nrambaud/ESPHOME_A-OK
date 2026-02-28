#include "aok_protocol.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aok_rf {

// ─── Encoder ──────────────────────────────────────────────────────────────────
// A full A-OK transmission consists of 3 identical frames:
//
//   Frame 1:  [7×'0' preamble][SYNC][64 data bits][trailing '1' LOW=5100µs]
//   Frame 2:  [SYNC][64 data bits][trailing '1' LOW=5100µs]
//   Frame 3:  [SYNC][64 data bits][trailing '1' LOW=300µs]
//
// The inter-frame silence is produced by extending the LOW phase of the
// trailing '1' bit to AOK_INTER_FRAME_US (5100 µs) on frames 1 and 2.
// No separate gap item is needed.

static void encode_frame_(remote_base::RemoteTransmitData *dst,
                           uint64_t bits, bool with_preamble,
                           bool last_frame = false) {
  if (with_preamble) {
    for (int i = 0; i < AOK_PREAMBLE_BITS; i++) {
      dst->item(AOK_ZERO_HIGH_US, AOK_ZERO_LOW_US);
    }
  }

  // Sync pulse
  dst->item(AOK_SYNC_HIGH_US, AOK_SYNC_LOW_US);

  // 64 data bits, MSB first
  for (int i = 63; i >= 0; i--) {
    if ((bits >> i) & 1u) {
      dst->item(AOK_ONE_HIGH_US, AOK_ONE_LOW_US);
    } else {
      dst->item(AOK_ZERO_HIGH_US, AOK_ZERO_LOW_US);
    }
  }

  // Trailing '1':
  //   - on frames 1 & 2: LOW = AOK_INTER_FRAME_US (5100 µs) → produces the gap
  //   - on frame 3 (last): LOW = AOK_ONE_LOW_US (300 µs) → normal end
  uint32_t trailing_low = last_frame ? AOK_ONE_LOW_US : AOK_INTER_FRAME_US;
  dst->item(AOK_ONE_HIGH_US, trailing_low);
}

void AOKProtocol::encode(remote_base::RemoteTransmitData *dst, const AOKData &data) {
  uint64_t bits = data.to_uint64();

  // Item count:
  //   Frame 1: 7×2 (preamble) + 2 (sync) + 64×2 (data) + 2 (trailing) = 148
  //   Frame 2: 2 + 128 + 2 = 132
  //   Frame 3: 132
  //   Total: 148 + 132 + 132 = 412 items
  dst->reserve(412);
  dst->set_carrier_frequency(0);  // OOK — no carrier

  encode_frame_(dst, bits, true,  false);  // frame 1: preamble, gap trailing
  encode_frame_(dst, bits, false, false);  // frame 2: no preamble, gap trailing
  encode_frame_(dst, bits, false, true);   // frame 3: no preamble, normal trailing
}

// ─── Decoder ──────────────────────────────────────────────────────────────────
// The decoder handles a single frame (remote_receiver captures one frame at a
// time, delimited by the idle gap). It skips any leading '0' preamble bits
// before looking for the sync pulse.
//
// expect_item(mark, space) uses the tolerance configured on the
// remote_receiver component (tolerance: 40% in the YAML).
optional<AOKData> AOKProtocol::decode(remote_base::RemoteReceiveData src) {
  // ── Scan for sync pulse ───────────────────────────────────────────────────
  // Skip leading '0' preamble bits (present only on frame 1).
  bool sync_found = false;
  for (int skip = 0; skip <= AOK_PREAMBLE_BITS + 3; skip++) {
    if (src.expect_item(AOK_SYNC_HIGH_US, AOK_SYNC_LOW_US)) {
      sync_found = true;
      break;
    }
    if (!src.expect_item(AOK_ZERO_HIGH_US, AOK_ZERO_LOW_US)) {
      break;  // not a '0' bit either — give up
    }
    ESP_LOGV(TAG, "AOK decode: skipped leading '0' bit (%d)", skip + 1);
  }

  if (!sync_found) {
    return {};
  }

  // ── 64 data bits, MSB first ───────────────────────────────────────────────
  uint64_t bits = 0;
  for (int i = 63; i >= 0; i--) {
    if (src.expect_item(AOK_ONE_HIGH_US, AOK_ONE_LOW_US)) {
      bits |= (1ULL << i);
    } else if (src.expect_item(AOK_ZERO_HIGH_US, AOK_ZERO_LOW_US)) {
      // bit stays 0
    } else {
      ESP_LOGV(TAG, "AOK decode: invalid timing at bit %d", i);
      return {};
    }
  }

  // ── Trailing '1' — consume if present ────────────────────────────────────
  // The LOW phase may be 300 µs (last frame) or 5100 µs (inter-frame gap).
  // We simply consume the HIGH and ignore the LOW duration.
  src.expect_item(AOK_ONE_HIGH_US, AOK_ONE_LOW_US);

  // ── Validate start byte ───────────────────────────────────────────────────
  uint8_t start = (bits >> 56) & 0xFF;
  if (start != 0xA3) {
    ESP_LOGV(TAG, "AOK decode: bad start byte 0x%02X (expected 0xA3)", start);
    return {};
  }

  // ── Unpack fields ─────────────────────────────────────────────────────────
  AOKData data;
  data.remote_id   = (bits >> 32) & 0x00FFFFFF;
  data.address     = (bits >> 16) & 0xFFFF;
  data.command     = (bits >>  8) & 0xFF;
  uint8_t rx_crc   =  bits        & 0xFF;
  uint8_t calc_crc = data.checksum();

  if (rx_crc != calc_crc) {
    ESP_LOGW(TAG, "AOK decode: checksum mismatch rx=0x%02X calc=0x%02X", rx_crc, calc_crc);
    return {};
  }

  return data;
}

// ─── Dump (logger) ────────────────────────────────────────────────────────────
void AOKProtocol::dump(const AOKData &data) {
  const char *cmd_str = "UNKNOWN";
  switch (data.command) {
    case AOK_CMD_UP:      cmd_str = "UP";      break;
    case AOK_CMD_STOP:    cmd_str = "STOP";    break;
    case AOK_CMD_DOWN:    cmd_str = "DOWN";    break;
    case AOK_CMD_PROGRAM: cmd_str = "PROGRAM"; break;
  }
  ESP_LOGI(TAG, "Received A-OK: remote_id=0x%06X address=0x%04X command=%s (0x%02X)",
           data.remote_id, data.address, cmd_str, data.command);
}

}  // namespace aok_rf
}  // namespace esphome
