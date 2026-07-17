# Wireless Communication Layer (WCL)

Transport subsystem for ALIVE. Moves biometric frames from the BAU (STM32) to the
AGE (UE5) and carries haptic commands back the other way.

```
STM32 (BAU)  --UART 921600, COBS+protobuf-->  ESP32  --Wi-Fi / MQTT-->  Mosquitto  -->  UE5 (AGE)
                                                                          ^ topics:
                                                                          alive/biometrics (QoS 0)
                                                                          alive/haptics    (QoS 1)
```

The wire format is **protobuf** (schema in `proto/alive.proto`), framed with **COBS**
(`0x00` delimiter) so the byte stream can resync after any glitch.

## Layout

| Path | What |
|------|------|
| `proto/alive.proto` | Single source of truth for the wire schema. |
| `../Core/Src/wcl.c`, `../Core/Inc/wcl.h` | STM32 side: owns USART1, encodes + frames + transmits. |
| `../Core/Src/cobs.c`, `../Core/Inc/cobs.h` | COBS framing (shared by STM32 and ESP32). |
| `esp32/wcl_esp32_bridge/` | ESP32 Arduino sketch: UART <-> MQTT bridge. |

STM32 sources live under `Core/` so STM32CubeIDE compiles them with no project
config. The generated nanopb stubs (`alive.pb.c/.pb.h`) and the nanopb runtime
also need to land where each build can see them (see below).

> **Note:** nanopb has two parts that are shipped separately:
> 1. the **generator** (a Python tool) — from `pip`, and
> 2. the **C runtime** (`pb.h`, `pb_encode.c`, `pb_decode.c`, `pb_common.c` +
>    headers) — from the nanopb **GitHub release**, not from pip.
>
> Both must be the **same version** (currently 0.4.9.1). This repo already has
> the generated stubs and the matching runtime committed into `Core/`, so you
> only need the steps below if you change `alive.proto`.

## 1. Install the generator tooling

```
pip install nanopb grpcio-tools
```

`grpcio-tools` provides the `protoc` compiler that the nanopb generator shells
out to. Without it you get `FileNotFoundError: [WinError 2]`.

## 2. Generate the C stubs

From `firmware/WCL/` (the `generated/` dir must exist):

```
mkdir generated
python -m nanopb.generator.nanopb_generator -I proto -D generated proto/alive.proto
```

This produces `generated/alive.pb.h` and `generated/alive.pb.c`.

## 3. Get the matching C runtime (once per nanopb version)

Download these 7 files from `https://github.com/nanopb/nanopb` at the tag that
matches your generator version (e.g. `0.4.9.1`) into `Core/`:

- headers -> `firmware/Core/Inc/` : `pb.h`, `pb_common.h`, `pb_encode.h`, `pb_decode.h`
- sources -> `firmware/Core/Src/` : `pb_common.c`, `pb_encode.c`, `pb_decode.c`

## 4. Copy stubs into the STM32 project

- `generated/alive.pb.h` -> `firmware/Core/Inc/`
- `generated/alive.pb.c` -> `firmware/Core/Src/`

CubeIDE picks up new files in `Core/Src` on the next build (it regenerates
`Debug/.../subdir.mk`). No `.ioc` change is required — `wcl.c` configures USART1
by hand so regenerating from CubeMX will not clobber the bridge.

**For the ESP32 build**, drop these next to the `.ino`
(`esp32/wcl_esp32_bridge/`): `alive.pb.h`, `alive.pb.c`, `pb_common.c`,
`pb_encode.c`, `pb_decode.c`, and copies of `cobs.h`, `cobs.c` from `Core/`.

## 5. Wiring

Both boards are 3.3 V logic, so no level shifter is needed on the UART.

| STM32 (Nucleo) | ESP32-C6 DevKit | Note |
|----------------|-----------------|------|
| PA9  (USART1 TX) | GPIO5 (Serial1 RX) | data STM32 -> ESP32 |
| PA10 (USART1 RX) | GPIO4 (Serial1 TX) | haptics ESP32 -> STM32 |
| GND | GND | **common ground required** |

> **ESP32-C6 specifics:** the C6 has only 2 UARTs and **no `Serial2`** — the
> bridge uses `Serial1`. GPIO16/17 are the C6's UART0 console pins, so the bridge
> uses GPIO4/5 instead. In the Arduino IDE, set **Tools -> USB CDC On Boot ->
> Enabled**, otherwise `Serial` output never reaches the USB-C monitor.

> Power the ESP32 from **5V/VIN**, not the STM32 3.3 V rail. Wi-Fi TX current
> spikes (~350-500 mA) will brown out the Nucleo LDO and reset the MCU. Add a
> ~470 uF bulk cap across the ESP32 3V3/GND.

Debug `printf` stays on **USART2** (ST-Link virtual COM port), independent of the
WCL bridge, so you can watch decoded angles on USB while the radio stream runs.

## 6. Broker on the host PC

Install and run Mosquitto:

```
# Windows (choco) / macOS (brew) / Linux (apt)
mosquitto -v
```

Set `MQTT_HOST` in the `.ino` to the host PC's LAN IP, and fill in `WIFI_SSID` /
`WIFI_PASS`.

> The broker is currently unauthenticated (anonymous, port 1883). Fine for a
> closed lab network; before any shared/exposed deployment, enable a
> password file and TLS in `mosquitto.conf`.

## 7. End-to-end smoke test

1. Flash the STM32 (it already calls `WCL_Init()` and `WCL_SendBiometricFrame()`
   from `main.c`).
2. Flash the ESP32 bridge.
3. On the host, subscribe and watch frames arrive:

   ```
   mosquitto_sub -h localhost -t alive/biometrics -v
   ```

   You'll see raw protobuf bytes. The ESP32 serial console prints the decoded
   `roll/pitch/yaw` for a readable sanity check.
4. Test the reverse path — publish a haptic command (once you generate a valid
   encoded `HapticCommand` payload) to `alive/haptics` and confirm the STM32
   receives it over USART1.

## Status / next steps

- [x] Protobuf schema, COBS framing, STM32 TX path, ESP32 UART->MQTT bridge.
- [ ] STM32 **RX** path: parse inbound `alive/haptics` frames on USART1 (IRQ/DMA)
      and drive the vibration motor (F7).
- [ ] Move `WCL_SendBiometricFrame` onto the 100 Hz timer scheduler (N2) and swap
      blocking TX for `HAL_UART_Transmit_DMA`.
- [ ] Populate `yaw` (gyro integration + startup bias estimate), then HR and GSR.
- [ ] USB CDC deterministic fallback path.
