#include "daisysp.h"
#include "daisy_pod.h"
#include <cstdio>

#define MIDI_CC_DRYWET     10
#define MIDI_CC_FEEDBACK   11
#define MIDI_CC_DELAY_MS   12
#define MIDI_CC_MODE_CHANGE 13
#define MIDI_CC_CROSSFADE  14

#define MAX_DELAY static_cast<size_t>(48000 * 2.5f)
#define MAX_LOOP static_cast<size_t>(48000 * 10.0f)

#define REV 0
#define DEL 1
#define LOOP 2

using namespace daisysp;
using namespace daisy;

static DaisyPod pod;

static ReverbSc rev;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS dell;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delr;

float DSY_SDRAM_BSS loop_bufA_l[MAX_LOOP];
float DSY_SDRAM_BSS loop_bufA_r[MAX_LOOP];
float DSY_SDRAM_BSS loop_bufB_l[MAX_LOOP];
float DSY_SDRAM_BSS loop_bufB_r[MAX_LOOP];

float crossfade = 0.0f;
float knob_crossfade = 0.0f;
float midi_crossfade = 0.0f;

struct LoopEngine {
    float* buffer_l;
    float* buffer_r;
    size_t write_pos = 0;
    size_t read_pos = 0;
    size_t length = 0;
    bool write_enable = false;
    enum State { IDLE, RECORDING, PLAYING, OVERDUBBING } state = IDLE;

    void Reset() {
        write_pos = read_pos = length = 0;
        state = IDLE;
        write_enable = false;
    }

    void StartRecording() {
        write_pos = 0;
        state = RECORDING;
    }

    void StopRecording() {
        length = write_pos;
        read_pos = 0;
        state = PLAYING;
    }

    void ToggleOverdub() {
        if(state == PLAYING)
            state = OVERDUBBING;
        else if(state == OVERDUBBING)
            state = PLAYING;
    }

    void GetSample(float& outl, float& outr) {
        if(length == 0 || (state == IDLE || state == RECORDING)) {
            outl = outr = 0.0f;
        } else {
            outl = buffer_l[read_pos];
            outr = buffer_r[read_pos];
            read_pos = (read_pos + 1) % length;
        }
    }

    void Write(float inl, float inr) {
        if(state == RECORDING && write_pos < MAX_LOOP) {
            buffer_l[write_pos] = inl;
            buffer_r[write_pos] = inr;
            ++write_pos;
        } else if(state == OVERDUBBING && length > 0) {
            float mixed_l = 0.5f * (buffer_l[read_pos] + inl);
            float mixed_r = 0.5f * (buffer_r[read_pos] + inr);
            buffer_l[read_pos] = mixed_l;
            buffer_r[read_pos] = mixed_r;
        }
    }
};

LoopEngine loopA = { loop_bufA_l, loop_bufA_r };
LoopEngine loopB = { loop_bufB_l, loop_bufB_r };
LoopEngine* focused_loop = &loopA;

static Parameter deltime;
int mode = REV;

float drywet, feedback, delayTarget;
float currentDelay;

float knob_drywet = 0.5f;
float knob_feedback = 0.5f;
float knob_delay_ms = 750.0f;

float midi_drywet = 0.5f;
float midi_feedback = 0.5f;
float midi_delay_ms = 750.0f;

uint32_t last_knob_update = 0;
uint32_t last_midi_update = 0;

void UpdateKnobs(float &k1, float &k2) {
    k2 = pod.knob2.Process();
    k1 = pod.knob1.Process();

    switch(mode) {
        case REV:
            knob_drywet = k1;
            knob_feedback = k2;
            break;
        case DEL:
            knob_delay_ms = (1.0f - k1) * (MAX_DELAY / pod.AudioSampleRate()) * 1000.0f;
            knob_feedback = k2;
            break;
        case LOOP:
            knob_crossfade = k1;
            break;
    }
    last_knob_update = System::GetNow();
}

void GetLoopSample(float& outl, float& outr, float inl, float inr) {
    float la = 0.0f, ra = 0.0f, lb = 0.0f, rb = 0.0f;
    loopA.GetSample(la, ra);
    loopB.GetSample(lb, rb);

    float a_weight = 1.0f - crossfade;
    float b_weight = crossfade;
    outl = a_weight * la + b_weight * lb;
    outr = a_weight * ra + b_weight * rb;

    loopA.Write(inl, inr);
    loopB.Write(inl, inr);
}

void HandleMidiMessage(MidiEvent m) {
    switch(m.type) {
        case ControlChange:
        {
            ControlChangeEvent p = m.AsControlChange();
            switch(p.control_number) {
                case MIDI_CC_DRYWET:     midi_drywet = p.value / 127.0f; break;
                case MIDI_CC_FEEDBACK:   midi_feedback = p.value / 127.0f; break;
                case MIDI_CC_DELAY_MS:   midi_delay_ms = 10.0f + ((p.value / 127.0f) * 2490.0f); break;
                case MIDI_CC_CROSSFADE:  midi_crossfade = p.value / 127.0f; break;
                case MIDI_CC_MODE_CHANGE:
                {
                    int delta = (p.value < 63) ? -1 : (p.value > 64 ? 1 : 0);
                    if(delta != 0)
                        mode = (mode + delta + 3) % 3;
                    break;
                }
                default: break;
            }
            last_midi_update = System::GetNow();
            break;
        }
        default: break;
    }
}
