#pragma once
/*
  Сущности для настроек привода Nice по BusT4:
    number  — числовые параметры (время паузы, усилие, скорость...)
    switch  — функции вкл/выкл (автозакрытие, предмерцание...)
    select  — параметры-режимы со списком значений
    sensor  — только чтение (счётчик манёвров, порог обслуживания...)
    button  — калибровки и служебные команды
  Значение всегда берётся из привода: после записи параметр перечитывается.
*/
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/button/button.h"
#include "nice-bust4.h"

namespace esphome {
namespace bus_t4 {

// big-endian значение из 1..4 байт
inline uint32_t bus_t4_decode(const std::vector<uint8_t> &data, uint8_t nbytes) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < nbytes && i < data.size(); i++) v = (v << 8) | data[i];
  return v;
}
inline std::vector<uint8_t> bus_t4_encode(uint32_t v, uint8_t nbytes) {
  std::vector<uint8_t> out(nbytes);
  for (int i = nbytes - 1; i >= 0; i--) { out[i] = v & 0xFF; v >>= 8; }
  return out;
}

class BusT4Number : public number::Number, public Component, public BusT4Param {
 public:
  void set_parent(NiceBusT4 *p) { this->parent_ = p; }
  void set_bytes(uint8_t n) { this->nbytes_ = n; }
  void set_multiplier(float m) { this->multiplier_ = m; }
  void on_param_value(const std::vector<uint8_t> &data) override {
    this->publish_state(bus_t4_decode(data, this->nbytes_) * this->multiplier_);
  }
 protected:
  void control(float value) override {
    uint32_t raw = (uint32_t) lroundf(value / this->multiplier_);
    this->parent_->write_param(this->reg_, bus_t4_encode(raw, this->nbytes_));
  }
  NiceBusT4 *parent_{nullptr};
  uint8_t nbytes_{1};
  float multiplier_{1.0f};
};

class BusT4Switch : public switch_::Switch, public Component, public BusT4Param {
 public:
  void set_parent(NiceBusT4 *p) { this->parent_ = p; }
  void on_param_value(const std::vector<uint8_t> &data) override {
    if (!data.empty()) this->publish_state(data[0] != 0);
  }
 protected:
  void write_state(bool state) override {
    // состояние не публикуем сразу — его подтвердит ответ привода на GET
    this->parent_->write_param(this->reg_, {(uint8_t) (state ? 0x01 : 0x00)});
  }
  NiceBusT4 *parent_{nullptr};
};

class BusT4Select : public select::Select, public Component, public BusT4Param {
 public:
  void set_parent(NiceBusT4 *p) { this->parent_ = p; }
  // подписи вариантов и соответствующие им значения регистра (в одном порядке)
  void add_choice(const std::string &label, uint8_t value) {
    this->labels_.push_back(label);
    this->values_.push_back(value);
  }
  void on_param_value(const std::vector<uint8_t> &data) override {
    if (data.empty()) return;
    for (size_t i = 0; i < this->values_.size(); i++) {
      if (this->values_[i] == data[0]) { this->publish_state(this->labels_[i]); return; }
    }
    ESP_LOGW("bus_t4.select", "Регистр 0x%02X: неизвестное значение %u", this->reg_, data[0]);
  }
 protected:
  void control(const std::string &value) override {
    for (size_t i = 0; i < this->labels_.size(); i++) {
      if (this->labels_[i] == value) { this->parent_->write_param(this->reg_, {this->values_[i]}); return; }
    }
  }
  NiceBusT4 *parent_{nullptr};
  std::vector<std::string> labels_;
  std::vector<uint8_t> values_;
};

class BusT4Sensor : public sensor::Sensor, public Component, public BusT4Param {
 public:
  void set_parent(NiceBusT4 *p) { this->parent_ = p; }
  void set_bytes(uint8_t n) { this->nbytes_ = n; }
  void set_multiplier(float m) { this->multiplier_ = m; }
  void on_param_value(const std::vector<uint8_t> &data) override {
    this->publish_state(bus_t4_decode(data, this->nbytes_) * this->multiplier_);
  }
 protected:
  NiceBusT4 *parent_{nullptr};
  uint8_t nbytes_{1};
  float multiplier_{1.0f};
};

enum BusT4ButtonAction : uint8_t {
  BTN_LEARN_POSITIONS = 0,   // распознавание длины створки / поиск положений (SET 0x0B = 01)
  BTN_LEARN_BLUEBUS,         // распознавание устройств BlueBus (SET 0x0A = 01)
  BTN_CANCEL_MAINTENANCE,    // отмена обслуживания (SET 0xB4 = 01)
  BTN_REFRESH,               // перечитать все параметры
  BTN_SBS, BTN_STOP, BTN_OPEN, BTN_CLOSE, BTN_PARTIAL_OPEN,
};

class BusT4Button : public button::Button, public Component {
 public:
  void set_parent(NiceBusT4 *p) { this->parent_ = p; }
  void set_action(uint8_t a) { this->action_ = a; }
 protected:
  void press_action() override {
    switch (this->action_) {
      case BTN_LEARN_POSITIONS:    this->parent_->write_param(0x0B, {0x01}); break;
      case BTN_LEARN_BLUEBUS:      this->parent_->write_param(0x0A, {0x01}); break;
      case BTN_CANCEL_MAINTENANCE: this->parent_->write_param(C_MAIN, {0x01}); this->parent_->read_param(P_COUNT); break;
      case BTN_REFRESH:            this->parent_->read_all_params(); break;
      case BTN_SBS:                this->parent_->send_cmd(SBS); break;
      case BTN_STOP:               this->parent_->send_cmd(STOP); break;
      case BTN_OPEN:               this->parent_->send_cmd(OPEN); break;
      case BTN_CLOSE:              this->parent_->send_cmd(CLOSE); break;
      case BTN_PARTIAL_OPEN:       this->parent_->send_cmd(P_OPN1); break;
    }
  }
  NiceBusT4 *parent_{nullptr};
  uint8_t action_{BTN_REFRESH};
};

}  // namespace bus_t4
}  // namespace esphome
