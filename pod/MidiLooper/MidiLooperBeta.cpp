#include "daisysp.h"
#include "daisy_pod.h"
#include "daisy_core.h"
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
float playback_speed = 1.0f;

float drywet, feedback, delayTarget;
float currentDelay;
float knob_drywet = 0.5f;
float knob_feedback = 0.5f;
float knob_delay_ms = 750.0f;
float knob_crossfade = 0.0f;
float midi_drywet = 0.5f;
float midi_feedback = 0.5f;
float midi_delay_ms = 750.0f;
float midi_crossfade = 0.0f;

uint32_t last_knob_update = 0;
uint32_t last_midi_update = 0;
uint32_t encoder_last_moved = 0;
uint32_t last_blink_time = 0;
bool led_blink_state = false;
const uint32_t mode_set_timeout = 2000;
bool btn1_held = false, btn2_held = false;

int mode = REV;

struct LoopEngine {
    float* buffer_l;
    float* buffer_r;
    size_t write_pos = 0;
    float read_pos = 0.0f;
    size_t length = 0;
    float gain_out = 1.0f;
    bool write_enable = true;
    enum State { IDLE, RECORDING, PLAYING, OVERDUBBING } state = IDLE;

    void Reset() {
        write_pos = 0;
        read_pos = 0.0f;
        length = 0;
        state = IDLE;
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

    void SetLength(size_t len) {
        length = len;
    }

    void SetGain(float g) {
        gain_out = g;
    }

    void SetWriteEnable(bool enable) {
        write_enable = enable;
    }

    State GetState() const {
        return state;
    }

    size_t GetLength() const {
        return length;
    }

    void GetSample(float& outl, float& outr) {
        if(length == 0 || state == IDLE || state == RECORDING) {
            outl = outr = 0.0f;
        } else {
            size_t idx1 = static_cast<size_t>(read_pos) % length;
            size_t idx2 = (idx1 + 1) % length;
            float frac = read_pos - static_cast<float>(idx1);

            outl = (buffer_l[idx1] * (1.0f - frac) + buffer_l[idx2] * frac) * gain_out;
            outr = (buffer_r[idx1] * (1.0f - frac) + buffer_r[idx2] * frac) * gain_out;

            read_pos += playback_speed;
            if(read_pos >= length)
                read_pos -= length;
        }
    }

    void Write(float inl, float inr) {
        if(!write_enable)
            return;

        if(state == RECORDING && write_pos < MAX_LOOP) {
            buffer_l[write_pos] = inl;
            buffer_r[write_pos] = inr;
            ++write_pos;
        } else if(state == OVERDUBBING && length > 0) {
            size_t rpos = static_cast<size_t>(read_pos) % length;
            float mixed_l = 0.5f * (buffer_l[rpos] + inl);
            float mixed_r = 0.5f * (buffer_r[rpos] + inr);
            buffer_l[rpos] = mixed_l;
            buffer_r[rpos] = mixed_r;
        }
    }
};

LoopEngine loopA, loopB;

void UpdateLeds(float k1, float k2) {
    uint32_t now = System::GetNow();

    if (mode == LOOP && (btn1_held || btn2_held)) {
        float blink_interval = 500.0f;
        if (btn1_held) {
            blink_interval = 30.0f + 120.0f * (1.0f - (loopA.length / (float)MAX_LOOP));
        } else if (btn2_held) {
            blink_interval = 30.0f + 120.0f * (1.0f - (loopB.length / (float)MAX_LOOP));
        }

        if (now - last_blink_time >= static_cast<uint32_t>(blink_interval)) {
            led_blink_state = !led_blink_state;
            last_blink_time = now;
        }

        pod.led1.Set(led_blink_state && btn1_held ? 1.0f : 0.0f,
                     led_blink_state && btn1_held ? 1.0f : 0.0f,
                     led_blink_state && btn1_held ? 1.0f : 0.0f);

        pod.led2.Set(led_blink_state && btn2_held ? 1.0f : 0.0f,
                     led_blink_state && btn2_held ? 1.0f : 0.0f,
                     led_blink_state && btn2_held ? 1.0f : 0.0f);
    } else {
        switch(mode) {
            case REV:
                pod.led1.Set(0, 0, k1);
                pod.led2.Set(0, 0, k2);
                break;
            case DEL:
                pod.led1.Set(0, k1, 0);
                pod.led2.Set(0, k2, 0);
                break;
            case LOOP:
                switch(loopA.state) {
                    case LoopEngine::IDLE:        pod.led1.Set(0.2f, 0.2f, 0.2f); break;
                    case LoopEngine::RECORDING:   pod.led1.Set(1.0f, 0.0f, 0.0f); break;
                    case LoopEngine::PLAYING:     pod.led1.Set(0.0f, 1.0f, 0.0f); break;
                    case LoopEngine::OVERDUBBING: pod.led1.Set(0.8f, 0.4f, 0.0f); break;
                }
                switch(loopB.state) {
                    case LoopEngine::IDLE:        pod.led2.Set(0.2f, 0.2f, 0.2f); break;
                    case LoopEngine::RECORDING:   pod.led2.Set(1.0f, 0.0f, 0.0f); break;
                    case LoopEngine::PLAYING:     pod.led2.Set(0.0f, 1.0f, 0.0f); break;
                    case LoopEngine::OVERDUBBING: pod.led2.Set(0.8f, 0.4f, 0.0f); break;
                }
                break;
        }
    }
    pod.UpdateLeds();
}

void Controls() {
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    int encoder_increment = pod.encoder.Increment();
    if (encoder_increment != 0) {
        mode = (mode + encoder_increment + 3) % 3;
        encoder_last_moved = System::GetNow();
    }

    btn1_held = pod.button1.Pressed();
    btn2_held = pod.button2.Pressed();

    float k1 = pod.knob1.Process();
    float k2 = pod.knob2.Process();

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
            if (pod.button1.RisingEdge()) {
                if(loopA.state == LoopEngine::IDLE)
                    loopA.StartRecording();
                else if(loopA.state == LoopEngine::RECORDING)
                    loopA.StopRecording();
                else
                    loopA.ToggleOverdub();
            }
            if (pod.button2.RisingEdge()) {
                if(loopB.state == LoopEngine::IDLE)
                    loopB.StartRecording();
                else if(loopB.state == LoopEngine::RECORDING)
                    loopB.StopRecording();
                else
                    loopB.ToggleOverdub();
            }

            if (btn1_held && loopA.state == LoopEngine::IDLE) {
    loopA.length = static_cast<size_t>(k1 * MAX_LOOP);
} else if (btn2_held && loopB.state == LoopEngine::IDLE) {
    loopB.length = static_cast<size_t>(k2 * MAX_LOOP);
} else {
                playback_speed = 0.5f + k1;
                knob_crossfade = k2;
            }
            break;
    }

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

