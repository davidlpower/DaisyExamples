#include <cstdio>

// MIDI CC Assignments
#define MIDI_CC_DRYWET     10  // Controls dry/wet mix for reverb
#define MIDI_CC_FEEDBACK   11  // Controls feedback amount for delay or reverb
#define MIDI_CC_DELAY_MS   12  // Controls delay time in milliseconds
#define MIDI_CC_MODE_CHANGE 13 // Controls effect mode: increment/decrement effect type

#define MAX_DELAY static_cast<size_t>(48000 * 2.5f)
#define REV 0
#define DEL 1

using namespace daisysp;
using namespace daisy;

static DaisyPod pod;

static ReverbSc rev;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS dell;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delr;
static Tone tone;
static Parameter deltime;

int mode = REV;

float drywet, feedback, delayTarget, cutoff;
float currentDelay;

// Knob values
float knob_drywet = 0.5f;
float knob_feedback = 0.5f;
float knob_delay_ms = 750.0f;

// MIDI values
float midi_drywet = 0.5f;
float midi_feedback = 0.5f;
float midi_delay_ms = 750.0f;

// Timestamps
uint32_t last_knob_update = 0;
uint32_t last_midi_update = 0;

void UpdateKnobs()
{
    float k1 = pod.knob1.Process();
    float k2 = pod.knob2.Process();

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
    mode = mode + pod.encoder.Increment();
    mode = (mode % 2 + 2) % 2;
}

void UpdateLeds(float k1, float k2)
{
    pod.led1.Set(k1 * (mode == 2), k1 * (mode == 1), k1 * (mode == 0 || mode == 2));
    pod.led2.Set(k2 * (mode == 2), k2 * (mode == 1), k2 * (mode == 0 || mode == 2));
    pod.UpdateLeds();
}

void Controls()
{
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    UpdateKnobs();

    bool midi_newer = last_midi_update > last_knob_update;
    if(!midi_newer)
        UpdateEncoder();

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

    UpdateLeds(drywet, feedback);
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
                case 10: midi_drywet = p.value / 127.0f; break;
                case 11: midi_feedback = p.value / 127.0f; break;
                case 12: midi_delay_ms = 10.0f + ((p.value / 127.0f) * 2490.0f); break;
                case 13:
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
