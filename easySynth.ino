/*
 * Copyright (c) 2022 Marcel Licence
 * Modified by Jules for 2-Op FM Synthesis
 */

#ifdef __CDT_PARSER__
#include "cdt.h"
#endif

/* requires the ML_SynthTools library: https://github.com/marcel-licence/ML_SynthTools */
#include <ml_filter.h>
#include <ml_waveform.h>

#define CHANNEL_MAX 16

/*
 * Param indices for Synth_SetParam function
 */
#define SYNTH_PARAM_FM_RATIO    20
#define SYNTH_PARAM_FM_INDEX    21
#define SYNTH_PARAM_FM_ATTACK   22
#define SYNTH_PARAM_FM_DECAY    23
#define SYNTH_PARAM_FM_SUSTAIN  24
#define SYNTH_PARAM_FM_RELEASE  25

#define MAX_POLY_VOICE  8  /* Reduced polyphony for FM (2 ops per voice) */

#define MIDI_NOTE_CNT 128
static uint32_t midi_note_to_add[MIDI_NOTE_CNT]; /* lookup to playback waveforms with correct frequency */

/* Sine lookup table for FM */
#define WAVEFORM_CNT 1024
#define WAVEFORM_I(x) (((x) >> (32 - 10)) & (WAVEFORM_CNT - 1))
float *sine = NULL;

struct adsrT
{
    float a;
    float d;
    float s;
    float r;
};

typedef enum
{
    attack, decay, sustain, release
} adsr_phaseT;

inline bool ADSR_Process(const struct adsrT *ctrl, float *ctrlSig, adsr_phaseT *phase);

struct channelSetting_s
{
    // FM Parameters
    float fmRatio;
    float fmIndex;

    struct adsrT adsr_vol;

    /* modulation */
    float modulationDepth;
    float modulationSpeed;
    float modulationPitch;

    /* pitchbend */
    float pitchBendValue;
    float pitchMultiplier;

    /* mono mode variables */
    bool mono;

    float portAdd;
    float port;
    float noteA;
    float noteB;

    uint32_t noteCnt;
    // uint32_t noteStack[NOTE_STACK_MAX]; // Simplified, removing stack for now
};

static struct channelSetting_s chCfg[CHANNEL_MAX];
static struct channelSetting_s *curChCfg = &chCfg[0]; // Default to channel 0

struct notePlayerT
{
    float lastSample[2];

    float velocity;
    bool active;
    adsr_phaseT phase;

    uint8_t midiCh;
    uint8_t midiNote;

    float control_sign;

    // FM State
    uint32_t carrierPhase;
    uint32_t modulatorPhase;
    uint32_t carrierAdd;
    uint32_t modulatorAdd;

    struct channelSetting_s *cfg;
};

struct notePlayerT voicePlayer[MAX_POLY_VOICE];
uint32_t voc_act = 0;

void Synth_Init()
{
#ifdef ESP32
    randomSeed(34547379);
#endif

    sine = (float *)malloc(sizeof(float) * WAVEFORM_CNT);

    /* Generate Sine Table */
    for (int i = 0; i < WAVEFORM_CNT; i++)
    {
        sine[i] = (float)sin(i * 2.0 * PI / WAVEFORM_CNT);
    }

    /*
     * initialize all voices
     */
    for (int i = 0; i < MAX_POLY_VOICE; i++)
    {
        notePlayerT *voice = &voicePlayer[i];
        voice->active = false;
        voice->lastSample[0] = 0.0f;
        voice->lastSample[1] = 0.0f;
        voice->cfg = &chCfg[0];
    }

    /*
     * prepare lookup for constants to drive oscillators
     */
    for (int i = 0; i < MIDI_NOTE_CNT; i++)
    {
        float f = ((pow(2.0f, (float)(i - 69) / 12.0f) * 440.0f));
        uint32_t add = (uint32_t)(f * ((float)(1ULL << 32ULL) / ((float)SAMPLE_RATE)));
        midi_note_to_add[i] = add;
    }

    for (int i = 0; i < CHANNEL_MAX; i++)
    {
        Synth_ChannelSettingInit(&chCfg[i]);
    }
}


