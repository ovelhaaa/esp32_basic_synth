/*
 * Copyright (c) 2023 Marcel Licence
 * Modified by Jules for Headless FM Synth with Moog Filter
 */

#ifdef __CDT_PARSER__
#include <cdt.h>
#endif


#include "config.h"

#include <Arduino.h>

#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>

/* requires the ml_Synth library */
#include <ml_arp.h>
#include <ml_reverb.h>
#include <ml_midi_ctrl.h>
#include <ml_delay.h>
#ifdef OLED_OSC_DISP_ENABLED
#include <ml_scope.h>
#endif

#include <ml_types.h>

#define ML_SYNTH_INLINE_DECLARATION
#include <ml_inline.h>
#undef ML_SYNTH_INLINE_DECLARATION

/* Moog Filter */
#include "MicrotrackerModel.h"

// Instantiate the filter
// Using MicrotrackerMoog for efficiency on Lolin32 Lite
MicrotrackerMoog moogFilter(SAMPLE_RATE);

// Global filter parameters
float filterCutoffHz = 20000.0f;
float filterResonance = 0.0f;

/* FM Parameters to be defined in easySynth.ino / config headers */
// We will use Synth_SetParam with these custom IDs
#define SYNTH_PARAM_FM_RATIO    20
#define SYNTH_PARAM_FM_INDEX    21
#define SYNTH_PARAM_FM_ATTACK   22
#define SYNTH_PARAM_FM_DECAY    23
#define SYNTH_PARAM_FM_SUSTAIN  24
#define SYNTH_PARAM_FM_RELEASE  25

void setup()
{
    /*
     * this code runs once
     */
    delay(500);

    Serial.begin(SERIAL_BAUDRATE);

    Serial.println();

    Serial.printf("esp32_basic_synth  Copyright (c) 2023  Marcel Licence\n");
    Serial.printf("Modified for Headless FM Synth with Moog Filter\n");

    Serial.printf("Initialize Synth Module\n");
    Synth_Init();

#ifdef BLE_MIDI
    Serial.printf("Initialize MIDI over Bluetooth\n");
    BLE_setup();
#endif

    Serial.printf("Initialize I2S Module\n");

#ifdef BLINK_LED_PIN
    Blink_Setup();
#endif

    Audio_Setup();


    /*
     * Initialize reverb
     */
    static float *revBuffer = (float *)malloc(sizeof(float) * REV_BUFF_SIZE);
    Reverb_Setup(revBuffer);

    /*
     * Prepare a buffer which can be used for the delay
     */
#ifdef MAX_DELAY
    static int16_t *delBuffer1 = (int16_t *)malloc(sizeof(int16_t) * MAX_DELAY);
    static int16_t *delBuffer2 = (int16_t *)malloc(sizeof(int16_t) * MAX_DELAY);
    Delay_Init2(delBuffer1, delBuffer2, MAX_DELAY);
#endif

    /*
     * setup midi module / rx port
     */
    Midi_Setup();

#ifdef ESP32
    Serial.printf("ESP.getFreeHeap() %d\n", ESP.getFreeHeap());
    Serial.printf("ESP.getMinFreeHeap() %d\n", ESP.getMinFreeHeap());
    Serial.printf("ESP.getHeapSize() %d\n", ESP.getHeapSize());
    Serial.printf("ESP.getMaxAllocHeap() %d\n", ESP.getMaxAllocHeap());

    Serial.printf("Total heap: %d\n", ESP.getHeapSize());
    Serial.printf("Free heap: %d\n", ESP.getFreeHeap());

    /* PSRAM will be fully used by the looper */
    Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());

    /* Initialize Core 0 Task */
    Core0TaskInit();
#endif

    Serial.printf("Firmware started successfully\n");

#ifdef NOTE_ON_AFTER_SETUP
    Synth_NoteOn(0, 64, 1.0f);
#endif
}

#ifdef ESP32
/*
 * Core 0
 */
/* this is used to add a task to core 0 */
TaskHandle_t Core0TaskHnd;

inline
void Core0TaskInit()
{
    /* we need a second task for the terminal output */
    xTaskCreatePinnedToCore(Core0Task, "CoreTask0", 8000, NULL, 0, &Core0TaskHnd, 0);
}

inline
void Core0TaskSetup()
{
    /*
     * init your stuff for core0 here
     */
}

void Core0TaskLoop()
{
    /*
     * put your loop stuff for core0 here
     */

    // Process BLE MIDI on Core 0 to avoid interrupting Audio on Core 1
#ifdef BLE_MIDI
    BleMidiProc();
#endif

    /* Small delay to prevent watchdog trigger and yield to other tasks */
    delay(1);
}

void Core0Task(void *parameter)
{
    Core0TaskSetup();

    while (true)
    {
        Core0TaskLoop();
        yield();
    }
}
#endif /* ESP32 */

static uint32_t midiSyncCount = 0;

void Midi_SyncRecvd()
{
    midiSyncCount += 1;
}

void Synth_RealTimeMsg(uint8_t msg)
{
#ifndef MIDI_SYNC_MASTER
    switch (msg)
    {
    case 0xfa: /* start */
        // Arp_Reset(); // ARP disabled
        break;
    case 0xf8: /* Timing Clock */
        Midi_SyncRecvd();
        break;
    }
#endif
}

/*
 * use this if something should happen every second
 * - you can drive a blinking LED for example
 */
