"""
ALIVE - Fake IMU sender for UE5 pipeline testing.

Streams synthetic roll/pitch/yaw/acceleration frames over UDP in the same
binary layout the STM32 firmware will eventually use, so the UE5 receiver
can be built and tested before real hardware is on the bench.

Frame layout (little-endian, 32 bytes, matches struct FImuFrame in
Source/engine/ImuUdpReceiver.h):
    uint32  seq
    uint32  timestamp_ms
    float   roll_deg
    float   pitch_deg
    float   yaw_deg
    float   accel_x_g
    float   accel_y_g
    float   accel_z_g

Usage:
    python3 fake_imu_sender.py                     # defaults: 127.0.0.1:9999, 100 Hz
    python3 fake_imu_sender.py --host 127.0.0.1 --port 9999 --rate 100
"""
import argparse
import math
import socket
import struct
import time

FRAME_FMT = "<IIffffff"  # seq, timestamp_ms, roll, pitch, yaw, ax, ay, az
FRAME_SIZE = struct.calcsize(FRAME_FMT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--rate", type=float, default=100.0, help="frames per second (N2 = 100 Hz for IMU)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    period = 1.0 / args.rate

    print(f"Sending fake IMU frames to {args.host}:{args.port} at {args.rate} Hz "
          f"({FRAME_SIZE} bytes/frame). Ctrl+C to stop.")

    seq = 0
    t0 = time.monotonic()
    yaw_drift_bias_deg_s = 0.15  # mimics uncalibrated gyro bias from Section 3.1.5

    try:
        while True:
            t = time.monotonic() - t0

            # gentle oscillation on roll/pitch, slow drift on yaw -- enough
            # motion to visibly confirm the actor is rotating live.
            roll = 20.0 * math.sin(2 * math.pi * 0.15 * t)
            pitch = 15.0 * math.sin(2 * math.pi * 0.10 * t + 1.0)
            yaw = (30.0 * math.sin(2 * math.pi * 0.05 * t)) + (yaw_drift_bias_deg_s * t)

            accel_x = 0.02 * math.sin(2 * math.pi * 0.3 * t)
            accel_y = 0.02 * math.cos(2 * math.pi * 0.3 * t)
            accel_z = 1.0  # gravity, board roughly level

            frame = struct.pack(
                FRAME_FMT,
                seq & 0xFFFFFFFF,
                int(t * 1000) & 0xFFFFFFFF,
                roll, pitch, yaw,
                accel_x, accel_y, accel_z,
            )
            sock.sendto(frame, (args.host, args.port))

            seq += 1
            time.sleep(period)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()


if __name__ == "__main__":
    main()