static void Synth_ChannelSettingInit(struct channelSetting_s *setting)
{
    setting->fmRatio = 1.0f;
    setting->fmIndex = 0.0f;

    struct adsrT adsr_vol_def = {0.01f, 0.005f, 0.8f, 0.005f}; // Faster default
    memcpy(&setting->adsr_vol, &adsr_vol_def, sizeof(adsr_vol_def));

    setting->modulationDepth = 0.0f;
    setting->modulationSpeed = 5.0f;
    setting->modulationPitch = 1.0f;

    setting->pitchBendValue = 0.0f;
    setting->pitchMultiplier = 1.0f;

    setting->mono = false;
    setting->portAdd = 0.01f;
    setting->port = 1.0f;
    setting->noteA = 0;
    setting->noteB = 0;
    setting->noteCnt = 0;
}

inline bool ADSR_Process(const struct adsrT *ctrl, float *ctrlSig, adsr_phaseT *phase)
{
    switch (*phase)
    {
    case attack:
        *ctrlSig += ctrl->a;
        if (*ctrlSig > 1.0f)
        {
            *ctrlSig = 1.0f;
            *phase = decay;
        }
        break;
    case decay:
        *ctrlSig -= ctrl->d;
        if (*ctrlSig < ctrl->s)
        {
            *ctrlSig = ctrl->s;
            *phase = sustain;
        }
        break;
    case sustain:
        break;
    case release:
        *ctrlSig -= ctrl->r;
        if (*ctrlSig < 0.0f)
        {
            *ctrlSig = 0.0f;
            return false;
        }
    }
    return true;
}

static uint32_t count = 0;

//[[gnu::noinline, gnu::optimize ("fast-math")]]
inline void Synth_Process(float *left, float *right, uint32_t len)
{
    /* update pitch bending */
    for (int i = 0; i < CHANNEL_MAX; i++)
    {
        float modulation = 0; // GetModulation(i); // Simplified
        float pitchVar = chCfg[i].pitchBendValue + modulation;
        chCfg[i].pitchMultiplier = pow(2.0f, pitchVar / 12.0f);
    }

    for (uint32_t n = 0; n < len; n++)
    {
        count += 1;

        /*
         * voice processing
         */
        for (int i = 0; i < MAX_POLY_VOICE; i++)
        {
            notePlayerT *voice = &voicePlayer[i];
            if (voice->active)
            {
                if (n % 4 == 0) // Sub-sample ADSR
                {
                    voice->active = ADSR_Process(&voice->cfg->adsr_vol, &voice->control_sign, &voice->phase);
                    if (voice->active == false)
                    {
                        voc_act -= 1;
                    }
                }

                // FM Synthesis: 2-Op
                // Modulator -> Carrier

                // Advance Phases
                float pitchMult = voice->cfg->pitchMultiplier;
                // Ratio applies to Modulator frequency
                uint32_t modStep = (uint32_t)(voice->modulatorAdd * pitchMult * voice->cfg->fmRatio);
                voice->modulatorPhase += modStep;

                uint32_t carrierStep = (uint32_t)(voice->carrierAdd * pitchMult);
                voice->carrierPhase += carrierStep;

                // Modulator Output
                float modOut = sine[WAVEFORM_I(voice->modulatorPhase)];

                // Modulation Index (Amount)
                // Scale Index by Envelope? Usually good for musicality but basic FM relies on Index.
                // We'll scale Index by Velocity or Envelope if desired, but user asked for mapping to Index.
                float currentIndex = voice->cfg->fmIndex * 5.0f; // Scale 0-1 to 0-5

                // Carrier Frequency Modulation
                // Phase Modulation: Carrier(Phase + ModOut * Index)
                uint32_t modOffset = (uint32_t)(modOut * currentIndex * (float)(1ULL << (32 - 10))); // Scale to phase width

                // Carrier Output
                float carrierOut = sine[WAVEFORM_I(voice->carrierPhase + modOffset)];

                // Apply Amp Envelope
                carrierOut *= voice->control_sign * voice->velocity;

                voice->lastSample[0] = carrierOut;

                left[n] += voice->lastSample[0];
                right[n] += voice->lastSample[0]; // Mono to stereo buffer
            }
        }
    }

    // Scaling
    for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++)
    {
        left[i] *= 0.5f;
        right[i] *= 0.5f;
    }
}


