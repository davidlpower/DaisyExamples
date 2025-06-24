#include "daisysp.h"
#include "daisy_pod.h"
#include <cstdio>

#define MIDI_CC_DRYWET     10
#define MIDI_CC_FEEDBACK   11
#define MIDI_CC_DELAY_MS   12
#define MIDI_CC_MODE_CHANGE 13

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

float DSY_SDRAM_BSS loop_buf_l[MAX_LOOP];
float DSY_SDRAM_BSS loop_buf_r[MAX_LOOP];

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

enum LoopState { LOOP_IDLE, LOOP_RECORDING, LOOP_PLAYING, LOOP_OVERDUBBING };
LoopState loop_state = LOOP_IDLE;
size_t loop_write_pos = 0;
size_t loop_read_pos = 0;
size_t loop_length = 0;

void UpdateKnobs(float &k1, float &k2)
{
    k2 = pod.knob2.Process();

    switch(mode)
    {
        case REV:
            k1 = pod.knob1.Process();
            knob_drywet = k1;
            knob_feedback = k2;
            break;
        case DEL:
            knob_delay_ms = (1.0f - pod.knob1.Process()) * (MAX_DELAY / pod.AudioSampleRate()) * 1000.0f;
            knob_feedback = k2;
            break;
        case LOOP:
            break;
    }
    last_knob_update = System::GetNow();
}

void Controls()
{
    float k1 = 0.0f, k2 = 0.0f;
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    if(pod.button1.RisingEdge())
        mode = (mode + 1) % 3;

    UpdateKnobs(k1, k2);

    bool midi_valid = (last_midi_update != 0);
    bool midi_newer = midi_valid && (last_midi_update > last_knob_update);

    switch(mode)
    {
        case REV:
            drywet = midi_newer ? midi_drywet : knob_drywet;
            rev.SetFeedback(midi_newer ? midi_feedback : knob_feedback);
            break;

        case DEL:
            feedback = midi_newer ? midi_feedback : knob_feedback;
            delayTarget = pod.AudioSampleRate() * 
                ((midi_newer ? midi_delay_ms : knob_delay_ms) / 1000.0f);

            if(currentDelay < 0.0f || currentDelay > MAX_DELAY)
                currentDelay = delayTarget;
            break;

        case LOOP:
            break;
    }

    pod.UpdateLeds();
}

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    float outl, outr, inl, inr;

    Controls();

    for(size_t i = 0; i < size; i += 2)
    {
        inl = in[i];
        inr = in[i + 1];

        switch(mode)
        {
            case REV:  rev.Process(inl, inr, &outl, &outr); break;
            case DEL:
                fonepole(currentDelay, delayTarget, .00007f);
                delr.SetDelay(currentDelay);
                dell.SetDelay(currentDelay);
                outl = dell.Read();
                outr = delr.Read();

                dell.Write((feedback * outl) + inl);
                outl = (feedback * outl) + ((1.0f - feedback) * inl);

                delr.Write((feedback * outr) + inr);
                outr = (feedback * outr) + ((1.0f - feedback) * inr);
                break;
            default:
                outl = outr = 0.0f;
                break;
        }

        out[i]     = drywet * outl + (1 - drywet) * inl;
        out[i + 1] = drywet * outr + (1 - drywet) * inr;
    }
}

void HandleMidiMessage(MidiEvent m)
{
    if(m.type == ControlChange)
    {
        ControlChangeEvent p = m.AsControlChange();
        switch(p.control_number)
        {
            case MIDI_CC_DRYWET:     midi_drywet = p.value / 127.0f; break;
            case MIDI_CC_FEEDBACK:   midi_feedback = p.value / 127.0f; break;
            case MIDI_CC_DELAY_MS:   midi_delay_ms = 10.0f + ((p.value / 127.0f) * 2490.0f); break;
        }
        last_midi_update = System::GetNow();
    }
}

int main(void)
{
    pod.Init();
    pod.SetAudioBlockSize(4);

    rev.Init(pod.AudioSampleRate());
    dell.Init();
    delr.Init();

    midi_drywet = knob_drywet = 0.5f;
    midi_feedback = knob_feedback = 0.5f;
    midi_delay_ms = knob_delay_ms = 750.0f;

    delayTarget = currentDelay = pod.AudioSampleRate() * (midi_delay_ms / 1000.0f);

    dell.SetDelay(currentDelay);
    delr.SetDelay(currentDelay);

    pod.StartAdc();
    pod.StartAudio(AudioCallback);
    pod.midi.StartReceive();

    while(1)
    {
        pod.midi.Listen();
        while(pod.midi.HasEvents())
            HandleMidiMessage(pod.midi.PopEvent());
    }
}
