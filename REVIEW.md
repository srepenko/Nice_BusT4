# Обзор проекта Nice Bus T4 — предложения по оптимизации, доработкам и поддержке новых ESP32

> Документ-ревизия по результатам прочтения исходников
> (`components/bus_t4/nice-bust4.{h,cpp}`, `cover.py`, `automation.h`,
> `nice-wifi.yaml`, `uart_break.ino`).
> Цель — собрать в одном месте всё, что просится в рефакторинг, и описать,
> как переехать на ESP32-S3 / C6 / P4.

---

## 1. Краткая характеристика текущего состояния

Проект — внешний компонент ESPHome `bus_t4`, реализующий мост между шиной
Nice Bus T4 (UART 19200 8N1 + длинный break ≈ 520 µs перед каждым пакетом)
и Home Assistant. Сейчас он:

- жёстко завязан на Arduino-framework (`HardwareSerial`, `Serial1`,
  `Serial1.updateBaudRate()`),
- собирается под ESP32-C3 (board `esp32-c3-devkitm-1`),
- использует свой собственный приём/разбор пакетов и собственную
  очередь `std::queue<std::vector<uint8_t>>`,
- не использует встроенный ESPHome-компонент `uart` (он закомментирован),
- не выставляет в HA настройки привода в виде Number/Switch — кроме
  кнопок и трёх флагов автозакрытия.

Это работает, но имеет ряд узких мест, описанных ниже.

---

## 2. Замечания и предложения по коду

### 2.1. Архитектура / переносимость

| # | Сейчас | Предложение |
|---|--------|-------------|
| A1 | Используется `Serial1` напрямую, framework только `arduino`. | Перевести на ESPHome-обёртку `uart::UARTComponent` или, ещё лучше, на ESP-IDF `driver/uart.h` с честным `uart_set_line_inverse()`/`uart_write_bytes_with_break()`. Это сразу даст: поддержку `esp-idf` framework, корректный break без `delayMicroseconds()`, единый стиль логирования и автоматическую работу на любых ESP32-вариантах. |
| A2 | Только `framework: arduino`. | Добавить ветку для `esp-idf`. ESP32-C6/P4 в ESPHome поддерживаются почти исключительно через `esp-idf`. |
| A3 | `automation.h` содержит пустой `RawCmdAction` — мёртвый код. | Либо реализовать как полноценный `Action<>` (вызов `send_raw_cmd()`/`send_inf_cmd()` из автоматизаций без `lambda:`), либо удалить. |
| A4 | `cover.py` использует устаревший `yield`. | Переписать через `await` (новый стиль ESPHome >= 1.20) и зарегистрировать новые `cv.Optional`-поля (`from_address`, `oxi_address`, `update_interval`, `class_gate`). |
| A5 | Адреса `from_addr = 0x0066`, `update_interval = 500 ms`, тайм-аут tx 100 ms — захардкожены. | Вынести в YAML через сеттеры. |
| A6 | Нет ни `service: cancel_command`, ни `homeassistant_service` без lambda. | Добавить native `Action`-ы и убрать «магию» из YAML. |

### 2.2. Работа с UART и break

```816:921:components/bus_t4/nice-bust4.cpp
void NiceBusT4::send_array_cmd (const uint8_t *data, size_t len) {
  char br_ch = 0x00;
  Serial1.flush();
  Serial1.updateBaudRate(BAUD_BREAK);
  Serial1.write(&br_ch, 1);
  delayMicroseconds(90);
  Serial1.updateBaudRate(BAUD_WORK);
  Serial1.write(data, len);
  ...
}
```

Замечания:

- **Не блокирующий `write()` + смена baudrate без `flush()` перед ней** —
  `updateBaudRate(BAUD_WORK)` может произойти до того, как
  байт break реально вышел из shift-регистра. На C3 ещё работает,
  на S3/C6 при задержке планировщика будет «джиттер».
  Минимально: добавить `Serial1.flush()` после `write(&br_ch, 1)` —
  он ждёт опустошения tx-FIFO именно через `uart_wait_tx_done()`.
- **`delayMicroseconds(90)` подобран эмпирически на D1 mini** —
  на других кристаллах смысла не имеет.
- **Готовое решение в Arduino-ESP32**: `HardwareSerial::sendBreak(<bits>)`
  (если доступно в текущей версии core) или ESP-IDF
  `uart_write_bytes_with_break(uart, data, len, break_bits)`.
  Это аппаратный break, без переключения скорости и без задержек.
- В `uart_break.ino` уже есть рабочий вариант через `uart_set_baudrate()`,
  но для ESP32 (а не ESP8266) лучше окончательно переехать на API IDF.

Предложение: вынести `send_break()` в `bus_t4_uart.{h,cpp}` с двумя
реализациями (Arduino-fallback и ESP-IDF) под `#ifdef USE_ESP_IDF`.

