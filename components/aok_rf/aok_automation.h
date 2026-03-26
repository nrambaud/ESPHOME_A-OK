#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/remote_base/remote_base.h"
#include "aok_protocol.h"

namespace esphome {
namespace aok_rf {

// ─── Receive Trigger ──────────────────────────────────────────────────────────
// Attached to AOKReceiver; fires when a valid A-OK packet is decoded.
// Optional filters: remote_id, address, command.

class AOKTrigger : public Trigger<AOKData> {
 public:
  void process(const AOKData &data) {
    if (has_remote_id_ && data.remote_id != remote_id_) return;
    if (has_address_   && data.address   != address_)   return;
    if (has_command_   && data.command   != command_)   return;
    this->trigger(data);
  }

  void set_remote_id(uint32_t v) { remote_id_ = v; has_remote_id_ = true; }
  void set_address  (uint16_t v) { address_   = v; has_address_   = true; }
  void set_command  (uint8_t  v) { command_   = v; has_command_   = true; }

 protected:
  uint32_t remote_id_{0};
  uint16_t address_{0};
  uint8_t  command_{0};
  bool has_remote_id_{false};
  bool has_address_{false};
  bool has_command_{false};
};

// ─── Receiver component ───────────────────────────────────────────────────────
// Registered as a RemoteReceiverListener on the remote_receiver component.
// Decodes every pulse train, logs valid A-OK packets, fires AOKTriggers.

class AOKReceiver : public Component,
                    public remote_base::RemoteReceiverListener {
 public:
  bool on_receive(remote_base::RemoteReceiveData src) override {
    AOKProtocol proto;
    auto data = proto.decode(src);
    if (!data.has_value())
      return false;

    // dump() already logs the packet via ESP_LOGI
    proto.dump(*data);

    for (auto *trigger : triggers_)
      trigger->process(*data);

    return true;
  }

  void add_trigger(AOKTrigger *t) { triggers_.push_back(t); }

 protected:
  std::vector<AOKTrigger *> triggers_;
};

// ─── Transmit Action ──────────────────────────────────────────────────────────
// Usage in YAML:
//   - remote_transmitter.transmit_aok:
//       transmitter_id: rf_tx
//       remote_id: 0xABCDEF
//       address: 0x0001
//       command: DOWN
//       repeat:
//         times: 5
//         wait_time: 10ms

template<typename... Ts>
class AOKAction : public Action<Ts...> {
 public:
  explicit AOKAction(remote_base::RemoteTransmitterBase *transmitter)
      : transmitter_(transmitter) {}

  TEMPLATABLE_VALUE(uint32_t, remote_id)
  TEMPLATABLE_VALUE(uint16_t, address)
  TEMPLATABLE_VALUE(uint8_t,  command)
  TEMPLATABLE_VALUE(uint32_t, send_times)
  TEMPLATABLE_VALUE(uint32_t, send_wait)

  void play(Ts... x) override {
    AOKData data;
    data.remote_id = this->remote_id_.value(x...);
    data.address   = this->address_.value(x...);
    data.command   = this->command_.value(x...);

    auto call = this->transmitter_->transmit();
    AOKProtocol proto;
    proto.encode(call.get_data(), data);
    if (this->send_times_.has_value()) {
      call.set_send_times(this->send_times_.value(x...));
    }
    if (this->send_wait_.has_value()) {
      call.set_send_wait(this->send_wait_.value(x...));
    }
    call.perform();
  }

 protected:
  remote_base::RemoteTransmitterBase *transmitter_;
};

}  // namespace aok_rf
}  // namespace esphome
