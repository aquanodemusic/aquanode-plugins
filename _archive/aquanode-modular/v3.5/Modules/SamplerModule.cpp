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

        // dim everything outside the Start..End window, so the two bars have
        // something to aim at instead of being guessed against the waveform
        const float a = juce::jlimit (0.0f, 1.0f, m->getParameter ("start"));
        const float b = juce::jlimit (0.0f, 1.0f, m->getParameter ("end"));
        const float x0 = juce::jmin (a, b) * (float) w;
        const float x1 = juce::jmax (a, b) * (float) w;

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (0.0f, 0.0f, x0, (float) h);
        g.fillRect (x1, 0.0f, (float) w - x1, (float) h);

        g.setColour (juce::Colours::aqua.withAlpha (0.85f));
        g.drawVerticalLine ((int) x0, 0.0f, (float) h);
        g.drawVerticalLine (juce::jmin (w - 1, (int) x1), 0.0f, (float) h);
    }

private:
    void timerCallback() override
    {
        auto* m = module.get();
        if (m == nullptr)
            return;

        // repaint on a new sample OR on a moved window marker
        const int counter = m->getSampleChangeCounter();
        const float a = m->getParameter ("start"), b = m->getParameter ("end");
        if (counter != lastCounter || a != lastStart || b != lastEnd)
        {
            lastCounter = counter;
            lastStart = a;
            lastEnd = b;
            repaint();
        }
    }

    juce::WeakReference<SynthModule> module;
    int lastCounter { -1 };
    float lastStart { -1.0f }, lastEnd { -1.0f };
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
    glide.resetVoice (v);
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

void SamplerModule::windowFor (int& startIdx, int& endIdx, int total) const
{
    const float a = juce::jlimit (0.0f, 1.0f, param (pStart));
    const float b = juce::jlimit (0.0f, 1.0f, param (pEnd));

    startIdx = (int) (juce::jmin (a, b) * (float) (total - 1));
    endIdx   = (int) (juce::jmax (a, b) * (float) (total - 1));

    // Start above End is allowed on the sliders (and is a natural thing to do
    // while dragging); it just reads as the same span the other way round
    // rather than producing silence or a negative-length loop.
    startIdx = juce::jlimit (0, total - 2, startIdx);
    endIdx   = juce::jlimit (startIdx + 1, total - 1, endIdx);
}

void SamplerModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    juce::ignoreUnused (retrigger);
    // start from the window, not from 0
    if (auto sample = getLoadedSample())
    {
        int a = 0, b = 0;
        if (sample->getNumSamples() >= 2)
        {
            windowFor (a, b, sample->getNumSamples());
            position[v] = (double) a;
        }
        else
            position[v] = 0.0;
    }
    else
        position[v] = 0.0;
    gateLvl[v] = 0.0f;
    gateOn[v] = true;
    glide.noteOn (v, (float) note, isMonoVoice());
}

void SamplerModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gateOn[v] = false;
}

void SamplerModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // voice-steal de-click: a muted voice ramps out over a few ms instead of
    // being cut dead, and a re-used voice ramps back in
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void SamplerModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    outputs[0][0] = 0.0f;
    outputs[0][1] = 0.0f;

    // mono glide slides the playback rate between notes (no-op when polyphonic)
    rate[v] = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate)) / rootHz;

    if (latchedSample == nullptr || latchedSample->getNumSamples() < 2)
        return;

    const int total = latchedSample->getNumSamples();

    int startIdx = 0, endIdx = total - 1;
    windowFor (startIdx, endIdx, total);

    const bool cycling = param (pCycle) > 0.5f;

    if (position[v] < (double) startIdx)
        position[v] = (double) startIdx;   // window moved under a playing voice

    if (position[v] >= (double) endIdx)
    {
        // Cycle re-triggers the window for as long as the note is held. Once
        // the key is released the loop is allowed to finish, so the release
        // tail of an ADSR on Env In still gets something to fade out.
        if (! (cycling && gateOn[v]))
            return;                // one-shot finished for this voice

        const double span = (double) (endIdx - startIdx);
        if (span < 1.0)
            return;
        position[v] = (double) startIdx + std::fmod (position[v] - (double) startIdx, span);
    }

    const float fmIn = inputs[0][0];                       // modulates playback rate
    const bool envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;

    const int numCh = latchedSample->getNumChannels();
    const float* dataL = latchedSample->getReadPointer (0);
    const float* dataR = latchedSample->getReadPointer (numCh > 1 ? 1 : 0);

    const int i0 = juce::jlimit (0, total - 2, (int) position[v]);
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
    d.description =
        "Keyboard-mapped sample playback, root fixed at C4, with each voice playing its own copy. "
        "Start and End bound the section that plays, as a fraction of the file. With Cycle off the "
        "window plays once per note; with Cycle on it repeats for as long as the note is held, and "
        "is allowed to finish after release so an ADSR on Env In still has something to fade. "
        "FM In modulates the playback rate; Env In wants an ADSR.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 1;
    d.sockets = {
        modIn ("fmIn",  "FM In"),
        modIn ("envIn", "Env In"),
        midiIn ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.8f, 0),
        makeButton ("loadSample", "Load Sample", 0, 2)
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f)
    ,
        makeRotary ("start", "Start", 0.0f, 1.0f, 0.0f, 1),
        makeRotary ("end",   "End",   0.0f, 1.0f, 1.0f, 1),
        makeCombo  ("cycle", "Cycle", { "Cycle: Off", "Cycle: On" }, 0, 3, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SamplerModule, samplerDescriptor)
