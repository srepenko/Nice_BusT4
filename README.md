Адаптация под ESP32-C3
протестировано с [модулем](https://vi.aliexpress.com/item/1005007468497664.html?src=google&src=google&albch=shopping&acnt=494-037-6276&isdl=y&slnk=&plac=&mtctp=&albbt=Google_7_shopping&aff_platform=google&aff_short_key=UneMJZVf&gclsrc=aw.ds&albagn=888888&ds_e_adid=&ds_e_matchtype=&ds_e_device=c&ds_e_network=x&ds_e_product_group_id=&ds_e_product_id=ru1005007468497664&ds_e_product_merchant_id=5342614068&ds_e_product_country=UA&ds_e_product_language=ru&ds_e_product_channel=online&ds_e_product_store_id=&ds_url_v=2&albcp=19373879459&albag=&isSmbAutoCall=false&needSmbHouyi=false&gad_source=1&gclid=Cj0KCQjwrKu2BhDkARIsAD7GBotvhKkZXJInKAppROHfqopWnafTdUrSGFwoJHjJjfv8QIU3fv7-mWUaAl-1EALw_wcB&gatewayAdapt=glo2vnm) и [CAN bus transceiver](https://vi.aliexpress.com/item/1005007011627342.html?src=google&src=google&albch=shopping&acnt=494-037-6276&isdl=y&slnk=&plac=&mtctp=&albbt=Google_7_shopping&aff_platform=google&aff_short_key=UneMJZVf&gclsrc=aw.ds&albagn=888888&ds_e_adid=&ds_e_matchtype=&ds_e_device=c&ds_e_network=x&ds_e_product_group_id=&ds_e_product_id=ru1005007011627342&ds_e_product_merchant_id=5338634883&ds_e_product_country=UA&ds_e_product_language=ru&ds_e_product_channel=online&ds_e_product_store_id=&ds_url_v=2&albcp=19373879459&albag=&isSmbAutoCall=false&needSmbHouyi=false&gad_source=1&gclid=Cj0KCQjwrKu2BhDkARIsAD7GBosQ8vJxdOih0cb02H9w_iL2c1xznny_tLB22pUO4d1WJG0swLUn1WoaAtDgEALw_wcB&gatewayAdapt=glo2vnm), [DC-DC](https://vi.aliexpress.com/item/32786777084.html?spm=a2g0o.productlist.main.9.536ba13e9ggbLV&algo_pvid=4ae56446-2d59-4840-889e-616a8f32b5d3&algo_exp_id=4ae56446-2d59-4840-889e-616a8f32b5d3-4&pdp_npi=4%40dis%21UAH%2160.96%2153.76%21%21%211.44%211.27%21%40210384b217245903536127711ec7fa%2112000037070460749%21sea%21UA%21177156063%21X&curPageLogUid=1V0Tygla9JCz&utparam-url=scene%3Asearch%7Cquery_from%3A)

Доступен перевод на английский
[English](https://github.com/xdanik/Nice_BusT4)


# ESPHOME компонент для управления приводами Nice по протоколу Bus T4
# Nice Bus T4 protocol

Появилось желание разобраться в протоколе для управления воротами Nice.
Переспектива - дешёвые устройства на базе esp8266 для управлением из умного дома.

Современные блоки управления приводами имеют разъем BusT4, на который выведены GND, +V, Can-Rx, Can-Tx. Величина напряжения V может варьироваться от 24 до 35 вольт для разных блоков управления.

# Настройки привода из Home Assistant

📖 Подробное описание всех настроек: [Русский](docs/SETTINGS.ru.md) · [Українська](docs/SETTINGS.uk.md) · [English](docs/SETTINGS.en.md)
· пример: [examples/nice-walky.yaml](examples/nice-walky.yaml)

Параметры блока управления читаются и записываются по BusT4 как обычные сущности ESPHome.
Значение всегда берётся из привода: после записи параметр перечитывается, плюс периодически
(`params_interval`, по умолчанию 5 мин) — изменения кнопками на самом приводе тоже видны.

```yaml
cover:
  - platform: bus_t4
    id: nice_cover
    name: "Nice Cover"
    params_interval: 5min

switch:          # функции вкл/выкл
  - { platform: bus_t4, type: auto_close,   name: "Auto close" }
  - { platform: bus_t4, type: photo_close,  name: "Close after photo" }
  - { platform: bus_t4, type: always_close, name: "Always close" }
  # standby, preflash, operator_block, key_lock, custom (+ register: 0x..)

number:          # числовые параметры
  - { platform: bus_t4, type: pause_time, name: "Pause time" }
  - { platform: bus_t4, type: force_open, name: "Force open" }
  # photo_close_time, always_close_time, standby_time, preflash_open_time, preflash_close_time,
  # lamp_time, lock_time, suction_cup_time, speed_open, speed_close, speed_slow_open, speed_slow_close,
  # force_close, leaf_delay_open, leaf_delay_close, reverse_time,
  # custom (+ register, bytes, multiplier, min_value, max_value, step)

select:
  - { platform: bus_t4, type: standby_mode, name: "Standby mode" }
  # custom: register: 0x.. + options: {"Label": value, ...}

sensor:          # только чтение
  - { platform: bus_t4, type: maneuvers_count,       name: "Maneuvers count" }
  - { platform: bus_t4, type: maintenance_threshold, name: "Maintenance threshold" }

button:          # калибровки и команды
  - { platform: bus_t4, type: learn_positions,    name: "Learn positions" }
  - { platform: bus_t4, type: learn_bluebus,      name: "Learn BlueBus devices" }
  - { platform: bus_t4, type: cancel_maintenance, name: "Cancel maintenance" }
  - { platform: bus_t4, type: refresh,            name: "Refresh settings" }
  # step_by_step, stop, open, close, partial_open
```

Если блок управления не поддерживает параметр, привод отвечает ошибкой и сущность остаётся без значения.
Проверено на Nice Walky (WLA1): поддерживаются 52 параметра, нет частичных открываний, позиций энкодера,
замедления вкл/выкл, плавного пуска и входов IN2–IN4.

> Имена сущностей в YAML лучше давать латиницей: ESPHome 2026+ строит object_id из ASCII,
> кириллические имена одинаковой длины конфликтуют. Локализованные имена можно задать в Home Assistant.

# Текущие возможности компонента
* Отправка команд: "Открыть", "Стоп", "Закрыть", "Частичное открытие", "Пошагово (SBS)" и других через кнопки.
* Отправка произвольных HEX команд через службу "raw_command". Команда должна быть сформирована заранее или где-то подсмотрена. Разделителями байт могут быть точки или пробелы. Пример: 55 0c 00 03 00 81 01 05 86 01 82 01 64 e6 0c или 55.0D.00.FF.00.66.08.06.97.00.04.99.00.00.9D.0D
* Формирование и отправка произвольных GET/SET запросов через службу "send_inf_command". Позволяет произвести настройку устройства или получить его статусы.
* Отображение в логе пакетов от всех устройств в сети busT4.

# BusT4:

Это измененный UART 19200 8n1 с uart.break длительностью 519us-590us перед каждым пакетом.
Можно подключать несколько устройств, для этого в физический уровень добавлены трансиверы CAN-BUS.
Физическая передача чаще происходит через CAN трансиверы, но CAN-фреймов нет.

# Что сделано:
* Подключил FTDI232 к GND, Can-Rx, Can-Tx. Пакеты видны и поддаются расшифровке.
* Логическим анализатором увидел форму сигнала и состав посылок, подобрал параметры uart.
* Успешно имитировал считанный пакет через Arduino Mega, привод реагирует.
* Получил команды OPEN CLOSE и тд
* Получил байт статуса привода
* Считал основные команды, частично расшифровал значение байт.
* Собрал прототип устройства, протестировал работу.
* Собрал сниффер для отлова пакетов между OVIEW и устройствами busT4
* Написал компонент, который имеет возможность управлять приводами и приёмниками по протоколу BusT4
* Проверил работу на Wingo5000 c блоком [MCA5](img/IMG_20220113_160221.jpg), [Robus RB500HS](img/3hs.jpg), Soona SO2000, [Rd400](img/rba4.jpg), [D-PRO924](img/924.jpg), [Walky WL1024C](img/walky.jpg), [SPIN 22BDKCE](img/spin.jpg).

![alt text](img/Schematic_esphome_bust4_adapter.png "Схема адаптера bus-t4")


ESP8266 не совпадает по уровню сигнала с BUS T4, добавить преобразователь уровней 3.3В -> 5В для Tx на транзисторе.
Rx ESP толерантен к 5В, но для стабильной работы нужен диод. У меня работает со случайным германиевым, возможно и кремниевый подойдёт.

В дальнейшем схема была модифицирована.
![alt text](img/Schematic_busT4adapter_xl.png "Схема адаптера  bus-t4 с модифицированным блоком питания")
![alt text](img/IMG20230306201230.png "Готовое устройство 2.0")

![alt text](img/hassio-bust4.png "Тест работы компонента bus-t4")


Для приводов Walky нужна схема с CAN-трансивером. Разъем Bus-T4 спрятан под заглушкой. Эта же схема подойдет и к другим блокам управления с выведенным rj-11 (6p4c) разъемом, например [mc824h](img/mc824h.jpg) или [RBA3/C](img/rba3c.jpg)
![alt text](img/Schematic_bust4_2023-10-18.png "Схема CAN + bus-t4")

Компонент поддерживает отправку произвольной команды на привод через службу  ESPHome: nice_bust4_uart_raw_command в Home assystant.
```
SBS:   55 0c 00 03 00 81 01 05 86 01 82 01 64 e6 0c
Open:  55 0c 00 03 05 81 01 05 83 01 82 03 64 e4 0c
Close: 55 0c 00 03 05 81 01 05 83 01 82 04 64 e3 0c
Stop:  55 0c 00 03 00 81 01 05 86 01 82 02 64 e5 0c
```


При старте и работе ESP опрашивает подключенные к шине BusT4 устройства и выводит информацию о них в лог. 
![log](img/log.png "Лог")
![log](img/log2.png "Лог2")

# Обновления
* Добавлены службы в интерфейс компонента для более простого запуска процедуры распознавания длины створки и процедуры распознавания устройств BlueBus не разбирая корпус привода (и даже находясь удалённо).
* Добавлен вывод в лог конфигурации считанные из устройства состояния L1, L2, L3 (Автоматическое
закрывание, Закрыть после
фотоэлемента, Всегда закрывать)
* Улучшена совместимость с приводами DPRO924
* Кнопка СТОП всегда доступна в User Interface объекта
* Улучшена совместимость с приводами Walky WL1024C
* Улучшена совместимость с приводами Spin ([@TheGoblinHero](https://github.com/TheGoblinHero))
* Добавлена функция задания произвольного положения привода ([@TheGoblinHero](https://github.com/TheGoblinHero))

Если проект заинтересовал, вы можете [купить мне пиво или кофе](https://www.tinkoff.ru/cf/12xvN3UtJkO)


