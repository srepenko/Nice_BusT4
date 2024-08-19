#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/cover/cover.h"

namespace esphome {
namespace empty_uart_component {

class EmptyUARTComponent : public uart::UARTDevice, public Component, public Cover{
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
};


}  // namespace empty_uart_component
}  // namespace esphome