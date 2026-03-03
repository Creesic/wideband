# CAN Message Encoding

This document describes exactly how the firmware encodes CAN messages—what each byte means.

The firmware uses **little-endian** byte order (ARM Cortex-M). Structs are cast directly to the CAN frame buffer, so the layout matches the C struct layout in memory.

---

## Ch0 only (single-channel)

Single-channel boards send exactly two messages at 100 Hz:

| CAN ID | Message | DLC |
|--------|---------|-----|
| 0x190 | WidebandStandardData | 8 |
| 0x191 | WidebandDiagData | 8 |

Byte layout for each is described in sections 1 and 2 below.

---

## 1. WidebandStandardData (ID 0x190 + offset, DLC 8)

Sent when `RusEfiTx` is enabled. Base ID: `0x190`, plus `RusEfiIdOffset` per channel (e.g. channel 0 → 0x190, channel 1 → 0x192).

| Byte | Bits | Field | Type | Encoding | Notes |
|------|------|-------|------|----------|-------|
| 0 | 7:0 | Version | uint8_t | 0xA0 | Fixed; used for compatibility |
| 1 | 7:0 | Valid | uint8_t | 0 or 1 | 1 = lambda valid |
| 2 | 7:0 | Lambda low | uint16_t LSB | — | Lambda × 10000, little-endian |
| 3 | 7:0 | Lambda high | uint16_t MSB | — | e.g. λ=1.0 → 10000 → 0x2710 → bytes 0x10, 0x27 |
| 4 | 7:0 | TemperatureC low | uint16_t LSB | — | °C, little-endian |
| 5 | 7:0 | TemperatureC high | uint16_t MSB | — | e.g. 300°C → 0x012C → bytes 0x2C, 0x01 |
| 6 | 7:0 | pad low | uint16_t LSB | 0 | Unused |
| 7 | 7:0 | pad high | uint16_t MSB | 0 | Unused |

**Formula:** `physical_value = raw_value × factor + offset`
- Lambda: factor 0.0001, offset 0 → `lambda = raw / 10000`
- TemperatureC: factor 1, offset 0 → `temp = raw`
- Valid: 0 = invalid, 1 = valid

---

## 2. WidebandDiagData (ID 0x191 + offset, DLC 8)

Sent when `RusEfiTxDiag` is enabled. Base ID: `0x190 + 1`, plus `RusEfiIdOffset`.

| Byte | Bits | Field | Type | Encoding | Notes |
|------|------|-------|------|----------|-------|
| 0 | 7:0 | Esr low | uint16_t LSB | — | ESR in Ω, little-endian |
| 1 | 7:0 | Esr high | uint16_t MSB | — | e.g. 200 Ω → 0x00C8 → bytes 0xC8, 0x00 |
| 2 | 7:0 | NernstDc low | uint16_t LSB | — | Nernst DC × 1000, little-endian |
| 3 | 7:0 | NernstDc high | uint16_t MSB | — | e.g. 0.45 V → 450 → 0x01C2 → bytes 0xC2, 0x01 |
| 4 | 7:0 | PumpDuty | uint8_t | 0–255 | Pump duty × 255 (0–100%) |
| 5 | 7:0 | Status | uint8_t | Fault enum | 0=OK, 3=DidntHeat, 4=Overheat, 5=Underheat, 6=NoHeatSupply |
| 6 | 7:0 | HeaterDuty | uint8_t | 0–255 | Heater duty × 255 (0–100%) |
| 7 | 7:0 | pad | uint8_t | 0 | Unused |

**Formulas:**
- Esr: factor 1, offset 0 → `esr = raw`
- NernstDc: factor 0.001, offset 0 → `nernst = raw / 1000`
- PumpDuty: factor 0.392157, offset 0 → `pump% = raw × 100/255`
- HeaterDuty: same as PumpDuty

---

## 3. AEMNet UEGO (ID 0x180 + offset, 29-bit ext, DLC 8)

Sent when `AemNetTx` is enabled. Uses **big-endian** (`beuint16_t`).

| Byte | Field | Encoding |
|------|-------|----------|
| 0–1 | Lambda | Big-endian, λ × 10000 |
| 2–3 | Oxygen | Big-endian, 0.001 %/bit (currently 0) |
| 4 | SystemVolts | 0.1 V/bit |
| 5 | reserved | 0 |
| 6 | Flags | Bit 7: lambda valid, Bit 1: LSU4.9 |
| 7 | Faults | Currently 0 |

---

## 4. SetIndex (ID 0xEF40000) — Received from ECU

Sets the base CAN ID for RusEFI format. DLC 2 only. Same byte layout as Ping: `[0]=high, [1]=low`.

| Byte | Field | Encoding |
|------|-------|----------|
| 0 | BaseIdHigh | High byte of 11-bit base ID (0–7) |
| 1 | BaseIdLow | Low byte of base ID |

Example: `01 90` → base 0x190. AFR ch0 = 0x190, ch1 = 0x192. Persists to flash.

---

## 5. WidebandControl (ID 0xEF50000) — Received from ECU

| Byte | Field | Encoding |
|------|-------|----------|
| 0 | BatteryVoltage | 0.1 V (e.g. 140 = 14.0 V) |
| 1 | HeaterEnable | Bit 0: 1 = allow heating |
| 2 | (optional) | Pump gain % × 100 (0–200) if DLC ≥ 3 |

---

## 6. Ping (ID 0xEF60000) — Received from ECU

Any message on this ID is a ping. Payload = base CAN ID being asked for. Controller replies with Pong (WB_ACK, DLC 8) if its `RusEfiBaseId` matches.

| DLC | Bytes | Encoding |
|-----|-------|----------|
| 1 | [0] | Low byte of base CAN ID |
| 2 | [0, 1] | Full base ID: `[0]=high, [1]=low` (e.g. `01 90` → 0x190) |

**Pong reply (EID 0x727573, DLC 8):**
| Byte | Field |
|------|-------|
| 0 | baseId low |
| 1 | baseId high |
| 2 | Version (0xA0) |
| 3 | year (from 2000) |
| 4 | month |
| 5 | day |
| 6–7 | reserved |

---

## 7. SetSensorType (ID 0xEF70000) — Received from ECU

| Byte | Field | Encoding |
|------|-------|----------|
| 0 | hwIdx | Hardware index (0 = this controller) |
| 1 | type | 0=LSU4.9, 1=LSU4.2, 2=LSU ADV, 3=FAE LSU4.9 |

---

## 8. HeaterConfig (ID 0xEF80000) — Received from ECU

Configures heater thresholds and preheat time. DLC ≥ 3. Values are stored and persist to flash.

| Byte | Field | Encoding |
|------|-------|----------|
| 0 | HeaterSupplyOffVoltage | 0.1 V steps (e.g. 85 = 8.5 V) — below this, heater auto-start disabled |
| 1 | HeaterSupplyOnVoltage | 0.1 V steps (e.g. 95 = 9.5 V) — above this, heater auto-start allowed |
| 2 | PreheatTimeSec | Preheat phase duration in seconds (0 = use firmware default) |

---

## Endianness Summary

- **RusEfi format (StandardData, DiagData):** little-endian
- **AEMNet format:** big-endian for 16-bit fields