### 2.3. Парсер пакетов

```125:225:components/bus_t4/nice-bust4.cpp
void NiceBusT4::handle_char_(uint8_t c) {
  this->rx_message_.push_back(c);
  if (!this->validate_message_()) {
    this->rx_message_.clear();
  }
}
bool NiceBusT4::validate_message_() {
  ...
}
```

Проблемы:

1. `rx_message_` — `std::vector<uint8_t>` без верхней границы. На «мусорной»
   шине (некорректный 0x00 в начале → не сматчился) буфер будет очищен,
   но при подряд идущих байтах с правильным `0x00 0x55` и «битым»
   `length` мы будем читать `data[length]` за пределами и сравнивать
   с `crc2`. Нужно проверять `length` (например, в диапазоне 0x0c..0x40)
   и общий размер `rx_message_` (например, не более 64 байт),
   иначе ловим UB при обращении `data[at]` после `push_back`.
2. Нет тайм-аута неполного фрейма. Если последний байт пакета потерялся,
   буфер «зависнет» и склеится со следующим пакетом. Достаточно обнулять
   `rx_message_`, если `millis() - last_uart_byte_ > 5` (за 5 мс при
   19200 бод спокойно влезает целый пакет).
3. Дубль формата `format_hex_pretty(...)` четыре раза при ошибке — можно
   просто `if (logger->log_at_least(DEBUG))`.
4. `%S` (с большой буквы) в `printf`-строках — это `wchar_t*` по
   стандарту C. ESPHome это толерирует, но строго это UB.
   Везде заменить на `%s`.
5. `parse_status_packet` по 700 строк с глубокой вложенностью `switch`.
   Логику стоит разбить по типам пакетов (`parse_evt_*`, `parse_rsp_*`),
   а ещё лучше — таблицей-диспетчером `{cmd, handler}`.
6. Не учитывается `CRC1` для пакетов, у которых `at == 9`,
   если `at < 9` сообщение «пропускается». Надо в `validate_message_()`
   защититься от случая, когда `packet_size < 6` (тогда `length < 9`).

### 2.4. Очередь отправки

```469:470:components/bus_t4/nice-bust4.h
std::vector<uint8_t> rx_message_;
std::queue<std::vector<uint8_t>> tx_buffer_;
```

- `std::queue<std::vector<uint8_t>>` => куча выделений на каждый кадр
  (≤ 18 байт). Замените на:
  ```cpp
  std::deque<std::array<uint8_t, MAX_FRAME_SIZE>> tx_buffer_;
  ```
  или собственный circular buffer на стат. массиве —
  на ESP32-C3 с 320 КБ это не критично, но устранит аллокации
  в `loop()`-е.
- Нет ограничения размера очереди — при потере связи с приводом
  команды от пользователя могут «накопиться сотнями». Стоит
  ограничить (`tx_buffer_.size() < 32`) и логировать `WARN` при сбросе.
- `ready_to_tx_` сбрасывается на 100 мс после любого принятого байта.
  Корректно, но лучше явно ждать «тишины на линии», измеряя
  межсимвольный интервал (для нагруженной шины с OVIEW + OXI
  100 мс — это очень много, и команды от HA будут «лагать»).

### 2.5. Логика инициализации привода

```80:122:components/bus_t4/nice-bust4.cpp
void NiceBusT4::loop() {
    if ((millis() - this->last_update_) > 5000) {    // каждые 10 секунд
        ...
    }
```

- Комментарий «каждые 10 секунд», а в коде 5000 мс. Привести в порядок.
- Каждый цикл инициализации добавляет ~14 пакетов в очередь, даже если
  ответы уже получены. Нужно отдельные флаги `have_product_`,
  `have_firmware_`, `have_max_opn_` и опрашивать только то, чего нет.
- Ввести явный конечный автомат `enum class InitState { WAIT_BUS,
  DISCOVER, QUERY_CU, QUERY_OXI, RUNNING }` — резко улучшит читаемость
  и сделает `dump_config()` информативным.

### 2.6. Распознавание привода Walky

```411:415:components/bus_t4/nice-bust4.cpp
std::vector<uint8_t> wla1 = {0x57,0x4C,0x41,0x31,0x00,0x06,0x57};
if (this->product_ == wla1) {
  this->is_walky = true;
}
```

- Сравнение по точному значению, включая «хвост» — хрупко при
  малейшем изменении прошивки. Достаточно сравнить первые 4 байта
  (`"WLA1"`).
- Лучше — проверять `manufacturer_` и/или `class_gate_`, а не
  магическую строку.

### 2.7. Эргономика конфигурации (cover.py)

