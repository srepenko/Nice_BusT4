#include "nice-bust4.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <Arduino.h>
#include <HardwareSerial.h>

// На ESP32 используем аппаратное инвертирование TX для генерации break-сигнала.
// Это надёжнее смены baud-rate и одинаково работает на C3 / S3 / C6 / P4.
#ifdef USE_ESP32
#include <driver/uart.h>
#endif

namespace esphome {
namespace bus_t4 {

static const char *TAG = "bus_t4.cover";

using namespace esphome::cover;

CoverTraits NiceBusT4::get_traits() {
  auto traits = CoverTraits();
  traits.set_supports_position(true);
  traits.set_supports_stop(true);
  return traits;
}

/*
  дампы команд OVIEW

  SBS               55 0c 00 ff 00 66 01 05 9D 01 82 01 64 E6 0c
  STOP              55 0c 00 ff 00 66 01 05 9D 01 82 02 64 E5 0c
  OPEN              55 0c 00 ff 00 66 01 05 9D 01 82 03 00 80 0c
  CLOSE             55 0c 00 ff 00 66 01 05 9D 01 82 04 64 E3 0c
  PARENTAL OPEN 1   55 0c 00 ff 00 66 01 05 9D 01 82 05 64 E2 0c
  PARENTAL OPEN 2   55 0c 00 ff 00 66 01 05 9D 01 82 06 64 E1 0c
*/

// Безопасное вычисление позиции в диапазоне [0.0, 1.0].
float NiceBusT4::safe_position(uint16_t usl) const {
  if (this->_pos_opn == this->_pos_cls) {
    ESP_LOGW(TAG, "Некорректные граничные позиции (opn==cls=%d), позиция не обновлена", this->_pos_cls);
    return this->position;  // возвращаем последнее известное значение
  }
  float pos = (float)(usl - this->_pos_cls) / (float)(this->_pos_opn - this->_pos_cls);
  if (pos < 0.0f) pos = 0.0f;
  if (pos > 1.0f) pos = 1.0f;
  return pos;
}

void NiceBusT4::control(const CoverCall &call) {
  if (call.get_stop()) {
    this->tx_buffer_.push(gen_control_cmd(STOP));
    this->tx_buffer_.push(gen_inf_cmd(FOR_CU, INF_STATUS, GET));
    if (is_walky) {
      tx_buffer_.push(gen_inf_cmd((uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), FOR_CU, CUR_POS, GET, 0x00, {0x01}, 1));
    }
    else {
      this->tx_buffer_.push(gen_inf_cmd(FOR_CU, CUR_POS, GET));
    }

  } else if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    if (pos != this->position) {
      if (pos == COVER_OPEN) {
        this->tx_buffer_.push(gen_control_cmd(OPEN));

      } else if (pos == COVER_CLOSED) {
        this->tx_buffer_.push(gen_control_cmd(CLOSE));
      }
    }
  }
}

void NiceBusT4::setup() {
  Serial1.begin(BAUD_WORK, SERIAL_8N1, this->rx_pin, this->tx_pin);
}

void NiceBusT4::loop() {

    if ((millis() - this->last_update_) > POLL_INTERVAL_MS) {
        std::vector<uint8_t> unknown = {0x55, 0x55};
        if (this->init_ok == false) {
          this->tx_buffer_.push(gen_inf_cmd(0x00, 0xff, FOR_ALL, WHO, GET, 0x00));
          this->tx_buffer_.push(gen_inf_cmd(0x00, 0xff, FOR_ALL, PRD, GET, 0x00));
        }        
        else if (this->class_gate_ == 0x55) init_device((uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), 0x04);  
        else if (this->manufacturer_ == unknown)  {
         init_device((uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), 0x04);  
        }
        this->last_update_ = millis();
    }

  // Таймаут сборки пакета: если давно не было байт, но буфер не пуст — мусор, сбрасываем
  const uint32_t now = millis();
  if (!this->rx_message_.empty() && (now - this->last_rx_time_ > RX_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Таймаут RX, сброс буфера (%d байт)", (int)this->rx_message_.size());
    this->rx_message_.clear();
  }

  // разрешаем отправку каждые TX_INTERVAL_MS мс
  if (now - this->last_uart_byte_ > TX_INTERVAL_MS) {
    this->ready_to_tx_ = true;
    this->last_uart_byte_ = now;
  }

  while (Serial1.available() > 0) {
    uint8_t c = Serial1.read();
    this->last_rx_time_ = millis();
    this->handle_char_(c);
    this->last_uart_byte_ = millis();
  }

  if (this->ready_to_tx_) {
    if (!this->tx_buffer_.empty()) {
      this->send_array_cmd(this->tx_buffer_.front());
      this->tx_buffer_.pop();
      this->ready_to_tx_ = false;
    }
  }

} //loop


void NiceBusT4::handle_char_(uint8_t c) {
  this->rx_message_.push_back(c);
  if (!this->validate_message_()) {
    this->rx_message_.clear();
  }
}


bool NiceBusT4::validate_message_() {
  uint32_t at = this->rx_message_.size() - 1;
  uint8_t *data = &this->rx_message_[0];
  uint8_t new_byte = data[at];

  if (at == 0x00)
    return new_byte == 0x00;

  if (at == 1)
    return new_byte == START_CODE;

  if (at == 2)
    return true;

  uint8_t packet_size = data[2];
  uint8_t length = (packet_size + 3);

  if (at == 3)
    return true;

  if (at <= 8)
    return true;

  uint8_t crc1 = (data[3] ^ data[4] ^ data[5] ^ data[6] ^ data[7] ^ data[8]);

  if (at == 9)
    if (data[9] != crc1) {
      ESP_LOGW(TAG, "Checksum 1 error: %02X!=%02X", data[9], crc1);
      std::string pretty_cmd1 = format_hex_pretty(rx_message_);
      ESP_LOGD(TAG, "Получен пакет: %s", pretty_cmd1.c_str());
      return false;
    }

  if (at < length)
    return true;

  uint8_t crc2 = data[10];
  for (uint8_t i = 11; i < length - 1; i++) {
    crc2 = (crc2 ^ data[i]);
  }

  if (data[length - 1] != crc2 ) {
    ESP_LOGW(TAG, "Checksum 2 error: %02X!=%02X", data[length - 1], crc2);
    std::string pretty_cmd1 = format_hex_pretty(rx_message_);
    ESP_LOGD(TAG, "Получен пакет: %s", pretty_cmd1.c_str());
    return false;
  }

  if (data[length] != packet_size ) {
    ESP_LOGW(TAG, "Size mismatch: %02X!=%02X", data[length], packet_size);
    std::string pretty_cmd1 = format_hex_pretty(rx_message_);
    ESP_LOGD(TAG, "Получен пакет: %s", pretty_cmd1.c_str());
    return false;
  }

  // Удаляем ведущий 0x00
  rx_message_.erase(rx_message_.begin());

  std::string pretty_cmd = format_hex_pretty(rx_message_);
  ESP_LOGI(TAG, "Получен пакет: %s", pretty_cmd.c_str());

  parse_status_packet(rx_message_);

  return false;
}


// разбираем полученные пакеты
void NiceBusT4::parse_status_packet (const std::vector<uint8_t> &data) {
  // Минимальный пакет после удаления 0x00 — 15 байт (pct_size=0x0c, length=15).
  // Все обращения к байтам выше 13-го защищены проверками размера.
  const size_t sz = data.size();

  if (sz < 14) {
    ESP_LOGW(TAG, "Слишком короткий пакет (%d байт), пропускаем", (int)sz);
    return;
  }

  if ((data[1] == 0x0d) && (data[13] == 0xFD)) {
    ESP_LOGE(TAG, "Команда недоступна для этого устройства");
  }

  if (((data[11] == 0x18) || (data[11] == 0x19)) && (data[13] == NOERR)) { // EVT с данными
    ESP_LOGD(TAG, "Получен пакет EVT. Следующий блок: %d", data[12]);

    if (sz >= 16) {
      std::vector<uint8_t> vec_data(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
      std::string str(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
      ESP_LOGI(TAG, "Строка с данными: %s", str.c_str());
      std::string pretty_data = format_hex_pretty(vec_data);
      ESP_LOGI(TAG, "Данные HEX %s", pretty_data.c_str());
    }

    if ((data[6] == INF) && (data[9] == FOR_CU)  && (data[11] == GET - 0x80) && (data[13] == NOERR)) {
      ESP_LOGI(TAG, "Получен ответ на запрос 0x%02X", data[10]);
      switch (data[10]) {
        case TYPE_M:
          if (sz < 15) break;
          switch (data[14]) {
            case SLIDING:   this->class_gate_ = SLIDING;   break;
            case SECTIONAL: this->class_gate_ = SECTIONAL; break;
            case SWING:     this->class_gate_ = SWING;     break;
            case BARRIER:   this->class_gate_ = BARRIER;   break;
            case UPANDOVER: this->class_gate_ = UPANDOVER; break;
          }
          break;

        case INF_IO:
          if (sz < 17) break;
          switch (data[16]) {
            case 0x00:
              ESP_LOGI(TAG, "Концевик не сработал");
              break;
            case 0x01:
              ESP_LOGI(TAG, "Концевик на закрытие");
              this->position = COVER_CLOSED;
              break;
            case 0x02:
              ESP_LOGI(TAG, "Концевик на открытие");
              this->position = COVER_OPEN;
              break;
          }
          this->publish_state();
          break;

        case MAX_OPN:
          if (sz < 16) break;
          if (is_walky) {
            this->_max_opn = data[15];
            this->_pos_opn = data[15];
          }
          else {  
            this->_max_opn = (data[14] << 8) + data[15];
          }
          ESP_LOGI(TAG, "Максимальное положение энкодера: %d", this->_max_opn);
          break;

        case POS_MIN:
          if (sz < 16) break;
          this->_pos_cls = (data[14] << 8) + data[15];
          ESP_LOGI(TAG, "Положение закрытых ворот: %d", this->_pos_cls);
          break;

        case POS_MAX:
          if (sz < 16) break;
          if (((data[14] << 8) + data[15]) > 0x00) {
            this->_pos_opn = (data[14] << 8) + data[15];
          }
          ESP_LOGI(TAG, "Положение открытых ворот: %d", this->_pos_opn);
          break;

        case CUR_POS:
          if (sz < 16) break;
          if (is_walky) {
            this->_pos_usl = data[15];
          }
          else {
            this->_pos_usl = (data[14] << 8) + data[15];
          }
          this->position = this->safe_position(this->_pos_usl);
          ESP_LOGI(TAG, "Положение ворот: %d (%d%%)", this->_pos_usl, (int)(this->position * 100));
          this->publish_state();
          break;

        case 0x01:
          if (sz < 15) break;
          switch (data[14]) {
            case OPENED:
              ESP_LOGI(TAG, "Ворота открыты");
              this->position = COVER_OPEN;
              this->current_operation = COVER_OPERATION_IDLE;
              break;
            case CLOSED:
              ESP_LOGI(TAG, "Ворота закрыты");
              this->position = COVER_CLOSED;
              this->current_operation = COVER_OPERATION_IDLE;
              break;
            case 0x01:
              ESP_LOGI(TAG, "Ворота остановлены");
              this->current_operation = COVER_OPERATION_IDLE;
              break;
            case 0x00:
              ESP_LOGI(TAG, "Статус ворот неизвестен");
              this->current_operation = COVER_OPERATION_IDLE;
              break;
            case 0x0b:
              ESP_LOGI(TAG, "Поиск положений выполнен");
              this->current_operation = COVER_OPERATION_IDLE;
              break;
            case STA_OPENING:
              ESP_LOGI(TAG, "Идёт открывание");
              this->current_operation = COVER_OPERATION_OPENING;
              break;
            case STA_CLOSING:
              ESP_LOGI(TAG, "Идёт закрывание");
              this->current_operation = COVER_OPERATION_CLOSING;
              break;
          }
          this->publish_state();
          break;

        case AUTOCLS:
          if (sz < 15) break;
          this->autocls_flag = data[14];
          break;
          
        case PH_CLS_ON:
          if (sz < 15) break;
          this->photocls_flag = data[14];
          break;  
          
        case ALW_CLS_ON:
          if (sz < 15) break;
          this->alwayscls_flag = data[14];
          break;  
          
      } // switch
    } // if EVT GET от привода
    
    if ((data[6] == INF) && (data[9] == FOR_CU)  && (data[11] == SET - 0x80) && (data[13] == NOERR)) {
      switch (data[10]) {
        case AUTOCLS:
          tx_buffer_.push(gen_inf_cmd(FOR_CU, AUTOCLS, GET));
          break;
        case PH_CLS_ON:
          tx_buffer_.push(gen_inf_cmd(FOR_CU, PH_CLS_ON, GET));
          break;  
        case ALW_CLS_ON:
          tx_buffer_.push(gen_inf_cmd(FOR_CU, ALW_CLS_ON, GET));
          break;  
      }
    }

    if ((data[6] == INF) && (data[9] == FOR_ALL)  && ((data[11] == GET - 0x80) || (data[11] == GET - 0x81)) && (data[13] == NOERR)) {

      switch (data[10]) {
        case MAN:
          this->manufacturer_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          break;
        case PRD:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) {
            this->oxi_product.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) {
            this->product_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
            std::vector<uint8_t> wla1 = {0x57,0x4C,0x41,0x31,0x00,0x06,0x57}; // признак привода Walky
            if (this->product_ == wla1) { 
              this->is_walky = true;
            }
          }
          break;
        case HWR:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) {
            this->oxi_hardware.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) {
            this->hardware_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          break;
        case FRM:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) {
            this->oxi_firmware.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) {
            this->firmware_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          break;
        case DSC:
          if (((uint8_t)(this->oxi_addr >> 8) == data[4]) && ((uint8_t)(this->oxi_addr & 0xFF) == data[5])) {
            this->oxi_description.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          else if (((uint8_t)(this->to_addr >> 8) == data[4]) && ((uint8_t)(this->to_addr & 0xFF) == data[5])) {
            this->description_.assign(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
          }
          break;
        case WHO:
          if (data[12] == 0x01) {
            if (data[14] == 0x04) {
              this->to_addr = ((uint16_t)data[4] << 8) | data[5];
              this->init_ok = true;
            }
            else if (data[14] == 0x0A) {
              this->oxi_addr = ((uint16_t)data[4] << 8) | data[5];
              init_device(data[4], data[5], data[14]);
            }
          }
          break;
      }

    }

    if (sz >= 16 &&
        (data[9] == 0x0A) && (data[10] == 0x25) && (data[11] == 0x01) && (data[12] == 0x0A) && (data[13] == NOERR)) {
      std::vector<uint8_t> vec_data(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
      if (vec_data.size() >= 9) {
        ESP_LOGCONFIG(TAG, "Номер пульта: %X%X%X%X, команда: %X, кнопка: %X, режим: %X, нажатий: %d",
          vec_data[5], vec_data[4], vec_data[3], vec_data[2],
          vec_data[8] / 0x10, vec_data[5] / 0x10, vec_data[7] + 0x01, vec_data[6]);
      }
    }

    if (sz >= 16 &&
        (data[9] == 0x0A) && (data[10] == 0x26) && (data[11] == 0x41) && (data[12] == 0x08) && (data[13] == NOERR)) {
      std::vector<uint8_t> vec_data(this->rx_message_.begin() + 14, this->rx_message_.end() - 2);
      if (vec_data.size() >= 4) {
        ESP_LOGCONFIG(TAG, "Кнопка %X, номер пульта: %X%X%X%X",
          vec_data[0] / 0x10, vec_data[0] % 0x10, vec_data[1], vec_data[2], vec_data[3]);
      }
    }

  } // if evt


  else if (data[1] > 0x0d) { // пакет RSP — подтверждение команды / статус во время движения
    ESP_LOGD(TAG, "Получен пакет RSP");
    if (sz >= 14) {
      std::vector<uint8_t> vec_data(this->rx_message_.begin() + 12, this->rx_message_.end() - 3);
      std::string str(this->rx_message_.begin() + 12, this->rx_message_.end() - 3);
      ESP_LOGI(TAG, "Строка RSP: %s", str.c_str());
      std::string pretty_data = format_hex_pretty(vec_data);
      ESP_LOGI(TAG, "Данные HEX %s", pretty_data.c_str());
    }
    switch (data[9]) {
      case FOR_CU:
        ESP_LOGI(TAG, "Пакет контроллера привода");
        switch (data[10] + 0x80) {
          case RUN:
            ESP_LOGI(TAG, "Подменю RUN");
            switch (data[11] - 0x80) {
              case SBS:
                ESP_LOGI(TAG, "Команда: Пошагово");
                break;
              case STOP:
                ESP_LOGI(TAG, "Команда: STOP");
                break;
              case OPEN:
                ESP_LOGI(TAG, "Команда: OPEN");
                this->current_operation = COVER_OPERATION_OPENING;
                break;
              case CLOSE:
                ESP_LOGI(TAG, "Команда: CLOSE");
                this->current_operation = COVER_OPERATION_CLOSING;                
                break;
              case P_OPN1:
                ESP_LOGI(TAG, "Команда: Частичное открывание");
                break;
              case STOPPED:
                this->current_operation = COVER_OPERATION_IDLE;
                ESP_LOGI(TAG, "Команда: Остановлено");
                break;
              case ENDTIME:
                ESP_LOGI(TAG, "Операция завершена по таймауту");
                break;
            }
            
            switch (data[11]) {
              case STA_OPENING:
                ESP_LOGI(TAG, "Операция: Открывается");
                this->current_operation = COVER_OPERATION_OPENING;
                break;
              case STA_CLOSING:
                ESP_LOGI(TAG, "Операция: Закрывается");
                this->current_operation = COVER_OPERATION_CLOSING;                
                break;
              case CLOSED:
                ESP_LOGI(TAG, "Операция: Закрыто");
                this->position = COVER_CLOSED;
                this->current_operation = COVER_OPERATION_IDLE;
                break;
              case OPENED:
                this->position = COVER_OPEN;
                ESP_LOGI(TAG, "Операция: Открыто");
                this->current_operation = COVER_OPERATION_IDLE;
                break;
              case STOPPED:
                this->current_operation = COVER_OPERATION_IDLE;
                ESP_LOGI(TAG, "Операция: Остановлено");
                break;
              default:
                ESP_LOGI(TAG, "Операция: 0x%02X", data[11]);
            }
            this->publish_state();
            break; //RUN

          case STA:
            ESP_LOGI(TAG, "Подменю Статус в движении");
            switch (data[11]) {
              case STA_OPENING:
                ESP_LOGI(TAG, "Движение: Открывается");
                this->current_operation = COVER_OPERATION_OPENING;
                break;
              case STA_CLOSING:
                ESP_LOGI(TAG, "Движение: Закрывается");
                this->current_operation = COVER_OPERATION_CLOSING;
                break;
              case CLOSED:
                ESP_LOGI(TAG, "Движение: Закрыто");
                this->position = COVER_CLOSED;
                this->current_operation = COVER_OPERATION_IDLE;
                break;
              case OPENED:
                this->position = COVER_OPEN;
                ESP_LOGI(TAG, "Движение: Открыто");
                this->current_operation = COVER_OPERATION_IDLE;
                break;
              case STOPPED:
                this->current_operation = COVER_OPERATION_IDLE;
                ESP_LOGI(TAG, "Движение: Остановлено");
                break;
              default:
                ESP_LOGI(TAG, "Движение: 0x%02X", data[11]);
            }

            if (sz >= 14) {
              this->_pos_usl = (data[12] << 8) + data[13];
              this->position = this->safe_position(this->_pos_usl);
              ESP_LOGD(TAG, "Положение ворот: %d (%d%%)", this->_pos_usl, (int)(this->position * 100));
            }
            this->publish_state();
            break; //STA

          default:
            ESP_LOGI(TAG, "Подменю 0x%02X", data[10]);
        }

        break;
      case CONTROL:
        ESP_LOGI(TAG, "Пакет CONTROL");
        break;
      case FOR_ALL:
        ESP_LOGI(TAG, "Пакет для всех");
        break;
      case 0x0A:
        ESP_LOGI(TAG, "Пакет приёмника");
        break;
      default:
        ESP_LOGI(TAG, "Меню 0x%02X", data[9]);
    }

  } // else RSP

} // parse_status_packet


void NiceBusT4::dump_config() {
  ESP_LOGCONFIG(TAG, "  Bus T4 Cover");
  switch (this->class_gate_) {
    case SLIDING:
      ESP_LOGCONFIG(TAG, "  Тип: Откатные ворота");
      break;
    case SECTIONAL:
      ESP_LOGCONFIG(TAG, "  Тип: Секционные ворота");
      break;
    case SWING:
      ESP_LOGCONFIG(TAG, "  Тип: Распашные ворота");
      break;
    case BARRIER:
      ESP_LOGCONFIG(TAG, "  Тип: Шлагбаум");
      break;
    case UPANDOVER:
      ESP_LOGCONFIG(TAG, "  Тип: Подъёмно-поворотные ворота");
      break;
    default:
      ESP_LOGCONFIG(TAG, "  Тип: Неизвестный (0x%02X)", this->class_gate_);
  }

  ESP_LOGCONFIG(TAG, "  Максимальное положение: %d", this->_max_opn);
  ESP_LOGCONFIG(TAG, "  Положение открытых ворот: %d", this->_pos_opn);
  ESP_LOGCONFIG(TAG, "  Положение закрытых ворот: %d", this->_pos_cls);

  std::string manuf_str(this->manufacturer_.begin(), this->manufacturer_.end());
  ESP_LOGCONFIG(TAG, "  Производитель: %s", manuf_str.c_str());

  std::string prod_str(this->product_.begin(), this->product_.end());
  ESP_LOGCONFIG(TAG, "  Привод: %s", prod_str.c_str());

  std::string hard_str(this->hardware_.begin(), this->hardware_.end());
  ESP_LOGCONFIG(TAG, "  Железо привода: %s", hard_str.c_str());

  std::string firm_str(this->firmware_.begin(), this->firmware_.end());
  ESP_LOGCONFIG(TAG, "  Прошивка привода: %s", firm_str.c_str());
  
  std::string dsc_str(this->description_.begin(), this->description_.end());
  ESP_LOGCONFIG(TAG, "  Описание привода: %s", dsc_str.c_str());

  ESP_LOGCONFIG(TAG, "  Адрес шлюза: 0x%04X", from_addr);
  ESP_LOGCONFIG(TAG, "  Адрес привода: 0x%04X", to_addr);
  ESP_LOGCONFIG(TAG, "  Адрес приёмника: 0x%04X", oxi_addr);
  
  std::string oxi_prod_str(this->oxi_product.begin(), this->oxi_product.end());
  ESP_LOGCONFIG(TAG, "  Приёмник: %s", oxi_prod_str.c_str());
  
  std::string oxi_hard_str(this->oxi_hardware.begin(), this->oxi_hardware.end());
  ESP_LOGCONFIG(TAG, "  Железо приёмника: %s", oxi_hard_str.c_str());

  std::string oxi_firm_str(this->oxi_firmware.begin(), this->oxi_firmware.end());
  ESP_LOGCONFIG(TAG, "  Прошивка приёмника: %s", oxi_firm_str.c_str());
  
  std::string oxi_dsc_str(this->oxi_description.begin(), this->oxi_description.end());
  ESP_LOGCONFIG(TAG, "  Описание приёмника: %s", oxi_dsc_str.c_str());
 
  ESP_LOGCONFIG(TAG, "  Автозакрытие - L1: %s", autocls_flag ? "Да" : "Нет");
  ESP_LOGCONFIG(TAG, "  Закрыть после фото - L2: %s", photocls_flag ? "Да" : "Нет");
  ESP_LOGCONFIG(TAG, "  Всегда закрывать - L3: %s", alwayscls_flag ? "Да" : "Нет");
  ESP_LOGCONFIG(TAG, "  UART №%d, RX=%d, TX=%d", this->uart_num_, this->rx_pin, this->tx_pin);
}


// формирование команды управления
std::vector<uint8_t> NiceBusT4::gen_control_cmd(const uint8_t control_cmd) {
  std::vector<uint8_t> frame = {(uint8_t)(this->to_addr >> 8), (uint8_t)(this->to_addr & 0xFF), (uint8_t)(this->from_addr >> 8), (uint8_t)(this->from_addr & 0xFF)};
  frame.push_back(CMD);  // 0x01
  frame.push_back(0x05);
  uint8_t crc1 = (frame[0] ^ frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5]);
  frame.push_back(crc1);
  frame.push_back(CONTROL);
  frame.push_back(RUN);
  frame.push_back(control_cmd);
  frame.push_back(0x64); // OFFSET — D-PRO924 не реагирует без этого байта
  uint8_t crc2 = (frame[7] ^ frame[8] ^ frame[9] ^ frame[10]);
  frame.push_back(crc2);
  uint8_t f_size = frame.size();
  frame.push_back(f_size);
  frame.insert(frame.begin(), f_size);
  frame.insert(frame.begin(), START_CODE);
  return frame;
}

// формирование команды INF с данными и без
std::vector<uint8_t> NiceBusT4::gen_inf_cmd(const uint8_t to_addr1, const uint8_t to_addr2, const uint8_t whose, const uint8_t inf_cmd, const uint8_t run_cmd, const uint8_t next_data, const std::vector<uint8_t> &data, size_t len) {
  std::vector<uint8_t> frame = {to_addr1, to_addr2, (uint8_t)(this->from_addr >> 8), (uint8_t)(this->from_addr & 0xFF)};
  frame.push_back(INF);  // 0x08 mes_type
  frame.push_back(0x06 + len); // mes_size
  uint8_t crc1 = (frame[0] ^ frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5]);
  frame.push_back(crc1);
  frame.push_back(whose);
  frame.push_back(inf_cmd);
  frame.push_back(run_cmd);
  frame.push_back(next_data);
  frame.push_back(len);
  if (len > 0) {
    frame.insert(frame.end(), data.begin(), data.end());
  }
  uint8_t crc2 = frame[7];
  for (size_t i = 8; i < 12 + len; i++) {
    crc2 = crc2 ^ frame[i];
  }
  frame.push_back(crc2);
  uint8_t f_size = frame.size();
  frame.push_back(f_size);
  frame.insert(frame.begin(), f_size);
  frame.insert(frame.begin(), START_CODE);
  return frame;
}


void NiceBusT4::send_raw_cmd(std::string data) {
  std::vector<uint8_t> v_cmd = raw_cmd_prepare(data);
  send_array_cmd(&v_cmd[0], v_cmd.size());
}


std::vector<uint8_t> NiceBusT4::raw_cmd_prepare(std::string data) {
  data.erase(remove_if(data.begin(), data.end(), [](const unsigned char ch) {
    return (!(iswalnum(ch)));
  }), data.end());

  std::vector<uint8_t> frame;
  for (uint8_t i = 0; i < data.size(); i += 2) {
    std::string sub_str(data, i, 2);
    char hexstoi = (char)std::strtol(&sub_str[0], 0, 16);
    frame.push_back(hexstoi);
  }
  return frame;
}


void NiceBusT4::send_array_cmd(std::vector<uint8_t> data) {
  return send_array_cmd((const uint8_t *)data.data(), data.size());
}

void NiceBusT4::send_array_cmd(const uint8_t *data, size_t len) {
  Serial1.flush();

#ifdef USE_ESP32
  // На ESP32 (C3 / S3 / C6 / P4 и других): аппаратное инвертирование TX для чистого break.
  // Это надёжнее смены baud-rate и не зависит от тактовой частоты APB.
  uart_set_line_inverse((uart_port_t)this->uart_num_, UART_SIGNAL_TXD_INV);
  delayMicroseconds(520);  // ~10 бит при 19200 = 520 мкс
  uart_set_line_inverse((uart_port_t)this->uart_num_, UART_SIGNAL_INV_DISABLE);
  delayMicroseconds(8);    // короткая пауза перед стартовым битом первого байта
#else
  // ESP8266: смена скорости для генерации длинного нулевого импульса
  char br_ch = 0x00;
  Serial1.updateBaudRate(BAUD_BREAK);
  Serial1.write(&br_ch, 1);
  delayMicroseconds(90);
  Serial1.updateBaudRate(BAUD_WORK);
#endif

  Serial1.write(data, len);

  std::string pretty_cmd = format_hex_pretty((uint8_t *)&data[0], len);
  ESP_LOGI(TAG, "Отправлено: %s", pretty_cmd.c_str());
}


// генерация и отправка inf команд из yaml конфигурации
void NiceBusT4::send_inf_cmd(std::string to_addr, std::string whose, std::string command, std::string type_command, std::string next_data, bool data_on, std::string data_command) {
  std::vector<uint8_t> v_to_addr      = raw_cmd_prepare(to_addr);
  std::vector<uint8_t> v_whose        = raw_cmd_prepare(whose);
  std::vector<uint8_t> v_command      = raw_cmd_prepare(command);
  std::vector<uint8_t> v_type_command = raw_cmd_prepare(type_command);
  std::vector<uint8_t> v_next_data    = raw_cmd_prepare(next_data);
  std::vector<uint8_t> v_data_command = raw_cmd_prepare(data_command);

  if (data_on) {
    tx_buffer_.push(gen_inf_cmd(v_to_addr[0], v_to_addr[1], v_whose[0], v_command[0], v_type_command[0], v_next_data[0], v_data_command, v_data_command.size()));
  } else {
    tx_buffer_.push(gen_inf_cmd(v_to_addr[0], v_to_addr[1], v_whose[0], v_command[0], v_type_command[0], v_next_data[0]));
  }
}

// генерация команд для контроллера привода с минимальными параметрами
void NiceBusT4::set_mcu(std::string command, std::string data_command) {
  std::vector<uint8_t> v_command      = raw_cmd_prepare(command);
  std::vector<uint8_t> v_data_command = raw_cmd_prepare(data_command);
  tx_buffer_.push(gen_inf_cmd(0x04, v_command[0], 0xa9, 0x00, v_data_command));
}
  
// инициализация устройства
void NiceBusT4::init_device(const uint8_t addr1, const uint8_t addr2, const uint8_t device) {
  if (device == FOR_CU) {
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, TYPE_M, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, MAN, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, FRM, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, PRD, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, HWR, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, POS_MAX, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, POS_MIN, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, DSC, GET, 0x00));
    if (is_walky) {
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, MAX_OPN, GET, 0x00, {0x01}, 1));
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, CUR_POS, GET, 0x00, {0x01}, 1));
    }
    else { 
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, MAX_OPN, GET, 0x00));
      tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, CUR_POS, GET, 0x00));
    }  
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, INF_STATUS, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, AUTOCLS, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, PH_CLS_ON, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, device, ALW_CLS_ON, GET, 0x00));
  }
  if (device == FOR_OXI) {
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, PRD, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, HWR, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, FRM, GET, 0x00));
    tx_buffer_.push(gen_inf_cmd(addr1, addr2, FOR_ALL, DSC, GET, 0x00));
  }
}


}  // namespace bus_t4
}  // namespace esphome
