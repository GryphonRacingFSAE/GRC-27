# Gryphon Racing Telemetry System

## Purpose

The telemetry system should carry vehicle CAN measurements wirelessly to a computer in the pits for live monitoring, session logging, and later analysis. It should help the team understand vehicle performance, monitor sensor health, and investigate issues during testing.

This document describes the intended functionality for a fresh firmware implementation. The previous system can inform development, while the new design's internal structure and communication format remain open.

## Main components

- **Remote Transmitter (RTU):** Collect data from the MaxxECU, Aero Probe, and IMU + GPS boards over CAN, then send telemetry to the pits over LoRa.
- **Remote Receiver (RRU):** Receive and validate wireless telemetry, then provide structured data to a connected computer over USB serial.
- **Telemetry software:** Read the receiver's output, make measurements available for live monitoring, and save CSV logs for later review.

The telemetry system observes vehicle data. Its scope does not include controlling the ECU, sensors, or vehicle systems.

## Vehicle data

[GRC27.dbc](GRC27.dbc) has already been ported and is the source of truth for supported CAN messages, signal definitions, scaling, signedness, and units.

The system should support the measurements and status information defined in that file, including engine and vehicle operating data, aerodynamic measurements, acceleration and rotational motion, GPS position and speed, and sensor-node health information. Decoded values should preserve the DBC's meaning and precision throughout transmission and logging.

## Expected behaviour

- **Independent sources:** Available measurements should continue flowing when other boards are disconnected or silent. Startup should not require every CAN source to be present.
- **Clear data quality:** Distinguish valid measurements, values never received, and stale readings. Missing data must not appear as a valid zero, and old readings must not appear current. Preserve source health and fault information separately from freshness.
- **Useful update rates:** Prioritise rapidly changing measurements and important status changes while keeping overall traffic within the wireless link's capacity.
- **Reliable operation:** Reject malformed or incompatible data, expose communication problems, and recover from temporary interruptions. Wireless or computer-side delays should not stall CAN acquisition.
- **Usable output:** Provide a documented, consistent serial format and enough timing, delivery, and link-quality information to interpret measurements and identify gaps in the logs.

## Development and validation

Support bench testing with simulated CAN data before testing on the car. Verify decoding against the DBC, operation with missing or stale sources, recovery after interruptions, and sustained delivery through to the computer.

Packet layouts, transmission rates, radio settings, buffering, firmware organisation, and build instructions should be defined during implementation. Confirm practical throughput and range on the actual boards before vehicle use.