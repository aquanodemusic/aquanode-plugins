#include "GranulateModule.h"

using namespace aquanode;

//==============================================================================
GranulateModule::GranulateModule()
{
    // std::atomic's default constructor doesn't zero-initialise pre-C++20,
    // so every slot needs an explicit "inactive" (-1) starting value.
    for (auto& voiceGrains : grainDisplayPos)
        for (auto& p : voiceGrains)
            p.store (-1.0f, std::memory_order_relaxed);
}

//==============================================================================
// read-only waveform display (same pattern as the Sampler's), plus a live
// playhead marker per active grain so Position/Spray/etc. changes are
// visually confirmable, not just audible.
class GranulateWaveformDisplay : public juce::Component,
                                 private juce::Timer
{
public:
    explicit GranulateWaveformDisplay (SynthModule& moduleToShow)
        : module (&moduleToShow)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);   // fast enough for the grain markers to read as motion
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

        // one thin marker per currently active grain, at its live read position
        g.setColour (juce::Colours::yellow.withAlpha (0.85f));
        for (int i = 0; i < numDisplayPositions; ++i)
        {
            const float x = displayPositions[i] * (float) w;
            g.drawVerticalLine ((int) x, 1.0f, (float) h - 1.0f);
        }
    }

private:
    void timerCallback() override
    {
        // safe downcast: this display is only ever built by GranulateModule
        auto* m = static_cast<GranulateModule*> (module.get());
        if (m == nullptr)
            return;

        const int counter = m->getSampleChangeCounter();
        const bool sampleChanged = counter != lastCounter;
        lastCounter = counter;

        const int newCount = m->getActiveGrainPositions (displayPositions, maxDisplayMarkers);
        const bool activityChanged = newCount > 0 || numDisplayPositions > 0;
        numDisplayPositions = newCount;

        if (sampleChanged || activityChanged)
            repaint();
    }

    static constexpr int maxDisplayMarkers = 128;

    juce::WeakReference<SynthModule> module;
    int lastCounter { -1 };
    float displayPositions [maxDisplayMarkers] {};
    int numDisplayPositions { 0 };
};

std::unique_ptr<juce::Component> GranulateModule::createExtraContentComponent()
{
    return std::make_unique<GranulateWaveformDisplay> (*this);
}

//==============================================================================
void GranulateModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void GranulateModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void GranulateModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    for (auto& g : grains[v])
        g = Grain();
    for (auto& p : grainDisplayPos[v])
        p.store (-1.0f, std::memory_order_relaxed);
    spawnCounter[v] = 0.0;
    noteRate[v] = 0.0;
    gateLvl[v] = 0.0f;
    gateOn[v] = false;
    vel[v] = 1.0f;
}

void GranulateModule::blockStart()
{
    latchedSample = getLoadedSample (&latchedSourceRate);
}

void GranulateModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    juce::ignoreUnused (retrigger);
    glide.noteOn (v, (float) note, isMonoVoice());
    gateOn[v] = true;
    spawnCounter[v] = 0.0;   // spawn the first grain immediately
}

void GranulateModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gateOn[v] = false;
}

void GranulateModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
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

void GranulateModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    outputs[0][0] = 0.0f;
    outputs[0][1] = 0.0f;

    // mono glide slides the grain playback rate between notes (no-op when polyphonic)
    noteRate[v] = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                     ! pool.isMuted (v), sampleRate)) / rootHz;

    if (latchedSample == nullptr || latchedSample->getNumSamples() < 2)
        return;

    const bool envConnected = isInputConnected (0);
    const float envIn = envConnected ? inputs[0][0] : 1.0f;

    const int total = latchedSample->getNumSamples();
    const int numCh = latchedSample->getNumChannels();
    const float* dataL = latchedSample->getReadPointer (0);
    const float* dataR = latchedSample->getReadPointer (numCh > 1 ? 1 : 0);

    const int nGrains = juce::jlimit (1, maxGrains, (int) param (pGrains));
    const int grainLen = juce::jmax (8, (int) (param (pSize) * 0.001 * sampleRate));

    // ---- grain spawning: nGrains overlapping across one grain length -------
    const double invSr = 1.0 / sampleRate;
    const bool voiceAlive = gateOn[v] || envConnected;   // env-driven voices keep granulating through the release

    spawnCounter[v] -= 1.0;
    if (spawnCounter[v] <= 0.0 && voiceAlive)
    {
        spawnCounter[v] += juce::jmax (1.0, (double) grainLen / (double) nGrains);

        for (auto& g : grains[v])
        {
            if (g.active)
                continue;

            const float sprayAmt = param (pSpray) * 0.01f;
            const float posFrac = juce::jlimit (0.0f, 1.0f,
                param (pPosition) * 0.01f + (random.nextFloat() * 2.0f - 1.0f) * sprayAmt * 0.5f);

            g.active = true;
            g.pos = posFrac * (total - 2);
            g.age = 0;
            g.length = grainLen;

            // pitch dispersion: up to +-12 semitones of per-grain random detune
            const float dispSemis = (random.nextFloat() * 2.0f - 1.0f)
                                    * param (pPitchDisp) * 0.01f * 12.0f;
            g.inc = noteRate[v] * (latchedSourceRate * invSr)
                    * std::pow (2.0, dispSemis / 12.0);

            // stereo spread: random per-grain pan
            const float pan = (random.nextFloat() * 2.0f - 1.0f) * param (pStereo) * 0.01f;
            g.gainL = pan > 0.0f ? 1.0f - pan : 1.0f;
            g.gainR = pan < 0.0f ? 1.0f + pan : 1.0f;
            break;
        }
    }

    // ---- render active grains ----------------------------------------------
    float sumL = 0.0f, sumR = 0.0f;

    for (int gi = 0; gi < maxGrains; ++gi)
    {
        auto& g = grains[v][gi];

        if (! g.active)
        {
            grainDisplayPos[v][gi].store (-1.0f, std::memory_order_relaxed);
            continue;
        }

        if (g.age >= g.length || g.pos >= total - 1 || g.pos < 0.0)
        {
            g = Grain();
            grainDisplayPos[v][gi].store (-1.0f, std::memory_order_relaxed);
            continue;
        }

        // Hann window over the grain's life
        const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                             * (float) g.age / (float) g.length);

        const int i0 = (int) g.pos;
        const float frac = (float) (g.pos - i0);
        const float l = dataL[i0] + (dataL[i0 + 1] - dataL[i0]) * frac;
        const float r = dataR[i0] + (dataR[i0 + 1] - dataR[i0]) * frac;

        sumL += l * window * g.gainL;
        sumR += r * window * g.gainR;

        grainDisplayPos[v][gi].store ((float) (g.pos / (double) total), std::memory_order_relaxed);

        g.pos += g.inc;
        ++g.age;
    }

    // overlapping grains stack; normalise so the grain count sets texture, not level
    const float norm = 1.0f / std::sqrt ((float) nGrains);

    float gain;
    if (envConnected)
    {
        gain = envIn;
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
    outputs[0][0] = sumL * norm * gain * volume;
    outputs[0][1] = sumR * norm * gain * volume;
}

int GranulateModule::getActiveGrainPositions (float* out, int maxOut) const
{
    int count = 0;
    for (int v = 0; v < aquanode::kMaxVoices && count < maxOut; ++v)
        for (int gi = 0; gi < maxGrains && count < maxOut; ++gi)
        {
            const float p = grainDisplayPos[v][gi].load (std::memory_order_relaxed);
            if (p >= 0.0f)
                out[count++] = p;
        }
    return count;
}

//==============================================================================
static ModuleDescriptor granulateDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.granulate";
    d.displayName = "Granulator";
    d.description =
        "Granular sampler: each note runs its own cloud of Hann-windowed grains, pitched relative "
        "to C4. Env In wants an ADSR, that is where note-level shaping comes from, exactly as on "
        "the Oscillator.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 2;
    d.sockets = {
        modIn ("envIn", "Env In"),
        midiIn ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume",    "Volume",     0.0f, 1.0f, 0.8f, 0),
        makeRotary ("grains",    "Grains",     1.0f, 32.0f, 8.0f, 0, {}, false, 1.0f),
        makeRotary ("size",      "Size",       5.0f, 500.0f, 80.0f, 0, "ms", true),
        makeRotary ("position",  "Position",   0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("spray",     "Spray",      0.0f, 100.0f, 10.0f, 0, "%"),
        makeRotary ("pitchDisp", "Pitch Disp", 0.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("stereo",    "Stereo",     0.0f, 100.0f, 30.0f, 1, "%"),
        makeButton ("loadSample", "Load Sample", 1, 2)
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (GranulateModule, granulateDescriptor)