static struct notePlayerT *getFreeVoice(void)
{
    // Simple round-robin or first-free
    for (int i = 0; i < MAX_POLY_VOICE ; i++)
    {
        if (voicePlayer[i].active == false)
        {
            return &voicePlayer[i];
        }
    }
    // Steal voice 0 if full? For now just drop note.
    return NULL;
}

inline void Synth_NoteOn(uint8_t ch, uint8_t note, float vel)
{
    struct notePlayerT *voice = getFreeVoice();

    if (voice == NULL) return;

    voice->cfg = &chCfg[ch];
    voice->midiCh = ch;
    voice->midiNote = note;
    voice->velocity = vel;
    voice->control_sign = 0.0f;
    voice->active = true;
    voice->phase = attack;

    // Reset phases for consistency
    voice->carrierPhase = 0;
    voice->modulatorPhase = 0;

    // Set Base Frequencies
    voice->carrierAdd = midi_note_to_add[note];
    voice->modulatorAdd = midi_note_to_add[note]; // Ratio applied in Process

    voc_act += 1;

    // Start Envelope immediately
    ADSR_Process(&voice->cfg->adsr_vol, &voice->control_sign, &voice->phase);
}

inline void Synth_NoteOff(uint8_t ch, uint8_t note)
{
    for (int i = 0; i < MAX_POLY_VOICE ; i++)
    {
        if ((voicePlayer[i].active) && (voicePlayer[i].midiNote == note) && (voicePlayer[i].midiCh == ch))
        {
            voicePlayer[i].phase = release;
        }
    }
}

void Synth_ModulationWheel(uint8_t ch, float value)
{
    chCfg[ch].modulationDepth = value;
}

void Synth_PitchBend(uint8_t ch, float bend)
{
    chCfg[ch].pitchBendValue = bend;
}

void Synth_SetCurCh(uint8_t ch, float value)
{
    if (value > 0 && ch < CHANNEL_MAX)
    {
        curChCfg = &chCfg[ch];
    }
}

void Synth_ToggleMono(uint8_t ch __attribute__((unused)), float value)
{
    if (value > 0)
    {
        curChCfg->mono = !curChCfg->mono;
    }
}

void Synth_SetParam(uint8_t paramId, float value)
{
    switch (paramId)
    {
    case SYNTH_PARAM_FM_RATIO:
        // Map 0-1 to 0.5 - 10.0 or similar
        // Or discrete ratios: 1, 2, 3...
        // Let's use continuous for now: 0.5 to 15.5
        curChCfg->fmRatio = 0.5f + (value * 15.0f);
        break;

    case SYNTH_PARAM_FM_INDEX:
        curChCfg->fmIndex = value;
        break;

    case SYNTH_PARAM_FM_ATTACK:
        curChCfg->adsr_vol.a = (0.00005 * pow(5000, 1.0f - value));
        break;
    case SYNTH_PARAM_FM_DECAY:
        curChCfg->adsr_vol.d = (0.00005 * pow(5000, 1.0f - value));
        break;
    case SYNTH_PARAM_FM_SUSTAIN:
        curChCfg->adsr_vol.s = value;
        break;
    case SYNTH_PARAM_FM_RELEASE:
        curChCfg->adsr_vol.r = (0.0001 * pow(100, 1.0f - value));
        break;

    default:
        break;
    }
}
