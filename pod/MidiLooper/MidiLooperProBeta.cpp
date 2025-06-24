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

void UpdateLeds(float k1, float k2) {
    switch(mode) {
        case REV:  pod.led1.Set(0, 0, k1); pod.led2.Set(0, 0, k2); break;
        case DEL:  pod.led1.Set(0, k1, 0); pod.led2.Set(0, k2, 0); break;
        case LOOP:
            switch(focused_loop->state) {
                case LoopEngine::IDLE:        pod.led1.Set(0.2f, 0.2f, 0.2f); break;
                case LoopEngine::RECORDING:   pod.led1.Set(1.0f, 0.0f, 0.0f); break;
                case LoopEngine::PLAYING:     pod.led1.Set(0.0f, 1.0f, 0.0f); break;
                case LoopEngine::OVERDUBBING: pod.led1.Set(0.8f, 0.4f, 0.0f); break;
            }
            pod.led2.Set(0, 0, 0);
            break;
    }
    pod.UpdateLeds();
}

void Controls() {
    float k1 = 0.0f, k2 = 0.0f;
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    if(pod.button1.RisingEdge()) {
        if(mode == LOOP)
            focused_loop = (focused_loop == &loopA) ? &loopB : &loopA;
        else
            mode = (mode + 1) % 3;
    }

    if(mode == LOOP && pod.button2.RisingEdge()) {
        switch(focused_loop->state) {
            case LoopEngine::IDLE:        focused_loop->StartRecording(); break;
            case LoopEngine::RECORDING:   focused_loop->StopRecording(); break;
            case LoopEngine::PLAYING:
            case LoopEngine::OVERDUBBING: focused_loop->ToggleOverdub(); break;
        }
    }

    UpdateKnobs(k1, k2);

    bool midi_newer = last_midi_update > last_knob_update;

    switch(mode) {
        case REV:
            drywet = midi_newer ? midi_drywet : knob_drywet;
            rev.SetFeedback(midi_newer ? midi_feedback : knob_feedback);
            break;
        case DEL:
            feedback = midi_newer ? midi_feedback : knob_feedback;
            delayTarget = pod.AudioSampleRate() * ((midi_newer ? midi_delay_ms : knob_delay_ms) / 1000.0f);
            break;
        case LOOP:
            crossfade = midi_newer ? midi_crossfade : knob_crossfade;
            break;
    }

    UpdateLeds(midi_newer ? knob_drywet : knob_crossfade, midi_newer ? knob_feedback : knob_crossfade);
}

void GetReverbSample(float &outl, float &outr, float inl, float inr) {
    rev.Process(inl, inr, &outl, &outr);
    outl = drywet * outl + (1 - drywet) * inl;
    outr = drywet * outr + (1 - drywet) * inr;
}

void GetDelaySample(float &outl, float &outr, float inl, float inr) {
    fonepole(currentDelay, delayTarget, .00007f);
    delr.SetDelay(currentDelay);
    dell.SetDelay(currentDelay);

    outl = dell.Read();
    outr = delr.Read();

    dell.Write((feedback * outl) + inl);
    outl = (feedback * outl) + ((1.0f - feedback) * inl);

    delr.Write((feedback * outr) + inr);
    outr = (feedback * outr) + ((1.0f - feedback) * inr);
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

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size) {
    float outl, outr, inl, inr;
    Controls();

    for(size_t i = 0; i < size; i += 2) {
        inl = in[i];
        inr = in[i + 1];

        switch(mode) {
            case REV:  GetReverbSample(outl, outr, inl, inr); break;
            case DEL:  GetDelaySample(outl, outr, inl, inr); break;
            case LOOP: GetLoopSample(outl, outr, inl, inr); break;
            default:   outl = outr = 0; break;
        }

        out[i]     = outl;
        out[i + 1] = outr;
    }
}

int main(void) {
    float sample_rate;
    pod.Init();
    pod.SetAudioBlockSize(4);
    sample_rate = pod.AudioSampleRate();

    rev.Init(sample_rate);
    dell.Init();
    delr.Init();

    for(size_t i = 0; i < MAX_DELAY; ++i) {
        dell.Write(0.0f);
        delr.Write(0.0f);
    }
    for(size_t i = 0; i < MAX_LOOP; ++i) {
        loop_bufA_l[i] = 0.0f;
        loop_bufA_r[i] = 0.0f;
        loop_bufB_l[i] = 0.0f;
        loop_bufB_r[i] = 0.0f;
    }

    deltime.Init(pod.knob1, MAX_DELAY, sample_rate * .05f, deltime.LOGARITHMIC);
    rev.SetLpFreq(18000.0f);
    rev.SetFeedback(0.85f);

    currentDelay = delayTarget = sample_rate * 0.75f;
    dell.SetDelay(currentDelay);
    delr.SetDelay(currentDelay);

    pod.StartAdc();
    pod.StartAudio(AudioCallback);
    pod.midi.StartReceive();

    while(1) {
        pod.midi.Listen();
        while(pod.midi.HasEvents())
            HandleMidiMessage(pod.midi.PopEvent());
    }
}
