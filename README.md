# ESP32 CRTP Sender (int16 variant)

This repository contains example sketches to send CRTP (Crazy RealTime Protocol) setpoints
to a LiteWing/Crazyflie-like drone using an ESP32 over UDP.

Files:
- esp32-drone.ino: float-based example (already on main)
- esp32-drone-int16.ino: this int16 scaled setpoint example (added on branch)
- README.md: usage and safety notes

Important: test with propellers removed, start with low thrust, and verify your drone's
expected packet layout (header, scaling, endianness) before attempting to fly.
