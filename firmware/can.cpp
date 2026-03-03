#include "can.h"
#include "hal.h"

#include "can_helper.h"
#include "can_aemnet.h"

#include "port.h"
#include "pump_control.h"
#include "fault.h"
#include <rusefi/math.h>
#include "heater_control.h"
#include "lambda_conversion.h"
#include "sampling.h"
#include "pump_dac.h"
#include "max3185x.h"

// this same header is imported by rusEFI to get struct layouts and firmware version
#include "../for_rusefi/wideband_can.h"

static Configuration* configuration;

static THD_WORKING_AREA(waCanTxThread, 256);
void CanTxThread(void*)
{
    int cycle;
    chRegSetThreadName("CAN Tx");

    systime_t prev = chVTGetSystemTime(); // Current system time.

    while(1)
    {
        // AFR - 100 Hz
        for (int ch = 0; ch < AFR_CHANNELS; ch++) {
            SendCanForChannel(ch);
        }

        // EGT - 20 Hz
        if ((cycle % 5) == 0) {
            for (int ch = 0; ch < EGT_CHANNELS; ch++) {
                SendCanEgtForChannel(ch);
            }
        }

        cycle++;
        prev = chThdSleepUntilWindowed(prev, chTimeAddX(prev, TIME_MS2I(WBO_TX_PERIOD_MS)));
    }
}

static void SendAck()
{
    CANTxFrame frame;

#ifdef STM32G4XX
    frame.common.RTR = 0;
#else
    frame.RTR = CAN_RTR_DATA;
#endif
    CAN_EXT(frame) = 1;
    CAN_EID(frame) = WB_ACK;
    frame.DLC = 0;

    canTransmitTimeout(&CAND1, CAN_ANY_MAILBOX, &frame, TIME_INFINITE);
}

// Parse __DATE__ ("Mmm dd yyyy") into year (from 2000), month, day for Pong
static void GetBuildDate(uint8_t* year, uint8_t* month, uint8_t* day)
{
    const char* d = __DATE__;
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    *month = 1;
    for (int m = 0; m < 12; m++) {
        if (d[0] == months[m][0] && d[1] == months[m][1] && d[2] == months[m][2]) {
            *month = m + 1;
            break;
        }
    }
    d += 4;
    *day = (d[0] == ' ') ? (uint8_t)(d[1] - '0') : (uint8_t)((d[0] - '0') * 10 + (d[1] - '0'));
    d += 3;
    int y = (d[3] - '0') + (d[2] - '0') * 10 + (d[1] - '0') * 100 + (d[0] - '0') * 1000;
    *year = (uint8_t)(y - 2000);
}

static void SendPong(uint16_t baseId)
{
    uint8_t year, month, day;
    GetBuildDate(&year, &month, &day);

    CANTxFrame frame;

#ifdef STM32G4XX
    frame.common.RTR = 0;
#else
    frame.RTR = CAN_RTR_DATA;
#endif
    CAN_EXT(frame) = 1;
    CAN_EID(frame) = WB_ACK;
    frame.DLC = 8;
    frame.data8[0] = baseId & 0xFF;
    frame.data8[1] = baseId >> 8;
    frame.data8[2] = RUSEFI_WIDEBAND_VERSION;
    frame.data8[3] = year;
    frame.data8[4] = month;
    frame.data8[5] = day;
    frame.data8[6] = 0;
    frame.data8[7] = 0;

    canTransmitTimeout(&CAND1, CAN_ANY_MAILBOX, &frame, TIME_INFINITE);
}

// Start in Unknown state. If no CAN message is ever received, we operate
// on internal battery sense etc.
static HeaterAllow heaterAllow = HeaterAllow::Unknown;
static float remoteBatteryVoltage = 0;

