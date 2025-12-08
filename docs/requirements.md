# RC Transmitter–Receiver System Requirements (Inspired Repo)

> Document: project requirements and current progress (what we've implemented so far).

---

## 1. System Overview

The system consists of:

* **Transmitter Unit (TX)** — reads user inputs (joysticks, switches), builds and transmits structured control packets via NRF24L01, displays status on an OLED, receives telemetry ACK payloads from the receiver, and stores settings in EEPROM.
* **Receiver Unit (RX)** — receives and validates packets, drives servos/ESC outputs, implements failsafe, and sends telemetry (battery, link quality, status) back to the transmitter using NRF24 ACK payloads.

Core communication features: NRF24L01 at 1 Mbps, dynamic payloads, ACK payloads, retry/ARQ, structured packets with CRC-16.

---

## 2. Requirements (summary)

### 2.1 Packet Structure (TX → RX)

```
version (1B)
device_id (1B)
channel_count (1B)
channels[] (N × int16)
seq_tx (2B)
crc (2B)  // CRC-16-CCITT over all preceding bytes
```

### 2.2 Telemetry (ACK payload RX → TX)

```
seq_rx (2B)
battery_mV (2B)
rssi_est (1B)
status   (1B) // bitmask: link OK / failsafe / low batt / ID mismatch
```

### 2.3 Functional Requirements (high-level)

* TX: read analog sticks, apply calibration, send packets at 5–20 Hz, parse ACK telemetry, show on OLED, save settings to EEPROM.
* RX: validate packets (CRC, version, device_id), map channels to PWM outputs, implement 150 ms failsafe, send ACK telemetry, support binding.

### 2.4 Communication / Timing

* Data rate: 1 Mbps
* Dynamic payloads & ACK payloads enabled
* Retries: default tuned (e.g., setRetries(5,15))
* Failsafe timeout: 150 ms
* TX link-loss threshold for user: 300 ms without ACK

### 2.5 Safety

* Never actuate servos/ESC on invalid packets
* Enforce binding and ID filtering
* Send and show low-battery and failsafe states

---

## 3. Progress so far (what we have implemented and tested)

1. **Repository basis confirmed** — Project inspired from: `https://github.com/TheDIYGuy999/RC_Transmitter` and used as the starting design.

2. **Basic RF Link (sample packet)**

   * Implemented a minimal transmitter and receiver example using the RF24 library (CE=7, CSN=8) that sends a small `Telemetry` struct and prints on RX to verify connectivity.
   * Wiring and library notes documented (3.3V supply advice, SPI pins, decoupling recommendation).

3. **Structured Packet with CRC (TX ↔ RX)**

   * Designed and implemented a packed `Packet` struct containing `version`, `device_id`, `channel_count`, `channels[]`, and `crc` (CRC-16-CCITT `0x1021`, init `0xFFFF`).
   * TX computes CRC over bytes before CRC field; RX computes and verifies CRC before accepting the packet.
   * Example Arduino sketches provided and tested; CRC validation working.

4. **ACK Payload Telemetry (RX → TX)**

   * Implemented ACK payload using `radio.writeAckPayload()` on RX and `radio.isAckPayloadAvailable()` / `radio.read()` on TX.
   * Telemetry structure implemented: `seq`, `battery_mV` (simulated), `rssi_est` (placeholder), `status` flags.
   * End-to-end bidirectional link verified — TX sees ACK telemetry after sending packets.

5. **PDF Requirements Document generated**

   * Created a PDF `RC_Transmitter_Receiver_Requirements.pdf` containing the requirements, and provided a download link for it.

6. **Markdown requirements version created**

   * Converted the requirement doc to markdown for inclusion in MkDocs or repo docs.

7. **Validation & Testing**

   * Serial monitor tests validated packet send/receive and CRC OK messages.
   * ACK payload flow tested and printed in serial logs on TX and RX.

---

## 4. Artifacts & Files produced

* Arduino example sketches (TX/RX) for:

  * Minimal sample packet link
  * Structured packet with CRC
  * ACK-payload telemetry

* `RC_Transmitter_Receiver_Requirements.pdf` (generated)

* `requirements.md` (markdown version of this requirements doc)

> NOTE: all code and docs were produced in-chat; you can request I push them into your GitHub repo structure (I can produce ready-to-commit files/zip).

---

## 5. Recommended next steps (pick one or more)

* **A. Real battery ADC on RX** — replace simulated battery values with ADC readings and scale with voltage divider.
* **B. Add seq_tx in main packet & echo in ACK** — correlates ACKs to specific TX packets to compute packet loss/latency.
* **C. PWM outputs on RX** — map channels to servo/ESC PWM and implement failsafe action.
* **D. EEPROM binding** — implement bind mode, store TX UID + vehicle ID in RX EEPROM and enforce filtering.
* **E. OLED UI on TX** — live channel graph, telemetry display, model selection & calibration menu.
* **F. EEPROM save/load on TX** — persist calibration and selected model.

---

## 6. What I can deliver next (pick one)

* Full TX + RX code for any of the recommended items above (A–F).
* A block diagram or packet structure diagram (Mermaid or PNG).
* A tidy GitHub-ready repo layout and commit patch/zip with the code & docs.

---

## 7. Notes & Implementation Considerations

* Endianness: current AVR/Arduino is little-endian — if you change MCU family, verify packing/endian behavior.
* CRC: uses CRC-16-CCITT. If you add fields later, keep CRC computed over preceding data.
* ACK payload: ensure `enableAckPayload()` and `enableDynamicPayloads()` are set on both ends.
* Safety: always test servo/ESC outputs with props removed and wheels suspended.

---

*If you want, I will now create the canvas page content as separate files (sketches, diagrams) and add them into the canvas. Or I can export this canvas to a GitHub `docs/requirements.md` file or commit-ready patch.*
