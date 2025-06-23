#include "daisysp.h"
#include "daisy_pod.h"
#include <cstdio>

#define MIDI_CC_DRYWET     10
#define MIDI_CC_FEEDBACK   11
#define MIDI_CC_DELAY_MS   12
#define MIDI_CC_MODE_CHANGE 13

#define MAX_DELAY static_cast<size_t>(48000 * 2.5f)
#define REV 0
#define DEL 1

using namespace daisysp;
using namespace daisy;

static DaisyPod pod;

static ReverbSc rev;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS dell;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delr;
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

void UpdateKnobs(float &k1, float &k2)
{
    k1 = pod.knob1.Process();
    k2 = pod.knob2.Process();

    switch(mode)
    {
        case REV:
            knob_drywet = k1;
            knob_feedback = k2;
            break;
        case DEL:
            knob_delay_ms = deltime.Process() / pod.AudioSampleRate() * 1000.0f;
            knob_feedback = k2;
            break;
    }
    last_knob_update = System::GetNow();
}

void UpdateEncoder()
{
    mode += pod.encoder.Increment();
    mode = (mode % 2 + 2) % 2;
}

void UpdateLeds(float k1, float k2)
{
    if(mode == REV)
    {
        pod.led1.Set(0.0f, 0.0f, k1); // Blue
        pod.led2.Set(0.0f, 0.0f, k2); // Blue
    }
    else if(mode == DEL)
    {
        pod.led1.Set(0.0f, k1, 0.0f); // Green
        pod.led2.Set(0.0f, k2, 0.0f); // Green
    }
    pod.UpdateLeds();
}

void Controls()
{
    float k1 = 0.0f, k2 = 0.0f;
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    UpdateKnobs(k1, k2);
    UpdateEncoder();

    bool midi_newer = last_midi_update > last_knob_update;

    switch(mode)
    {
        case REV:
            drywet = midi_newer ? midi_drywet : knob_drywet;
            rev.SetFeedback(midi_newer ? midi_feedback : knob_feedback);
            break;
        case DEL:
            feedback = midi_newer ? midi_feedback : knob_feedback;
            delayTarget = pod.AudioSampleRate() * ((midi_newer ? midi_delay_ms : knob_delay_ms) / 1000.0f);
            break;
    }
    UpdateLeds(midi_newer ? midi_drywet : knob_drywet, midi_newer ? midi_feedback : knob_feedback);
}

void GetReverbSample(float &outl, float &outr, float inl, float inr)
{
    rev.Process(inl, inr, &outl, &outr);
    outl = drywet * outl + (1 - drywet) * inl;
    outr = drywet * outr + (1 - drywet) * inr;
}

void GetDelaySample(float &outl, float &outr, float inl, float inr)
{
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
            case REV: GetReverbSample(outl, outr, inl, inr); break;
            case DEL: GetDelaySample(outl, outr, inl, inr); break;
            default: outl = outr = 0;
        }

        out[i] = outl;
        out[i + 1] = outr;
    }
}

void HandleMidiMessage(MidiEvent m)
{
    switch(m.type)
    {
        case ControlChange:
        {
            ControlChangeEvent p = m.AsControlChange();
            switch(p.control_number)
            {
                case MIDI_CC_DRYWET: midi_drywet = p.value / 127.0f; break;
                case MIDI_CC_FEEDBACK: midi_feedback = p.value / 127.0f; break;
                case MIDI_CC_DELAY_MS: midi_delay_ms = 10.0f + ((p.value / 127.0f) * 2490.0f); break;
                case MIDI_CC_MODE_CHANGE:
                {
                    int delta = 0;
                    if(p.value < 63) delta = -1;
                    else if(p.value > 64) delta = 1;
                    if(delta != 0)
                    {
                        mode += delta;
                        mode = (mode % 2 + 2) % 2;
                    }
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

int main(void)
{
    float sample_rate;
    pod.Init();
    pod.SetAudioBlockSize(4);
    sample_rate = pod.AudioSampleRate();

    rev.Init(sample_rate);
    dell.Init();
    delr.Init();

    deltime.Init(pod.knob1, sample_rate * .05f, MAX_DELAY, deltime.LOGARITHMIC);
    rev.SetLpFreq(18000.0f);
    rev.SetFeedback(0.85f);

    currentDelay = delayTarget = sample_rate * 0.75f;
    dell.SetDelay(currentDelay);
    delr.SetDelay(currentDelay);

    pod.StartAdc();
    pod.StartAudio(AudioCallback);
    pod.midi.StartReceive();

    while(1)
    {
        pod.midi.Listen();
        while(pod.midi.HasEvents())
        {
            HandleMidiMessage(pod.midi.PopEvent());
        }
    }
}
