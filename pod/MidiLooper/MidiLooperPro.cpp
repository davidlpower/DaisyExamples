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
static DelayLine<float, MAX_LOOP> DSY_SDRAM_BSS loopl;
static DelayLine<float, MAX_LOOP> DSY_SDRAM_BSS loopr;

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
            knob_delay_ms = deltime.Process() / pod.AudioSampleRate() * 1000.0f;
            knob_feedback = k2;
            break;
        case LOOP:
            break;
    }
    last_knob_update = System::GetNow();
}

void UpdateEncoder() {}

void UpdateLeds(float k1, float k2)
{
    switch(mode)
    {
        case REV:  pod.led1.Set(0, 0, k1); pod.led2.Set(0, 0, k2); break;
        case DEL:  pod.led1.Set(0, k1, 0); pod.led2.Set(0, k2, 0); break;
        case LOOP:
            switch(loop_state)
            {
                case LOOP_IDLE:        pod.led1.Set(0.2f, 0.2f, 0.2f); break;
                case LOOP_RECORDING:   pod.led1.Set(1.0f, 0.0f, 0.0f); break;
                case LOOP_PLAYING:     pod.led1.Set(0.0f, 1.0f, 0.0f); break;
                case LOOP_OVERDUBBING: pod.led1.Set(0.8f, 0.4f, 0.0f); break;
            }
            pod.led2.Set(0, 0, 0);
            break;
    }
    pod.UpdateLeds();
}

void Controls()
{
    float k1 = 0.0f, k2 = 0.0f;
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    if(pod.button1.RisingEdge())
        mode = (mode + 1) % 3;

    if(mode == LOOP && pod.button2.RisingEdge())
    {
        switch(loop_state)
        {
            case LOOP_IDLE:        loop_state = LOOP_RECORDING; loop_write_pos = 0; break;
            case LOOP_RECORDING:   loop_state = LOOP_PLAYING; loop_length = loop_write_pos; loop_write_pos = 0; loopl.SetRead(0); loopr.SetRead(0); break;
            case LOOP_PLAYING:     loop_state = LOOP_OVERDUBBING; break;
            case LOOP_OVERDUBBING: loop_state = LOOP_PLAYING; break;
        }
    }

    UpdateKnobs(k1, k2);

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
        case LOOP:
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

void GetLoopSample(float &outl, float &outr, float inl, float inr)
{
    switch(loop_state)
    {
        case LOOP_RECORDING:
            loopl.Write(inl);
            loopr.Write(inr);
            loopl.Increment();
            loopr.Increment();
            loop_write_pos++;
            if(loop_write_pos >= MAX_LOOP)
            {
                loop_length = MAX_LOOP;
                loop_state = LOOP_PLAYING;
                loop_write_pos = 0;
                loopl.SetRead(0);
                loopr.SetRead(0);
            }
            outl = inl;
            outr = inr;
            break;

        case LOOP_PLAYING:
            outl = loopl.Read();
            outr = loopr.Read();
            loopl.Increment();
            loopr.Increment();
            loop_write_pos++;
            if(loop_write_pos >= loop_length)
            {
                loop_write_pos = 0;
                loopl.SetRead(0);
                loopr.SetRead(0);
            }
            break;

        case LOOP_OVERDUBBING:
            outl = loopl.Read();
            outr = loopr.Read();
            loopl.Write(0.5f * (inl + outl));
            loopr.Write(0.5f * (inr + outr));
            loopl.Increment();
            loopr.Increment();
            loop_write_pos++;
            if(loop_write_pos >= loop_length)
            {
                loop_write_pos = 0;
                loopl.SetRead(0);
                loopr.SetRead(0);
            }
            break;

        default:
            outl = inl;
            outr = inr;
            break;
    }
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
            case REV:  GetReverbSample(outl, outr, inl, inr); break;
            case DEL:  GetDelaySample(outl, outr, inl, inr); break;
            case LOOP: GetLoopSample(outl, outr, inl, inr); break;
            default:   outl = outr = 0; break;
        }

        out[i]     = outl;
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
                case MIDI_CC_DRYWET:     midi_drywet = p.value / 127.0f; break;
                case MIDI_CC_FEEDBACK:   midi_feedback = p.value / 127.0f; break;
                case MIDI_CC_DELAY_MS:   midi_delay_ms = 10.0f + ((p.value / 127.0f) * 2490.0f); break;
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

int main(void)
{
    float sample_rate;
    pod.Init();
    pod.SetAudioBlockSize(4);
    sample_rate = pod.AudioSampleRate();

    rev.Init(sample_rate);
    dell.Init();
    delr.Init();
    loopl.Init();
    loopr.Init();

    deltime.Init(pod.knob1, sample_rate * .05f, MAX_DELAY, deltime.LOGARITHMIC);
    rev.SetLpFreq(18000.0f);
    rev.SetFeedback(0.85f);

    currentDelay = delayTarget = sample_rate * 0.75f;
    dell.SetDelay(currentDelay);
    delr.SetDelay(currentDelay);

    loopl.SetDelay(MAX_LOOP);
    loopr.SetDelay(MAX_LOOP);

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