static THD_WORKING_AREA(waCanRxThread, 512);
void CanRxThread(void*)
{
    chRegSetThreadName("CAN Rx");

    while(1)
    {
        CANRxFrame frame;
        msg_t msg = canReceiveTimeout(&CAND1, CAN_ANY_MAILBOX, &frame, TIME_INFINITE);

        // Ignore non-ok results...
        if (msg != MSG_OK)
        {
            continue;
        }

        // Ignore std frames, only listen to ext
        if (!CAN_EXT(frame))
        {
            continue;
        }

        // Ignore frames not in our protocol (header 0xEFx)
        if (WB_MSG_GET_HEADER(CAN_ID(frame)) != WB_BL_HEADER)
        {
            continue;
        }

        if (frame.DLC >= 2 && CAN_ID(frame) == WB_MSG_ECU_STATUS) {
            // This is status from ECU - battery voltage and heater enable signal

            // data1 contains heater enable bit
            if ((frame.data8[1] & 0x1) == 0x1)
            {
                heaterAllow = HeaterAllow::Allowed;
            }
            else
            {
                heaterAllow = HeaterAllow::NotAllowed;
            }

            // data0 contains battery voltage in tenths of a volt
            float vbatt = frame.data8[0] * 0.1f;
            if (vbatt < 5)
            {
                // provided vbatt is bogus, default to 14v nominal
                remoteBatteryVoltage = 14;
            }
            else
            {
                remoteBatteryVoltage = vbatt;
            }

            if (frame.DLC >= 3) {
                // data2 contains pump controller gain in percent (0-200)
                float pumpGain = frame.data8[2] * 0.01f;
                SetPumpGainAdjust(clampF(0, pumpGain, 1));
            }
        }
        // If it's a bootloader entry request, reboot to the bootloader!
        else if ((frame.DLC == 0 || frame.DLC == 1) && CAN_ID(frame) == WB_BL_ENTER)
        {
            // If 0xFF (force update all) or our base ID low byte, reset to bootloader, otherwise ignore
            if (frame.DLC == 0 || frame.data8[0] == 0xFF || frame.data8[0] == (GetConfiguration()->afr[0].RusEfiBaseId & 0xFF))
            {
                SendAck();

                // Let the message get out before we reset the chip
                chThdSleep(50);

                NVIC_SystemReset();
            }
        }
        // Ping: any message on 0xEF60000 is a ping. Payload = base ID being asked for.
        // DLC 1: [0] = low byte of base ID. DLC 2: [0]=high, [1]=low (e.g. 01 90 = 0x190)
        else if (frame.DLC >= 1 && CAN_ID(frame) == WB_MSG_PING)
        {
            for (int ch = 0; ch < AFR_CHANNELS; ch++) {
                uint16_t ourBaseId = GetConfiguration()->afr[ch].RusEfiBaseId;
                bool match = (frame.DLC >= 2)
                    ? (ourBaseId == ((frame.data8[0] << 8) | frame.data8[1]))
                    : ((ourBaseId & 0xFF) == frame.data8[0]);
                if (match) {
                    SendPong(ourBaseId);
                    break;
                }
            }
        }
        // Set sensor type: [0]=hwIdx, [1]=type. 0=LSU4.9, 1=LSU4.2, 2=LSU ADV, 3=FAE LSU4.9
        else if (frame.DLC >= 2 && CAN_ID(frame) == WB_MSG_SET_SENSOR_TYPE)
        {
            uint8_t hwIdx = frame.data8[0];
            uint8_t type = frame.data8[1];
            if (hwIdx == 0 && type <= 3) {
                configuration = GetConfiguration();
                configuration->sensorType = static_cast<SensorType>(type);
                SetConfiguration();
                SendAck();
            }
        }
        // Set CAN ID: DLC 2 only, [0]=high, [1]=low (same layout as Ping, e.g. 01 90 = 0x190)
        else if (frame.DLC >= 2 && CAN_ID(frame) == WB_MSG_SET_INDEX)
        {
            configuration = GetConfiguration();
            uint16_t baseId = (frame.data8[0] << 8) | frame.data8[1];
            if (baseId > 0x7FF) baseId = 0x7FF;
            for (int i = 0; i < AFR_CHANNELS; i++) {
                configuration->afr[i].RusEfiBaseId = baseId + i * 2;
            }
            for (int i = 0; i < EGT_CHANNELS; i++) {
                configuration->egt[i].RusEfiIdOffset = (baseId - WB_DATA_BASE_ADDR + i) & 0xFF;
            }
            SetConfiguration();
            SendAck();
        }
        // Heater config: HeaterSupplyOffV (0.1V), HeaterSupplyOnV (0.1V), PreheatTimeSec
        else if (frame.DLC >= 3 && CAN_ID(frame) == WB_MSG_SET_HEATER_CONFIG)
        {
            configuration = GetConfiguration();
            configuration->heaterConfig.HeaterSupplyOffVoltage = frame.data8[0];
            configuration->heaterConfig.HeaterSupplyOnVoltage = frame.data8[1];
            configuration->heaterConfig.PreheatTimeSec = frame.data8[2];
            SetConfiguration();
            SendAck();
        }
    }
}

