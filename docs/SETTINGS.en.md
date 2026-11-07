# Nice drive settings from Home Assistant

[Русский](SETTINGS.ru.md) · [Українська](SETTINGS.uk.md) · [English](SETTINGS.en.md)

The `bus_t4` component reads and changes Nice control-unit parameters over the BusT4 bus —
the same things you set with the buttons on the control unit or with Nice O-View, but from Home Assistant.

## How it works

* Every parameter is a separate ESPHome entity: `switch` (on/off), `number`, `select` (mode),
  `sensor` (read-only), `button` (command/calibration).
* **The value always comes from the drive.** Parameters are read after the ESP starts and then every
  `params_interval` (5 minutes by default). After a write the parameter is read back immediately, so
  Home Assistant shows what the control unit actually accepted.
* If you change a setting with the buttons on the drive, HA picks it up on the next read
  (or right away with the "Refresh settings" button).
* If the control unit does not support a parameter it answers with an error and the entity stays empty.
  Better not to add unsupported parameters to the YAML.
* Out-of-range values are rejected by the drive and the entity keeps showing the previous value.

## Setup

```yaml
external_components:
  - source: github://srepenko/Nice_BusT4@main
    refresh: 0s

cover:
  - platform: bus_t4
    id: nice_cover
    name: "Nice Cover"
    device_class: gate
    tx_pin: 21
    rx_pin: 20
    params_interval: 5min      # how often to re-read the settings (optional)
```

All entities below use `platform: bus_t4` and (optionally) `bus_t4_id: nice_cover` if you have more than one cover.

> **Names.** ESPHome 2026+ builds the object_id from ASCII, so non-Latin names of the same length collide.
> Use Latin names in YAML and set localized names in Home Assistant.

## Functions (`switch`)

| `type` | Register | What it does |
|---|---|---|
| `auto_close` | 0x80 | **Automatic closing** (L1). After a full opening the gate closes by itself after the pause time. |
| `photo_close` | 0x84 | **Close after photo** (L2). After the photocells have been crossed the gate closes after `photo_close_time` instead of the full pause. |
| `always_close` | 0x88 | **Always close** (L3). If the gate is open when power returns, the unit closes it after `always_close_time`. |
| `standby` | 0x8C | **Stand-by / energy saving** (L4). After `standby_time` of inactivity the unit switches off some loads (see `standby_mode`). |
| `preflash` | 0x94 | **Pre-flashing**. The flashing light blinks before the movement starts (`preflash_open_time` / `preflash_close_time`). |
| `operator_block` | 0x9A | **Operator block**. The drive ignores control commands (maintenance, cleaning). |
| `key_lock` | 0x9C | **Key lock** of the control-unit buttons (protects the settings). |
| `custom` + `register` | any | Any 0/1 flag parameter of the protocol. |

## Numeric parameters (`number`)

| `type` | Register | Unit | Description |
|---|---|---|---|
| `pause_time` | 0x81 | s | **Pause time** — how long the gate stays open before automatic closing. |
| `photo_close_time` | 0x85 | s | Wait before closing after the photocells were crossed. |
| `always_close_time` | 0x89 | s | Delay before closing after power-up ("Always close"). |
| `standby_time` | 0x8D | s | Inactivity time before stand-by. |
| `preflash_open_time` | 0x95 | s | Pre-flashing before opening. |
| `preflash_close_time` | 0x99 | s | Pre-flashing before closing. |
| `lamp_time` | 0x5B | s | Courtesy light time after a manoeuvre. |
| `lock_time` | 0x5A | s | Electric lock pulse time. |
| `suction_cup_time` | 0x5C | s | Electromagnetic suction cup time. |
| `speed_open` / `speed_close` | 0x42 / 0x43 | level | Opening / closing speed. |
| `speed_slow_open` / `speed_slow_close` | 0x45 / 0x46 | level | Speed in the slow-down zone near the end positions. |
| `force_open` / `force_close` | 0x4A / 0x4B | level | Force (obstacle sensitivity threshold). Lower — more sensitive and safer, higher — pushes harder (wind, stiff hinges). |
| `leaf_delay_open` / `leaf_delay_close` | 0x28 / 0x29 | unit-specific | Leaf phase shift on double-leaf gates: the second leaf starts later so the leaves do not collide. |
| `reverse_time` | 0x31 | unit-specific | Duration of the brief reverse after an obstacle is detected. |
| `custom` | any | — | Any numeric parameter: `register`, `bytes` (1–4), `multiplier`, `min_value`, `max_value`, `step`. |

**Speed and force levels** are the scale of the particular control unit. The component allows 1–8; if the
unit supports fewer levels it rejects the extra values. Factory values on Nice Walky (WLA1): speed 3,
slow-down 2, force 2.

**Safety.** Do not raise the force without a reason: it is the threshold at which the drive decides it has
hit an obstacle. After changing speed/force check that the gate stops and reverses when you hold a leaf by hand.

## Modes (`select`)

| `type` | Register | Options |
|---|---|---|
| `standby_mode` | 0x8E | `Safety` — switch off safety devices; `BlueBus` — switch off the BlueBus; `All` — everything. |
| `custom` | any | Your own `options: {"Label": value}`. |

## Sensors (`sensor`, read-only)

| `type` | Register | Description |
|---|---|---|
| `maneuvers_count` | 0xB2 | Manoeuvre counter since the last maintenance. |
| `maintenance_threshold` | 0xB1 | Maintenance threshold (manoeuvres). When the counter reaches it the unit signals that service is due. |
| `custom` | any | `register`, `bytes`, `multiplier`. |

## Buttons (`button`)

| `type` | What it does |
|---|---|
| `learn_positions` | **Position learning / leaf length recognition.** The gate runs a full cycle and the unit stores the end positions. Needed after installation, adjusting the stops or replacing the motor. ⚠️ The gate will move — make sure nobody is in the passage. |
| `learn_bluebus` | **BlueBus device recognition** (photocells, keypads, etc.). Needed after adding/replacing devices on the bus. |
| `cancel_maintenance` | **Cancel maintenance** — resets the service counter. Do it after servicing the drive. |
| `refresh` | Re-read all settings from the drive now. |
| `step_by_step`, `open`, `close`, `stop`, `partial_open` | Control commands. |

## Full YAML example

See [`examples/nice-walky.yaml`](../examples/nice-walky.yaml).

## Tested

Nice **Walky** (WLA1 control unit, swing gate, 2 leaves): 52 parameters supported. Not supported:
partial openings 1–3, encoder positions, slow-down on/off, soft start, inputs IN2–IN4.

Other control units (ROBUS, SPIN, MC824H, DPRO924…) may support a different set. To find out what your
unit supports, send GET requests with the `send_inf_command` service (`type_command: 99`) and watch the
log: error `FD`/`F9` means the parameter is not supported.