```12:19:components/bus_t4/cover.py
CONFIG_SCHEMA = cover.COVER_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(Nice),
    cv.Optional(CONF_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_USE_ADDRESS): cv.hex_uint16_t,
    cv.Optional(CONF_RX_PIN): cv.uint8_t,
    cv.Optional(CONF_TX_PIN): cv.uint8_t,
}).extend(cv.COMPONENT_SCHEMA)
```

Что напрашивается добавить:

- `cv.Optional(CONF_OXI_ADDRESS)` — сейчас задаётся жёстко из кода.
- `cv.Optional("class_gate")` со значениями `sliding/sectional/swing/barrier/up_and_over`
  — чтобы не ждать 5 с автоопределения каждый старт.
- `cv.Optional(CONF_UPDATE_INTERVAL, default="500ms")`.
- Использовать `pins.gpio_input_pin_schema` и `pins.internal_gpio_output_pin_schema`,
  чтобы корректно задавать пин с pull-up, инверсией и т. д.,
  а не «голым» `uint8_t`.
- Перейти на `await` (`async def to_code`) вместо `yield`.

### 2.8. Что просится наружу как сущности HA

Сейчас параметры привода читаются в лог. Их можно превратить в
полноценные сущности и не дублировать через `template`:

- `binary_sensor`: «Автозакрытие L1», «Закрыть после фото L2»,
  «Всегда закрывать L3»;
- `number`: «Усилие открытия» (`OPN_PWR`), «Усилие закрытия» (`CLS_PWR`),
  «Скорость открытия/закрытия» (`SPEED_*`), «Время паузы» (`P_TIME`);
- `sensor`: «Текущая позиция, %», «Счётчик манёвров» (`P_COUNT`),
  «Порог обслуживания» (`T_VAL`);
- `text_sensor`: «Производитель», «Модель», «HW», «FW», «Описание».

Это превратит проект из «голого мостика» в полноценную интеграцию,
которую можно выводить на дашборд без ручной подгонки.

### 2.9. Безопасность и стабильность

- Команды `OPEN/CLOSE` отправляются без подтверждения от пользователя.
  Полезно добавить опциональный `confirmation_timeout: 0s`.
- При длительных проблемах с шиной перезагружать `Serial1.begin(...)`.
- Реализовать `watchdog`: если `init_ok == false` дольше N минут —
  публиковать `binary_sensor.problem`.
- Добавить `restart` сервис.

### 2.10. Минорные замечания

- В заголовке `nice-bust4.h` `#include "esphome.h"` — этот «жирный»
  заголовок ломает сборку на чистом ESP-IDF. Заменить на точечные
  `esphome/components/cover/cover.h`, `esphome/core/component.h`,
  `esphome/core/log.h`.
- `cover.py` импортирует `CONF_UPDATE_INTERVAL`, но не использует.
- В YAML примере опечатка «Переспектива» / «hassystant» в README.
- В `dump_config()` многократно конструируются `std::string` из
  `std::vector<uint8_t>` — не критично, но в `dump_config()` это
  однократно, поэтому терпимо.
- Закомментированных огромных блоков `parse_status_packet`
  (строки 605–724) — удалить из main, оставить в git-истории.

---

## 3. Сводка приоритетов

| Приоритет | Что | Зачем |
|-----------|-----|-------|
| ★★★ | Перевести UART на ESPHome `uart` или ESP-IDF, корректный hardware-break | Перенос на S3/C6/P4, стабильность |
| ★★★ | Ограничить размер `rx_message_`, тайм-аут неполного кадра | Защита от UB на «мусорной» шине |
| ★★ | Конечный автомат инициализации, опрос только недостающего | Меньше трафика, быстрее старт |
| ★★ | Number/Switch/Sensor для параметров привода | UX в HA |
| ★★ | Корректная схема в `cover.py` (`async def`, pin-схема, oxi_address, class_gate) | Современный ESPHome |
| ★ | Заменить `%S` → `%s`, починить комментарии «5/10 сек» | Гигиена |
| ★ | Удалить мёртвый `automation.h`, закомментированные блоки | Читаемость |
| ★ | Перейти на `std::deque<std::array>` для tx-очереди | Меньше фрагментации кучи |

---

## 4. Варианты под ESP32-S3 / C6 / P4

В корне репозитория добавлены три YAML-конфигурации:

- [`nice-wifi-s3.yaml`](nice-wifi-s3.yaml) — ESP32-S3
- [`nice-wifi-c6.yaml`](nice-wifi-c6.yaml) — ESP32-C6
- [`nice-wifi-p4.yaml`](nice-wifi-p4.yaml) — ESP32-P4 (с памяткой)

Базовый `nice-wifi.yaml` для C3 оставлен без изменений.

### 4.1. ESP32-S3 — самый «безболезненный» переезд