inline void Loop_1Hz(void)
{
#ifdef BLINK_LED_PIN
    Blink_Process();
#endif
}


/*
 * our main loop
 * - all is done in a blocking context
 * - do not block the loop otherwise you will get problems with your audio
 */
static float fl_sample[SAMPLE_BUFFER_SIZE];
static float fr_sample[SAMPLE_BUFFER_SIZE];

void loop()
{
    static uint32_t loop_cnt_1hz;

    loop_cnt_1hz += SAMPLE_BUFFER_SIZE;
    if (loop_cnt_1hz >= SAMPLE_RATE)
    {
        Loop_1Hz();
        loop_cnt_1hz = 0;
    }

    /*
     * Midi does not required to be checked after every processed sample
     * - we divide our operation by 8
     */
    Midi_Process();


    /* zero buffer, otherwise you can pass trough an input signal */
    memset(fl_sample, 0, sizeof(fl_sample));
    memset(fr_sample, 0, sizeof(fr_sample));

    Synth_Process(fl_sample, fr_sample, SAMPLE_BUFFER_SIZE);

    /*
     * Apply Moog Filter (Global)
     * Processing in Mono (using Left channel) and copying to Right since input is mono synth usually
     */

    // Apply Filter to Left Channel
    moogFilter.Process(fl_sample, SAMPLE_BUFFER_SIZE);

    // Copy to right channel (Dual Mono output)
    memcpy(fr_sample, fl_sample, sizeof(fr_sample));

    /*
     * process delay line
     */
#ifdef MAX_DELAY
    Delay_Process_Buff2(fl_sample, fr_sample, SAMPLE_BUFFER_SIZE);
#endif

    /*
     * add some mono reverb
     */
    Reverb_Process(fl_sample, SAMPLE_BUFFER_SIZE);

#ifdef ESP32
    memcpy(fr_sample, fl_sample, sizeof(fr_sample));

    /*
     * Output the audio
     */
    Audio_Output(fl_sample, fr_sample);
#else
    Audio_OutputMono(fl_sample);
#endif

}

/*
 * Callbacks
 */
void MidiCtrl_Cb_NoteOn(uint8_t ch, uint8_t note, float vel)
{
    Synth_NoteOn(ch, note, vel);
}

void MidiCtrl_Cb_NoteOff(uint8_t ch, uint8_t note)
{
    Synth_NoteOff(ch, note);
}

// Map CC to parameters
void MidiCtrl_Cb_ControlChange(uint8_t channel, uint8_t number, uint8_t value) {
    // Standard MIDI CC mapping for Synth parameters

    float valNormal = (float)value / 127.0f;

    switch (number) {
        case 74: // Cutoff Frequency (Brightness)
        {
            // Exponential mapping: 20Hz to 20kHz
            const float minFreq = 20.0f;
            const float maxFreq = 20000.0f;
            // Cutoff = Min * (Max/Min)^val
            float cutoff = minFreq * pow(maxFreq / minFreq, valNormal);
            moogFilter.SetCutoff(cutoff);
            break;
        }
        case 71: // Resonance (Harmonic Content)
        {
            // Map 0-127 to 0.0 - 1.1 (allowing self-oscillation > 1.0)
            float resonance = valNormal * 1.1f;
            moogFilter.SetResonance(resonance);
            break;
        }

        // FM Parameters
        case 16: // General Purpose 1 - Mapped to FM Ratio (Coarse)
             Synth_SetParam(SYNTH_PARAM_FM_RATIO, valNormal);
            break;

        case 17: // General Purpose 2 - Mapped to FM Index (Amount)
             Synth_SetParam(SYNTH_PARAM_FM_INDEX, valNormal);
            break;

        case 73: // Attack Time
             Synth_SetParam(SYNTH_PARAM_FM_ATTACK, valNormal);
            break;
        case 75: // Decay Time
             Synth_SetParam(SYNTH_PARAM_FM_DECAY, valNormal);
            break;
        case 79: // Sustain Level
             Synth_SetParam(SYNTH_PARAM_FM_SUSTAIN, valNormal);
            break;
        case 72: // Release Time
             Synth_SetParam(SYNTH_PARAM_FM_RELEASE, valNormal);
            break;

        default:
            // Pass other CCs to the engine if it handles them
            break;
    }
}


void MidiCtrl_Status_ValueChangedIntArr(const char *descr, int value, int index)
{
    Status_ValueChangedIntArr(descr, value, index);
}

void Arp_Cb_NoteOn(uint8_t ch, uint8_t note, float vel)
{
    // ARP disabled, direct pass through
    Synth_NoteOn(ch, note, vel);
}

void Arp_Cb_NoteOff(uint8_t ch, uint8_t note)
{
    Synth_NoteOff(ch, note);
}

void Arp_Status_ValueChangedInt(const char *msg, int value)
{
    Status_ValueChangedInt(msg, value);
}

void Arp_Status_LogMessage(const char *msg)
{
    Status_LogMessage(msg);
}

void Arp_Status_ValueChangedFloat(const char *msg, float value)
{
    Status_ValueChangedFloat(msg, value);
}

void Arp_Cb_Step(uint8_t step __attribute__((unused)))
{
    /* ignore */
}

/*
 * Test functions
 */
#if defined(I2C_SCL) && defined (I2C_SDA)
void ScanI2C(void)
{
    Wire.begin(I2C_SDA, I2C_SCL);
}
#endif