    UpdateLeds(k1, k2);
    last_knob_update = System::GetNow();
    last_knob_update = System::GetNow();
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
            case LOOP:
            {
                float la, ra, lb, rb;
                loopA.GetSample(la, ra);
                loopB.GetSample(lb, rb);
                outl = (1.0f - crossfade) * la + crossfade * lb;
                outr = (1.0f - crossfade) * ra + crossfade * rb;
                loopA.Write(inl, inr);
                loopB.Write(inl, inr);
                break;
            }
            default: outl = outr = 0; break;
        }

        out[i]     = outl;
        out[i + 1] = outr;
    }
}

int main(void) {
    pod.Init();
    pod.SetAudioBlockSize(4);
    float sample_rate = pod.AudioSampleRate();

    rev.Init(sample_rate);
    dell.Init();
    delr.Init();
    rev.SetLpFreq(18000.0f);
    rev.SetFeedback(0.85f);
    currentDelay = delayTarget = sample_rate * 0.75f;

    dell.SetDelay(currentDelay);
    delr.SetDelay(currentDelay);

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

    loopA.buffer_l = loop_bufA_l;
    loopA.buffer_r = loop_bufA_r;
    loopB.buffer_l = loop_bufB_l;
    loopB.buffer_r = loop_bufB_r;

    pod.StartAdc();
    pod.StartAudio(AudioCallback);
    pod.midi.StartReceive();

    pod.led1.Set(1.0f, 1.0f, 1.0f);
    pod.led2.Set(1.0f, 1.0f, 1.0f);
    pod.UpdateLeds();
    System::Delay(500);
    pod.led1.Set(0, 0, 0);
    pod.led2.Set(0, 0, 0);
    pod.UpdateLeds();

    while(1) {
        pod.midi.Listen();
        while(pod.midi.HasEvents())
            HandleMidiMessage(pod.midi.PopEvent());
    }
}
