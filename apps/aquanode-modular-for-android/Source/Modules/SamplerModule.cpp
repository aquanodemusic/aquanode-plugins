#include "SamplerModule.h"

using namespace aquanode;

//==============================================================================
// The Sampler's waveform: zoom with the mouse wheel (around the pointer),
// slide along by dragging or with shift+wheel, drag the Start / End markers
// directly, double-click to see the whole file again. Long files draw from a
// peak cache built once per loaded sample.
class SamplerWaveformDisplay : public juce::Component,
                               private juce::Timer
{
public:
    explicit SamplerWaveformDisplay (SynthModule& moduleToShow)
        : module (&moduleToShow)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        auto* m = module.get();
        if (m == nullptr)
            return;

        if (sample == nullptr || sample->getNumSamples() == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("no sample loaded", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int w = juce::jmax (1, getWidth());
        const int h = getHeight() - kBarH;
        const float midY = h * 0.5f;
        const double total = (double) sample->getNumSamples();
        const float* data = sample->getReadPointer (0);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (int x = 0; x < w; ++x)
        {
            const auto s0 = (juce::int64) (viewStart + viewLen * x / w);
            const auto s1 = juce::jmax (s0 + 1, (juce::int64) (viewStart + viewLen * (x + 1) / w));
            float mn = 0.0f, mx = 0.0f;
            if (s1 - s0 >= 2 * kPeakBlock && ! peaks.empty())
            {
                for (auto b = s0 / kPeakBlock; b < juce::jmin ((juce::int64) peaks.size(), s1 / kPeakBlock + 1); ++b)
                {
                    mn = juce::jmin (mn, peaks[(size_t) b].first);
                    mx = juce::jmax (mx, peaks[(size_t) b].second);
                }
            }
            else
            {
                for (auto i = s0; i < s1 && i < (juce::int64) total; ++i)
                {
                    mn = juce::jmin (mn, data[i]);
                    mx = juce::jmax (mx, data[i]);
                }
            }
            g.drawVerticalLine (x, midY - mx * (midY - 2.0f), midY - mn * (midY - 2.0f) + 1.0f);
        }

        // dim everything outside the Start..End window
        const float x0 = (float) xOf (juce::jmin (startPos(), endPos()));
        const float x1 = (float) xOf (juce::jmax (startPos(), endPos()));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (0.0f, 0.0f, juce::jlimit (0.0f, (float) w, x0), (float) h);
        if (x1 < (float) w)
            g.fillRect (juce::jmax (0.0f, x1), 0.0f, (float) w - juce::jmax (0.0f, x1), (float) h);

        g.setColour (juce::Colours::aqua.withAlpha (0.9f));
        if (x0 >= 0.0f && x0 <= (float) w) g.fillRect (x0 - 1.0f, 0.0f, 2.0f, (float) h);
        if (x1 >= 0.0f && x1 <= (float) w) g.fillRect (juce::jmin ((float) w - 2.0f, x1 - 1.0f), 0.0f, 2.0f, (float) h);

        // what part of the file is on screen, and how long it is
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (0, h, w, kBarH);
        g.setColour (juce::Colours::aqua.withAlpha (0.55f));
        g.fillRect ((float) (viewStart / total * w), (float) h, juce::jmax (3.0f, (float) (viewLen / total * w)), (float) kBarH);

        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        const double sr = juce::jmax (1.0, rate);
        const auto label = timeText (viewStart / sr) + " - " + timeText ((viewStart + viewLen) / sr)
                         + (viewLen < total - 1.0 ? "" : "  (wheel / Zoom knob: zoom, drag: slide)");
        g.drawText (label, getLocalBounds().withHeight (h).reduced (4, 2), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = Drag::None;
        if (sample == nullptr)
            return;
        const double xs = xOf (startPos()), xe = xOf (endPos());
        if (std::abs (e.x - xs) <= kGrab)      dragging = Drag::Start;
        else if (std::abs (e.x - xe) <= kGrab) dragging = Drag::End;
        else { dragging = Drag::Pan; panFrom = viewStart; }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* m = module.get();
        if (m == nullptr || sample == nullptr)
            return;
        const double total = (double) sample->getNumSamples();
        if (dragging == Drag::Pan)
        {
            viewStart = panFrom - (double) e.getDistanceFromDragStartX() * viewLen / juce::jmax (1, getWidth());
            clampView();
            storeView();
            repaint();
        }
        else if (dragging != Drag::None)
        {
            const double pos = juce::jlimit (0.0, total - 1.0, sampleAt (e.x));
            m->setParameter (dragging == Drag::Start ? "start" : "end", (float) (pos / juce::jmax (1.0, total - 1.0)));
            repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (sample == nullptr) return;
        const bool nearMarker = std::abs (e.x - xOf (startPos())) <= kGrab || std::abs (e.x - xOf (endPos())) <= kGrab;
        setMouseCursor (nearMarker ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (sample == nullptr)
            return;
        const float dy = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        {
            viewStart -= (double) dy * viewLen * 0.5;          // slide along
        }
        else
        {
            const double anchor = sampleAt (e.x);              // zoom around the pointer
            const double factor = std::pow (0.5, (double) dy * 2.0);
            const double minLen = juce::jmin (64.0, (double) sample->getNumSamples());
            viewLen = juce::jlimit (minLen, (double) sample->getNumSamples(), viewLen * factor);
            viewStart = anchor - (double) e.x / juce::jmax (1, getWidth()) * viewLen;
        }
        clampView();
        storeView();
        repaint();
    }

    void mouseMagnify (const juce::MouseEvent& e, float scale) override
    {
        if (sample == nullptr || scale <= 0.0f)
            return;
        const double anchor = sampleAt (e.x);
        viewLen = juce::jlimit (juce::jmin (64.0, (double) sample->getNumSamples()),
                                (double) sample->getNumSamples(), viewLen / (double) scale);
        viewStart = anchor - (double) e.x / juce::jmax (1, getWidth()) * viewLen;
        clampView();
        storeView();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (sample == nullptr) return;
        viewStart = 0.0;
        viewLen = (double) sample->getNumSamples();
        storeView();
        repaint();
    }

private:
    // the view lives in the Zoom / View Pos knobs, so either moves it
    void storeView()
    {
        auto* m = module.get();
        if (m == nullptr || sample == nullptr) return;
        const double zoom = total() / juce::jmax (1.0, viewLen);
        const double room = total() - viewLen;
        lastZoom = (float) juce::jlimit (1.0, 1000.0, zoom);
        lastPos = room > 0.5 ? (float) juce::jlimit (0.0, 1.0, viewStart / room) : 0.0f;
        m->setParameter ("viewZoom", lastZoom);
        m->setParameter ("viewPos", lastPos);
    }
    void loadView()
    {
        auto* m = module.get();
        if (m == nullptr) return;
        lastZoom = juce::jlimit (1.0f, 1000.0f, m->getParameter ("viewZoom"));
        lastPos = juce::jlimit (0.0f, 1.0f, m->getParameter ("viewPos"));
        viewLen = juce::jlimit (juce::jmin (64.0, total()), total(), total() / (double) lastZoom);
        viewStart = (double) lastPos * (total() - viewLen);
        clampView();
    }

    static constexpr int kPeakBlock = 256, kBarH = 4, kGrab = 5;
    enum class Drag { None, Start, End, Pan };

    double total() const { return sample != nullptr ? (double) sample->getNumSamples() : 1.0; }
    double startPos() const { return juce::jlimit (0.0f, 1.0f, module->getParameter ("start")) * (total() - 1.0); }
    double endPos() const   { return juce::jlimit (0.0f, 1.0f, module->getParameter ("end"))   * (total() - 1.0); }
    double xOf (double pos) const { return (pos - viewStart) / juce::jmax (1.0, viewLen) * getWidth(); }
    double sampleAt (int x) const { return viewStart + viewLen * x / juce::jmax (1, getWidth()); }

    void clampView()
    {
        viewLen = juce::jlimit (1.0, total(), viewLen);
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, total() - viewLen), viewStart);
    }

    static juce::String timeText (double secs)
    {
        const int m = (int) (secs / 60.0);
        return juce::String (m) + ":" + juce::String (secs - m * 60.0, 1).paddedLeft ('0', 4);
    }

    void rebuildPeaks()
    {
        peaks.clear();
        if (sample == nullptr) return;
        const int n = sample->getNumSamples(), chans = sample->getNumChannels();
        peaks.resize ((size_t) (n / kPeakBlock + 1), { 0.0f, 0.0f });
        for (int c = 0; c < juce::jmin (1, chans); ++c)
        {
            const float* d = sample->getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                auto& p = peaks[(size_t) (i / kPeakBlock)];
                p.first = juce::jmin (p.first, d[i]);
                p.second = juce::jmax (p.second, d[i]);
            }
        }
    }

    void timerCallback() override
    {
        auto* m = module.get();
        if (m == nullptr)
            return;

        const int counter = m->getSampleChangeCounter();
        if (counter != lastCounter)
        {
            const bool first = lastCounter < 0;
            lastCounter = counter;
            sample = m->getLoadedSample (&rate);
            rebuildPeaks();
            if (first)
                loadView();   // the view the patch was saved with
            else
            {
                viewStart = 0.0;   // a new sample: all of it
                viewLen = total();
                storeView();
            }
            repaint();
        }
        if (m->getParameter ("viewZoom") != lastZoom || m->getParameter ("viewPos") != lastPos)
        {
            loadView();
            repaint();
        }
        const float a = m->getParameter ("start"), b = m->getParameter ("end");
        if (a != lastStart || b != lastEnd)
        {
            lastStart = a;
            lastEnd = b;
            repaint();
        }
    }

    juce::WeakReference<SynthModule> module;
    std::shared_ptr<const juce::AudioBuffer<float>> sample;
    std::vector<std::pair<float, float>> peaks;
    double rate { 44100.0 }, viewStart { 0.0 }, viewLen { 1.0 }, panFrom { 0.0 };
    Drag dragging { Drag::None };
    int lastCounter { -1 };
    float lastStart { -1.0f }, lastEnd { -1.0f }, lastZoom { -1.0f }, lastPos { -1.0f };
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

    // in double: a float loses whole samples along a 20-minute file
    startIdx = (int) ((double) juce::jmin (a, b) * (double) (total - 1));
    endIdx   = (int) ((double) juce::jmax (a, b) * (double) (total - 1));

    // Start above End is allowed on the sliders (and is a natural thing to do
    // while dragging); it reads as the same span the other way round
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
    rate[v] = midiNoteToHz ((double) (glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate) + pitchBendSemitones())) / rootHz;

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
        "Takes long files (20 minutes, 10 on Android): zoom the waveform with the wheel, slide by dragging, "
        "and drag Start and End right in it. Start and End bound the section that plays, as a fraction of the file. With Cycle off the "
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
        makeCombo  ("cycle", "Cycle", { "Off", "On" }, 0, 3, 2),
        // the waveform view (touch screens have no wheel): how far in, and where
        makeRotary ("viewZoom", "Zoom", 1.0f, 1000.0f, 1.0f, 2, "x", true).noMod(),
        makeRotary ("viewPos", "View Pos", 0.0f, 1.0f, 0.0f, 2).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SamplerModule, samplerDescriptor)
