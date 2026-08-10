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
    voices.reset();
}

void SamplerModule::blockStart()
{
    latchedSample = getLoadedSample (&latchedSourceRate);
}

void SamplerModule::handleMidiEvent (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        bool retrigger = false;
        const int v = voices.noteOn (m.getNoteNumber(), retrigger);
        position[v] = 0.0;
        gateLvl[v] = 0.0f;
        releaseElapsed[v] = 0.0;
        rate[v] = midiNoteToHz (m.getNoteNumber()) / rootHz;
    }
    else if (m.isNoteOff())
    {
        voices.noteOff (m.getNoteNumber());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        voices.allGatesOff();
    }
}

void SamplerModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    outputs[0][0] = 0.0f;
    outputs[0][1] = 0.0f;

    if (latchedSample == nullptr || latchedSample->getNumSamples() == 0)
        return;

    const float fmIn = inputs[0][0];                       // modulates playback rate
    const bool envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;
    const float volume = param (pVolume);

    const int total = latchedSample->getNumSamples();
    const int numCh = latchedSample->getNumChannels();
    const float* dataL = latchedSample->getReadPointer (0);
    const float* dataR = latchedSample->getReadPointer (numCh > 1 ? 1 : 0);

    const double srRatio = latchedSourceRate / sampleRate;
    const double invSr = 1.0 / sampleRate;
    const float gateStep = (float) (invSr / gateRampSeconds);

    float sumL = 0.0f, sumR = 0.0f;

    for (int v = 0; v < VoiceAllocator::maxVoices; ++v)
    {
        auto& slot = voices.slots[v];
        if (! slot.inUse)
            continue;

        // one-shot: voice ends at the end of the sample
        if (position[v] >= total - 1)
        {
            voices.freeVoice (v);
            continue;
        }

        const int i0 = (int) position[v];
        const float frac = (float) (position[v] - i0);
        const float l = dataL[i0] + (dataL[i0 + 1] - dataL[i0]) * frac;
        const float r = dataR[i0] + (dataR[i0 + 1] - dataR[i0]) * frac;

        position[v] += rate[v] * srRatio * juce::jmax (0.0, 1.0 + (double) fmIn);

        float gain;
        if (envConnected)
        {
            gain = 1.0f;
            if (! slot.gate)
            {
                releaseElapsed[v] += invSr;
                if (releaseElapsed[v] > envTailSeconds)
                {
                    voices.freeVoice (v);
                    continue;
                }
            }
        }
        else
        {
            const float target = slot.gate ? 1.0f : 0.0f;
            if (gateLvl[v] < target)      gateLvl[v] = juce::jmin (target, gateLvl[v] + gateStep);
            else if (gateLvl[v] > target) gateLvl[v] = juce::jmax (target, gateLvl[v] - gateStep);

            if (! slot.gate && gateLvl[v] <= 0.0f)
            {
                voices.freeVoice (v);
                continue;
            }
            gain = gateLvl[v];
        }

        sumL += l * gain;
        sumR += r * gain;
    }

    outputs[0][0] = sumL * volume * envIn;
    outputs[0][1] = sumR * volume * envIn;
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
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SamplerModule, samplerDescriptor)
