"""
ALIVE - Fake WCL biometrics publisher for UE5 pipeline testing.

Publishes synthetic alive.BiometricFrame protobuf messages to an MQTT broker
on topic alive/biometrics, in the same hand-rolled wire format AliveProto.cpp
decodes UE5-side (see firmware/wcl/proto/alive.proto). Lets AAliveBiometricsActor
be tested without the STM32/ESP32 hardware -- just a broker.

Also subscribes to alive/haptics and prints any HapticCommand it receives, so
the AGE -> BAU return path can be sanity-checked too.

Requires: pip install paho-mqtt

Usage:
    python3 fake_biometrics_mqtt_publisher.py                      # localhost:1883, 100 Hz
    python3 fake_biometrics_mqtt_publisher.py --host 192.168.1.50 --rate 100
"""
import argparse
import math
import struct
import time

import paho.mqtt.client as mqtt

TOPIC_BIOMETRICS = "alive/biometrics"
TOPIC_HAPTICS = "alive/haptics"


def encode_varint(value: int) -> bytes:
    out = bytearray()
    value &= 0xFFFFFFFFFFFFFFFF
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def encode_tag(field_number: int, wire_type: int) -> bytes:
    return encode_varint((field_number << 3) | wire_type)


def encode_biometric_frame(seq, timestamp_ms, roll, pitch, yaw, hr_bpm, gsr,
                            imu_valid, hr_valid, gsr_valid) -> bytes:
    WIRE_VARINT = 0
    WIRE_FIXED32 = 5

    out = bytearray()
    out += encode_tag(1, WIRE_VARINT) + encode_varint(seq)
    out += encode_tag(2, WIRE_VARINT) + encode_varint(timestamp_ms)
    out += encode_tag(3, WIRE_FIXED32) + struct.pack("<f", roll)
    out += encode_tag(4, WIRE_FIXED32) + struct.pack("<f", pitch)
    out += encode_tag(5, WIRE_FIXED32) + struct.pack("<f", yaw)
    out += encode_tag(6, WIRE_FIXED32) + struct.pack("<f", hr_bpm)
    out += encode_tag(7, WIRE_FIXED32) + struct.pack("<f", gsr)
    out += encode_tag(8, WIRE_VARINT) + encode_varint(1 if imu_valid else 0)
    out += encode_tag(9, WIRE_VARINT) + encode_varint(1 if hr_valid else 0)
    out += encode_tag(10, WIRE_VARINT) + encode_varint(1 if gsr_valid else 0)
    return bytes(out)


def decode_varint(buf: bytes, index: int):
    result = 0
    shift = 0
    while True:
        b = buf[index]
        index += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, index
        shift += 7


def decode_haptic_command(payload: bytes):
    fields = {}
    index = 0
    while index < len(payload):
        tag, index = decode_varint(payload, index)
        field_number = tag >> 3
        value, index = decode_varint(payload, index)
        fields[field_number] = value
    return fields.get(1, 0), fields.get(2, 0), fields.get(3, 0)


def on_message(client, userdata, msg):
    event_id, intensity, duration_ms = decode_haptic_command(msg.payload)
    print(f"[haptics] event_id={event_id} intensity={intensity} duration_ms={duration_ms}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--rate", type=float, default=100.0, help="frames per second")
    args = parser.parse_args()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="alive-fake-biometrics-publisher")
    client.on_message = on_message
    client.connect(args.host, args.port)
    client.subscribe(TOPIC_HAPTICS, qos=1)
    client.loop_start()

    period = 1.0 / args.rate
    print(f"Publishing fake BiometricFrames to {args.host}:{args.port} topic '{TOPIC_BIOMETRICS}' "
          f"at {args.rate} Hz. Listening on '{TOPIC_HAPTICS}'. Ctrl+C to stop.")

    seq = 0
    t0 = time.monotonic()

    try:
        while True:
            t = time.monotonic() - t0

            roll = 20.0 * math.sin(2 * math.pi * 0.15 * t)
            pitch = 15.0 * math.sin(2 * math.pi * 0.10 * t + 1.0)
            yaw = 30.0 * math.sin(2 * math.pi * 0.05 * t)
            hr_bpm = 70.0 + 5.0 * math.sin(2 * math.pi * 0.02 * t)
            gsr = 0.5 + 0.1 * math.sin(2 * math.pi * 0.03 * t)

            payload = encode_biometric_frame(
                seq=seq & 0xFFFFFFFF,
                timestamp_ms=int(t * 1000) & 0xFFFFFFFF,
                roll=roll, pitch=pitch, yaw=yaw,
                hr_bpm=hr_bpm, gsr=gsr,
                imu_valid=True, hr_valid=True, gsr_valid=True,
            )
            client.publish(TOPIC_BIOMETRICS, payload, qos=0)

            seq += 1
            time.sleep(period)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
