#pragma once

#include "esphome/core/automation.h"
#include "nice-bust4.h"

namespace esphome {
namespace bus_t4 {

// Действие для отправки произвольной HEX-команды через автоматизацию ESPHome.
// Пример использования в YAML:
//   on_press:
//     - bus_t4.raw_command:
//         id: nice_cover
//         raw_cmd: "55 0c 00 03 00 81 01 05 86 01 82 01 64 e6 0c"
template<typename... Ts>
class RawCmdAction : public Action<Ts...>, public Parented<NiceBusT4> {
 public:
  TEMPLATABLE_VALUE(std::string, raw_cmd)

  void play(Ts... x) override {
    this->parent_->send_raw_cmd(this->raw_cmd_.value(x...));
  }
};

}  // namespace bus_t4
}  // namespace esphome
