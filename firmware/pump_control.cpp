#include "pump_control.h"
#include "wideband_config.h"
#include "heater_control.h"
#include "sampling.h"
#include "pump_dac.h"
#include "pid.h"

#include "ch.h"

static float pumpGainAdjust = 1.0f;

void SetPumpGainAdjust(float ratio)
{
    pumpGainAdjust = ratio;
}

static float f_abs(float x)
{
    return x > 0 ? x : -x;
}

class SensorDetector
{
public:
    void feed(int pumpCh, const ISampler& sampler)
    {
        if (cycle < 25) {
            SetPumpCurrentTarget(pumpCh, 1000);
            nernstHi = sampler.GetNernstDc();
        } else {
            SetPumpCurrentTarget(pumpCh, -1000);
            nernstLo = sampler.GetNernstDc();
        }
        if (++cycle >= 50) {
            float amplitude = f_abs(nernstHi - nernstLo);
            if (amplitude > maxAmplitude) {
                maxAmplitude = amplitude;
            }
            cycle = 0;
            counter++;
        }
    }
    void reset()
    {
        cycle = counter = 0;
        nernstHi = nernstLo = 0.0f;
        maxAmplitude = 0.0f;
    }

private:
    int cycle = 0;
    int counter = 0;
    float nernstHi = 0.0f;
    float nernstLo = 0.0f;
    float maxAmplitude = 0.0f;
};

static SensorDetector sensorDetector[AFR_CHANNELS];

struct pump_control_state {
    Pid pumpPid;
};

static struct pump_control_state state[AFR_CHANNELS] =
{
    {
        Pid(50.0f, 10000.0f, 0.0f, 10.0f, 2),
    },
#if (AFR_CHANNELS > 1)
    {
        Pid(50.0f, 10000.0f, 0.0f, 10.0f, 2),
    }
#endif
};

static THD_WORKING_AREA(waPumpThread, 256);
static void PumpThread(void*)
{
    chRegSetThreadName("Pump");

    while(true)
    {
        for (int ch = 0; ch < AFR_CHANNELS; ch++)
        {
            pump_control_state &s = state[ch];

            const auto& sampler = GetSampler(ch);
            const auto& heater = GetHeaterController(ch);

            float sensorTemp = sampler.GetSensorTemperature();
            float targetTemp = heater.GetTargetTemp();

            // Only actuate pump when hot enough to not hurt the sensor
            if (heater.IsRunningClosedLoop() ||
                (sensorTemp >= targetTemp - START_PUMP_TEMP_OFFSET))
            {
                float nernstVoltage = sampler.GetNernstDc();

                float result = s.pumpPid.GetOutput(NERNST_TARGET, nernstVoltage);

                // result is in mA, scaled by ECU-adjustable gain
                SetPumpCurrentTarget(ch, (int32_t)(result * 1000 * pumpGainAdjust));
            }
#ifdef START_SENSOR_DETECTION_TEMP_OFFSET
            else if (sensorTemp >= targetTemp - START_SENSOR_DETECTION_TEMP_OFFSET)
            {
                sensorDetector[ch].feed(ch, sampler);
            }
#endif
            else
            {
                sensorDetector[ch].reset();
                SetPumpCurrentTarget(ch, 0);
            }
        }

        // Run at 500hz
        chThdSleepMilliseconds(2);
    }
}

void StartPumpControl()
{
    chThdCreateStatic(waPumpThread, sizeof(waPumpThread), NORMALPRIO + 4, PumpThread, nullptr);
}
