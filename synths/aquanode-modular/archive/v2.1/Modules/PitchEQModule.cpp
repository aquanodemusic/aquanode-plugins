#include "PitchEQModule.h"

using namespace aquanode;

//==============================================================================
// little clickable keyboard: one octave of note-class toggles (C..B)
//==============================================================================
class PitchEQKeyboard : public juce::Component,
                        public aquanode::CustomParamCableTargets,
                        private juce::Timer
{
public:
    explicit PitchEQKeyboard (PitchEQModule& m) : module (&m)
    {
        startTimerHz (12);   // reflect patch loads / mutations / cable-driven keys
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = module.get();
        auto* eq = dynamic_cast<PitchEQModule*> (m);
        if (m == nullptr)
            return;

        const float w = whiteKeyWidth();
        const float h = (float) getHeight();

        // a key whose EFFECTIVE state (cable modulation folded in) differs
        // from its clicked state gets a small live-marker dot
        auto liveDot = [&] (int nc, bool baseOn, juce::Point<float> centre)
        {
            if (eq != nullptr && eq->isClassLiveOn (nc) != baseOn)
            {
                g.setColour (juce::Colour (0xffd970b0));
                g.fillEllipse (centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawEllipse (centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f, 1.0f);
            }
        };

        // white keys: C D E F G A B
        for (int i = 0; i < 7; ++i)
        {
            const int nc = whiteNoteClass[i];
            const bool on = m->getParameter ("nc" + juce::String (nc)) > 0.5f;
            juce::Rectangle<float> key (i * w, 0.0f, w, h);

            g.setColour (on ? juce::Colour (0xffd970b0) : juce::Colours::white.withAlpha (0.85f));
            g.fillRect (key.reduced (0.5f));
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRect (key, 1.0f);

            liveDot (nc, on, { i * w + w * 0.5f, h * 0.82f });
        }

        // black keys: C# D# F# G# A#
        for (int i = 0; i < 5; ++i)
        {
            const int nc = blackNoteClass[i];
            const bool on = m->getParameter ("nc" + juce::String (nc)) > 0.5f;
            auto key = blackKeyRect (i);

            g.setColour (on ? juce::Colour (0xffd970b0) : juce::Colour (0xff1a1a1a));
            g.fillRect (key);
            g.setColour (juce::Colours::black);
            g.drawRect (key, 1.0f);

            liveDot (nc, on, { key.getCentreX(), key.getCentreY() });
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // right-clicks belong to the module's knob-modulation menu (opened by
        // the owning ModuleComponent through its mouse listener) - a popup
        // click must not toggle the key underneath
        if (e.mods.isPopupMenu())
            return;

        auto* m = module.get();
        if (m == nullptr)
            return;

        const int nc = noteClassAt (e.position);
        const auto id = "nc" + juce::String (nc);
        m->setParameter (id, m->getParameter (id) > 0.5f ? 0.0f : 1.0f);
        repaint();
    }

    //=== CustomParamCableTargets: each key accepts a knob-modulation cable ====
    juce::String paramTargetAt (juce::Point<int> localPos) const override
    {
        if (! getLocalBounds().contains (localPos))
            return {};
        return "nc" + juce::String (noteClassAt (localPos.toFloat()));
    }

    // cables snap to the centre of the key: black keys attach at their body
    // centre, white keys at the exposed lower part below the black keys
    juce::Point<int> paramTargetCentre (const juce::String& paramId) const override
    {
        if (! paramId.startsWith ("nc"))
            return { -1, -1 };
        const int nc = paramId.substring (2).getIntValue();

        for (int i = 0; i < 5; ++i)
            if (blackNoteClass[i] == nc)
                return blackKeyRect (i).getCentre().toInt();

        for (int i = 0; i < 7; ++i)
            if (whiteNoteClass[i] == nc)
                return { (int) ((i + 0.5f) * whiteKeyWidth()), (int) (getHeight() * 0.8f) };

        return { -1, -1 };
    }

private:
    static constexpr int whiteNoteClass[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int blackNoteClass[5] = { 1, 3, 6, 8, 10 };
    // black key centred over the boundary after white key 0,1,3,4,5
    static constexpr int blackAfterWhite[5] = { 0, 1, 3, 4, 5 };

    float whiteKeyWidth() const { return (float) getWidth() / 7.0f; }

    juce::Rectangle<float> blackKeyRect (int i) const
    {
        const float w = whiteKeyWidth();
        const float bw = w * 0.62f;
        return { (blackAfterWhite[i] + 1) * w - bw * 0.5f, 0.0f, bw, getHeight() * 0.6f };
    }

    int noteClassAt (juce::Point<float> pos) const
    {
        for (int i = 0; i < 5; ++i)               // black keys sit on top
            if (blackKeyRect (i).contains (pos))
                return blackNoteClass[i];

        const int i = juce::jlimit (0, 6, (int) (pos.x / whiteKeyWidth()));
        return whiteNoteClass[i];
    }

    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
};

constexpr int PitchEQKeyboard::whiteNoteClass[7];
constexpr int PitchEQKeyboard::blackNoteClass[5];
constexpr int PitchEQKeyboard::blackAfterWhite[5];

std::unique_ptr<juce::Component> PitchEQModule::createExtraContentComponent()
{
    return std::make_unique<PitchEQKeyboard> (*this);
}

//==============================================================================
// peaking EQ with constant-semitone bandwidth (from the original plugin)
//==============================================================================
static PitchEQModule::Biquad makePeakingEQ (double fc, double sampleRate,
                                            double gainDB, double bwSemitones)
{
    PitchEQModule::Biquad bq;

    fc = juce::jlimit (20.0, sampleRate * 0.45, fc);
    const double A = std::pow (10.0, gainDB / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * fc / sampleRate;

    // bandwidth in octaves from semitones; Q from bandwidth
    const double bwOct = juce::jmax (0.005, bwSemitones / 12.0);
    const double q = 1.0 / (2.0 * std::sinh (std::log (2.0) * 0.5 * bwOct * w0 / std::sin (w0)));
    const double alpha = std::sin (w0) / (2.0 * q);

    const double a0inv = 1.0 / (1.0 + alpha / A);
    bq.b0 = (1.0 + alpha * A) * a0inv;
    bq.b1 = (-2.0 * std::cos (w0)) * a0inv;
    bq.b2 = (1.0 - alpha * A) * a0inv;
    bq.a1 = bq.b1;
    bq.a2 = (1.0 - alpha / A) * a0inv;
    return bq;
}

void PitchEQModule::rebuildFilters()
{
    const float dampenDB = param (pDampen);
    const float boostDB = param (pBoost);
    // Width knob 0 = wide, 1 = narrow: BW = 4.0 * exp(-6 t) semitones
    const double bw = 4.0 * std::exp (-6.0 * (double) juce::jlimit (0.0f, 1.0f, param (pWidth)));

    loNote = juce::jlimit (0, kTotalNotes - 1, (int) param (pRangeLo));
    hiNote = juce::jlimit (0, kTotalNotes - 1, (int) param (pRangeHi));
    if (loNote > hiNote)
        std::swap (loNote, hiNote);

    bool protectedClass[12] {};
    bool anyProtected = false;
    for (int i = 0; i < 12; ++i)
    {
        protectedClass[i] = param (6 + i) > 0.5f;   // nc0..nc11
        anyProtected = anyProtected || protectedClass[i];
    }

    anyActive = false;

    for (int n = 0; n < kTotalNotes; ++n)
    {
        const bool inRange = n >= loNote && n <= hiNote;
        const bool isProtected = protectedClass[n % 12];
        const double fc = midiNoteToHz (n);

        // dampen everything in range that is NOT a selected note class
        if (anyProtected && inRange && ! isProtected && dampenDB < -0.01f)
        {
            anyActive = true;
            double totalCut = juce::jmax ((double) dampenDB,
                                          kMaxDampenPerStage * kMaxDampenStages);
            int stages = (int) std::ceil (std::abs (totalCut) / std::abs (kMaxDampenPerStage));
            stages = juce::jlimit (1, kMaxDampenStages, stages);
            dampenStageCount[n] = stages;
            const double perStage = totalCut / stages;
            for (int st = 0; st < stages; ++st)
                dampenFilters[n][st] = makePeakingEQ (fc, sampleRate, perStage, bw);
        }
        else
            dampenStageCount[n] = 0;

        // boost the selected note classes in range
        boostActive[n] = anyProtected && inRange && isProtected && boostDB > 0.01f;
        if (boostActive[n])
        {
            anyActive = true;
            boostFilters[n] = makePeakingEQ (fc, sampleRate, (double) boostDB, bw);
        }
    }
}

void PitchEQModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float gain = std::pow (10.0f, param (pGain) / 20.0f);

    if (! anyActive)
    {
        outputs[0][0] = inputs[0][0] * gain;
        outputs[0][1] = inputs[0][1] * gain;
        return;
    }

    for (int c = 0; c < 2; ++c)
    {
        double x = (double) inputs[0][(size_t) c];

        for (int n = loNote; n <= hiNote; ++n)
        {
            for (int st = 0; st < dampenStageCount[n]; ++st)
                x = dampenFilters[n][st].process (x, dampenStates[c][n][st]);
            if (boostActive[n])
                x = boostFilters[n].process (x, boostStates[c][n]);
        }

        outputs[0][(size_t) c] = (float) x * gain;
    }
}

static ModuleDescriptor pitchEQDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.pitcheq";
    d.displayName = "Pitch Lock Filter";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 3;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("dampen", "Dampen", -64.0f, 0.0f, -24.0f, 0, "dB").noMod(),
        makeRotary ("width",  "Width",  0.0f, 1.0f, 0.5f, 0).noMod(),
        makeRotary ("boost",  "Boost",  0.0f, 24.0f, 0.0f, 0, "dB").noMod(),
        makeSteppedList ("rangeLo", "Lo", midiNoteNameChoices(), 36, 1),
        makeSteppedList ("rangeHi", "Hi", midiNoteNameChoices(), 96, 1),
        makeRotary ("gain",   "Gain",   -12.0f, 12.0f, 0.0f, 1, "dB")
    };
    for (int i = 0; i < 12; ++i)
        d.params.push_back (makeRotary ("nc" + juce::String (i),
                                        "NC" + juce::String (i),
                                        0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f)
                                .hide()
                                .cableTargetInCustomUI());   // keys accept LFO / DAW-mod cables
    return d;
}

AQUANODE_REGISTER_MODULE (PitchEQModule, pitchEQDescriptor)
