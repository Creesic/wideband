# CAN Message Functionality

Overview of all CAN message functionality in the wideband firmware.

---

## Transmit (Wideband → ECU)

| Message | CAN ID | DLC | Rate | Protocol | Enable Flag |
|---------|--------|-----|------|----------|-------------|
| **WidebandStandardData** | `RusEfiBaseId` + 0 | 8 | 100 Hz | RusEfi (little-endian) | `RusEfiTx` |
| **WidebandDiagData** | `RusEfiBaseId` + 1 | 8 | 100 Hz | RusEfi (little-endian) | `RusEfiTxDiag` |
| **AEMNet UEGO** | 0x180 + `AemNetIdOffset` | 8 | 100 Hz | AEMNet (29-bit ext, big-endian) | `AemNetTx` |
| **AEMNet EGT** | 0x0A0305 + `AemNetIdOffset` | 8 | 20 Hz | AEMNet (29-bit ext, big-endian) | `egt[ch].AemNetTx` |
| **Pong (WB_ACK)** | 0x727573 (EID) | 8 | On request | Response to Ping | — |

**Transmit schedule:** AFR at 100 Hz, EGT every 5th cycle (20 Hz). `SendCanForChannel()` calls both `SendRusefiFormat()` and `SendAemNetUEGOFormat()`; boards can override it.

---

## Receive (ECU → Wideband)

All received messages use 29-bit extended IDs with header `0xEF`:

| Message | CAN ID | DLC | Purpose |
|---------|--------|-----|---------|
| **Bootloader Enter** | 0xEF000000 | 0 or 1 | Reboot to bootloader (0xFF or low byte of `RusEfiBaseId`) |
| **SetIndex** | 0xEF40000 | 2 | Set CAN base ID for RusEFI format (see below) |
| **WidebandControl (ECU Status)** | 0xEF50000 | ≥2 | Battery voltage, heater enable; optional pump gain if DLC ≥ 3 |
| **Ping** | 0xEF60000 | 1 or 2 | Request version/build date; payload = base CAN ID (DLC 1: low byte; DLC 2: [high, low] e.g. `01 90` → 0x190); controller replies with Pong if it matches |
| **SetSensorType** | 0xEF70000 | ≥2 | Set sensor type (0=LSU4.9, 1=LSU4.2, 2=LSU ADV, 3=FAE LSU4.9) |
| **HeaterConfig** | 0xEF80000 | ≥3 | Heater thresholds and preheat time; stored in flash |

---

## SetIndex (0xEF40000)

Sets the base CAN ID for RusEFI format. DLC 2 only. Same byte layout as Ping: `[0]=high, [1]=low` (e.g. `01 90` → 0x190).

- Base ID must be 11-bit standard (0–0x7FF).
- AFR channels: `RusEfiBaseId = baseId + ch * 2` (StandardData at base, DiagData at base+1).
- EGT channels: `RusEfiIdOffset` derived from base.
- Persists to flash.

---

## Data Layouts

See [can-encoding.md](can-encoding.md) for byte-level encoding details.

- **WidebandStandardData:** Version (0xA0), Valid, Lambda×10000, TemperatureC, padding
- **WidebandDiagData:** ESR, NernstDc×1000, PumpDuty, Status (fault enum), HeaterDuty, padding
- **AEMNet UEGO:** Lambda (big-endian), Oxygen, SystemVolts, Flags (bit 7 = valid, bit 1 = LSU4.9), Faults
- **WidebandControl:** BatteryVoltage (0.1 V), HeaterEnable (bit 0), optional PumpGain (DLC ≥ 3)
- **Pong:** baseId (low, high), Version (0xA0), year, month, day, reserved

---

## Configuration

- **RusEfiBaseId** (AFR) — Full 11-bit base CAN ID per channel (StandardData at base, DiagData at base+1). Default 0x190 for ch0, 0x192 for ch1.
- **AemNetIdOffset** — Base 0x180 for UEGO, 0x0A0305 for EGT
- **RusEfiIdOffset** (EGT) — Offset from 0x190 for EGT channels