HeaterAllow GetHeaterAllowed()
{
    return heaterAllow;
}

float GetRemoteBatteryVoltage()
{
    return remoteBatteryVoltage;
}

void InitCan()
{
    configuration = GetConfiguration();

    canStart(&CAND1, &GetCanConfig());
    chThdCreateStatic(waCanTxThread, sizeof(waCanTxThread), NORMALPRIO, CanTxThread, nullptr);
    chThdCreateStatic(waCanRxThread, sizeof(waCanRxThread), NORMALPRIO - 4, CanRxThread, nullptr);
}

static int LambdaIsValid(int ch)
{
    const auto& sampler = GetSampler(ch);
    const auto& heater = GetHeaterController(ch);

    float nernstDc = sampler.GetNernstDc();
    float pumpDuty = GetPumpOutputDuty(ch);
    float lambda = GetLambda(ch);

    // Lambda is valid if:
    // 1. Heater is in closed loop
    // 2. Nernst voltage is near target
    // 3. Pump duty isn't saturated (sensor reading unreliable at limits)
    // 4. Lambda is >0.6 (sensor isn't specified below that)
    return (heater.IsRunningClosedLoop() &&
            nernstDc > (NERNST_TARGET - 0.1f) &&
            nernstDc < (NERNST_TARGET + 0.1f) &&
            pumpDuty > 0.1f && pumpDuty < 0.9f &&
            lambda > 0.6f);
}

void SendRusefiFormat(uint8_t ch)
{
    auto baseAddress = configuration->afr[ch].RusEfiBaseId;

    const auto& sampler = GetSampler(ch);

    float nernstDc = sampler.GetNernstDc();

    if (configuration->afr[ch].RusEfiTx) {
        CanTxTyped<wbo::StandardData> frame(baseAddress + 0);

        // The same header is imported by the ECU and checked against this data in the frame
        frame.get().Version = RUSEFI_WIDEBAND_VERSION;

        uint16_t lambdaInt = LambdaIsValid(ch) ? (uint16_t)(GetLambda(ch) * 10000) : 0;
        frame.get().Lambda = lambdaInt;
        frame.get().TemperatureC = sampler.GetSensorTemperature();
        frame.get().Valid = LambdaIsValid(ch) ? 0x01 : 0x00;
    }

    if (configuration->afr[ch].RusEfiTxDiag) {
        auto esr = sampler.GetSensorInternalResistance();

        CanTxTyped<wbo::DiagData> frame(baseAddress + 1);

        frame.get().Esr = esr;
        frame.get().NernstDc = nernstDc * 1000;
        frame.get().PumpDuty = GetPumpOutputDuty(ch) * 255;
        frame.get().Status = GetCurrentFault(ch);
        frame.get().HeaterDuty = GetHeaterDuty(ch) * 255;
    }
}

// Weak link so boards can override it
__attribute__((weak)) void SendCanForChannel(uint8_t ch)
{
    SendRusefiFormat(ch);
    SendAemNetUEGOFormat(ch);
}

__attribute__((weak)) void SendCanEgtForChannel(uint8_t ch)
{
#if (EGT_CHANNELS > 0)
    // TODO: implement RusEFI protocol?
    SendAemNetEGTFormat(ch);
#endif /* EGT_CHANNELS > 0 */
}