- Совместим с текущим Arduino-кодом (`HardwareSerial Serial1` доступен).
- 2 ядра, USB-CDC/JTAG встроенно, есть PSRAM на DevKit-C-1.
- Хорошие пины: `TX=17`, `RX=18` — дальше от USB (GPIO19/20)
  и от JTAG-пинов (GPIO39–42). На WROOM-1 модуле без PSRAM эти
  пины свободны.
- На S3-USB DevKit нельзя использовать GPIO19/20 для UART, если хочется
  оставить USB-CDC для логов. У многих чужих гайдов фигурирует
  `tx_pin: 43`, `rx_pin: 44`, но это ровно те пины, на которые выведен
  внутренний USB-Serial-JTAG — лучше избегать, особенно на
  DevKit-C-1, где они подтянуты к UART0.
- Для ESPHome рекомендую `framework: esp-idf` — даст
  hardware-break и USB-CDC log out-of-the-box.
- Питание трансивера CAN через 3V3/5V — без изменений.

### 4.2. ESP32-C6 — Wi-Fi 6 + Thread/Zigbee

- В ESPHome поддерживается **только через `esp-idf`**
  (Arduino-core 3.x для C6 экспериментальный, в ESPHome не везде ловится).
  Так как текущий C++-код использует `HardwareSerial`, **сначала
  потребуется минимальный порт на ESPHome `uart` или ESP-IDF**
  (см. п. 2.1). Без этого YAML не соберётся.
- Преимущество: можно интегрировать с Matter/Thread (через
  стандартную `openthread` ESPHome) и закрыть «вечный» вопрос с
  Wi-Fi 2.4 ГГц, перегруженным MQTT-шлюзами.
- Пины UART1: `TX=16`, `RX=17` — доступны и на DevKit-C-1, и на
  большинстве модулей.
- Понадобится явный `board: esp32-c6-devkitc-1`.

### 4.3. ESP32-P4 — большой кристалл без Wi-Fi

> ⚠️ ESP32-P4 **не имеет встроенного Wi-Fi/BT**. Для использования с
> Home Assistant нужен **внешний Wi-Fi-сопроцессор** (обычно ESP32-C6
> или ESP32-C5) через `esp_hosted` / `esp_wifi_remote`. Сейчас в
> ESPHome (на момент 2026-Q1) этот режим не имеет «из коробки»
> готовой обвязки и требует кастомного компонента.

Если коротко:

- P4 здесь избыточен — у нас на шине 19200 бод и пара светодиодов,
  ему попросту нечего делать.
- Два сценария, в которых он оправдан:
  1. **HMI на корпусе** — добавить 5″ MIPI-DSI-экран, управлять
     приводом локально без Wi-Fi (или с C6-co-MCU).
  2. **CAN-мост** — у P4 есть встроенный TWAI/CAN-FD контроллер,
     можно одновременно держать BusT4 и «настоящий» CAN.
- Для ESPHome на P4 поддержка пока в стадии активной интеграции
  (через `esp-idf` 5.3+). Реальная сборка `cover` без `wifi:` потребует
  правок ядра ESPHome, поэтому в `nice-wifi-p4.yaml` приведена
  «целевая» конфигурация и инструкция, что нужно докрутить.

### 4.4. Сводная таблица пинов и фреймворков

| Чип       | Рекомендуемый framework | TX  | RX  | LED   | Замечание |
|-----------|------------------------|-----|-----|-------|-----------|
| ESP32-C3  | arduino (текущий)      | 21  | 20  | 8 inv | как сейчас |
| ESP32-S3  | arduino **или** esp-idf | 17  | 18  | 48    | избегать GPIO19/20/43/44 (USB/JTAG) |
| ESP32-C6  | **esp-idf** (требует порта UART) | 16 | 17 | 8 inv | поддержка Thread/Zigbee/Matter |
| ESP32-P4  | esp-idf, hosted-WiFi (C6 как co-MCU) | 11 | 10 | 35  | нужен внешний Wi-Fi, ниша HMI/CAN-FD |

---

## 5. Что я бы сделал в первую очередь, если задача — «довести до релиза»

1. Перевести UART на ESPHome `uart::UARTComponent` + опциональный
   ESP-IDF backend для break.
2. Добавить тайм-аут rx-буфера и валидацию длины пакета.
3. Конечный автомат инициализации (опрашивать только «недостающее»).
4. Расширить `cover.py`: `async def`, pin-схема, `oxi_address`,
   `class_gate`, `update_interval`.
5. Превратить флаги L1/L2/L3 и параметры скорости/усилия в
   `binary_sensor` / `number`.
6. CI: `esphome compile` для четырёх YAML (C3, S3, C6, P4) — чтобы
   не ломать сборку случайным коммитом.

После этих шагов проект окажется не «proof-of-concept», а
готовой интеграцией, которую не страшно ставить в чужой шкаф автоматики.
