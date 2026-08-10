#include "SamplerModule.h"

using namespace aquanode;

//==============================================================================
// read-only waveform display, embedded by the generic module renderer
class SamplerWaveformDisplay : public juce::Component,
                               private juce::Timer
{
public:
    explicit SamplerWaveformDisplay (SynthModule& moduleToShow)
        : module (&moduleToShow)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (5);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        auto* m = module.get();
        if (m == nullptr)
            return;

        auto sample = m->getLoadedSample();
        if (sample == nullptr || sample->getNumSamples() == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("no sample loaded", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int w = juce::jmax (1, getWidth());
        const int h = getHeight();
        const float midY = h * 0.5f;
        const int total = sample->getNumSamples();
        const float* data = sample->getReadPointer (0);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (int x = 0; x < w; ++x)
        {
            const int start = (int) ((juce::int64) x * total / w);
            const int end   = juce::jmax (start + 1, (int) ((juce::int64) (x + 1) * total / w));
            float mn = 0.0f, mx = 0.0f;
            for (int i = start; i < end && i < total; ++i)
            {
                mn = juce::jmin (mn, data[i]);
                mx = juce::jmax (mx, data[i]);
            }
            g.drawVerticalLine (x, midY - mx * (midY - 2.0f), midY - mn * (midY - 2.0f));
        }
    }

private:
    void timerCallback() override
    {
        auto* m = module.get();
        const int counter = m != nullptr ? m->getSampleChangeCounter() : -1;
        if (counter != lastCounter)
        {
            lastCounter = counter;
            repaint();
        }
    }

    juce::WeakReference<SynthModule> module;
    int lastCounter { -1 };
};

std::unique_ptr<juce::Component> SamplerModule::createExtraContentComponent()
{
    return std::make_unique<SamplerWaveformDisplay> (*this);
}

//==============================================================================
void SamplerModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void SamplerModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void SamplerModule::voiceReset (int v)
{
    pool.resetVoice (v);
    position[v] = 0.0;
    rate[v] = 0.0;
    gateLvl[v] = 0.0f;
    gateOn[v] = false;
    vel[v] = 1.0f;
}

void SamplerModule::blockStart()
{
    latchedSample = getLoadedSample (&latchedSourceRate);
}

void SamplerModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    juce::ignoreUnused (retrigger);
    position[v] = 0.0;             // one-shot restarts on every note-on
    gateLvl[v] = 0.0f;
    gateOn[v] = true;
    rate[v] = midiNoteToHz (note) / rootHz;
}

void SamplerModule::voiceNoteOff (int v)
{
    gateOn[v] = false;
}

void SamplerModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    if (pool.isMuted (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }
    outputs[0][0] = 0.0f;
    outputs[0][1] = 0.0f;

    if (latchedSample == nullptr || latchedSample->getNumSamples() < 2)
        return;

    const int total = latchedSample->getNumSamples();
    if (position[v] >= total - 1)
        return;                    // one-shot finished for this voice

    const float fmIn = inputs[0][0];                       // modulates playback rate
    const bool envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;

    const int numCh = latchedSample->getNumChannels();
    const float* dataL = latchedSample->getReadPointer (0);
    const float* dataR = latchedSample->getReadPointer (numCh > 1 ? 1 : 0);

    const int i0 = (int) position[v];
    const float frac = (float) (position[v] - i0);
    const float l = dataL[i0] + (dataL[i0 + 1] - dataL[i0]) * frac;
    const float r = dataR[i0] + (dataR[i0 + 1] - dataR[i0]) * frac;

    const double invSr = 1.0 / sampleRate;
    position[v] += rate[v] * (latchedSourceRate * invSr) * juce::jmax (0.0, 1.0 + (double) fmIn);

    float gain;
    if (envConnected)
    {
        gain = envIn;              // this voice's own envelope
    }
    else
    {
        const float gateStep = (float) (invSr / gateRampSeconds);
        const float target = gateOn[v] ? 1.0f : 0.0f;
        if (gateLvl[v] < target)      gateLvl[v] = juce::jmin (target, gateLvl[v] + gateStep);
        else if (gateLvl[v] > target) gateLvl[v] = juce::jmax (target, gateLvl[v] - gateStep);
        gain = gateLvl[v];
    }

    const float volume = param (pVolume) * vel[v];   // velocity always applies
    outputs[0][0] = l * gain * volume;
    outputs[0][1] = r * gain * volume;
}

//==============================================================================
static ModuleDescriptor samplerDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.sampler";
    d.displayName = "Sampler";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 1;
    d.sockets = {
        modIn ("fmIn",  "FM In"),
        modIn ("envIn", "Env In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.8f, 0),
        makeButton ("loadSample", "Load Sample", 0, 2)
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SamplerModule, samplerDescriptor)
