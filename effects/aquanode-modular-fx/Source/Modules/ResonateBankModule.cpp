#include "ResonateBankModule.h"

using namespace aquanode;
using namespace aqresonate;

namespace
{
    // resonator I hears the mono sum; II-V the left and right input in turn
    constexpr int kSourceChannel[ResonateBankModule::kNumRes] = { -1, 0, 1, 0, 1 };
    const char* kRoman[ResonateBankModule::kNumRes] = { "I", "II", "III", "IV", "V" };
}

void ResonateBankModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    for (auto& v : voices)
        v.prepare (sr);     // allocates the delay lines: message thread only
    for (auto& s : stage)
        s.prepare (sr);
    reset();
}

void ResonateBankModule::reset()
{
    for (auto& v : voices) v.reset();
    for (auto& s : stage) s.reset();
    for (auto& f : filt) f.reset();
    for (int i = 0; i < kNumRes; ++i)
    {
        midiNote[i] = -1;
        midiActive[i] = false;
        midiOrder[i] = 0;
        shownNote[i].store (-1, std::memory_order_relaxed);
    }
    for (auto& s : slotRes) s = -1;
    rrPointer = 0;
    ctrlLeft = 0;
}

//==============================================================================
// MIDI: round robin over the resonators that are switched on; when all are
// held the oldest is taken
void ResonateBankModule::voiceNoteOn (int slot, int note, bool)
{
    int res = -1;
    for (int k = 0; k < kNumRes; ++k)
    {
        const int idx = (rrPointer + k) % kNumRes;
        if (rp (idx, fOn) > 0.5f && ! midiActive[idx]) { res = idx; break; }
    }
    if (res < 0)
    {
        juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
        for (int i = 0; i < kNumRes; ++i)
            if (rp (i, fOn) > 0.5f && midiOrder[i] < oldest) { oldest = midiOrder[i]; res = i; }
    }
    if (res < 0)
        return;   // nothing switched on: nowhere for the note to go

    midiNote[res] = note;
    midiActive[res] = true;
    midiOrder[res] = ++midiCounter;
    rrPointer = (res + 1) % kNumRes;
    if (slot >= 0 && slot < kMaxVoices)
        slotRes[slot] = res;
    ctrlLeft = 0;   // retune now
}

void ResonateBankModule::voiceNoteOff (int slot)
{
    if (slot < 0 || slot >= kMaxVoices)
        return;
    if (const int res = slotRes[slot]; res >= 0)
        midiActive[res] = false;   // it keeps ringing at the note it was given
    slotRes[slot] = -1;
}

//==============================================================================
void ResonateBankModule::updateCoefficients()
{
    const double baseNote = juce::jlimit (0.0f, 127.0f, std::round (param (pNote)));
    const bool perRes = param (pPerRes) > 0.5f;
    const bool constMode = param (pConst) > 0.5f;
    const bool modeB = param (pMode) > 0.5f;

    const int filterType = juce::jlimit (0, 4, (int) std::lround (param (pFilter)));
    filterOn = filterType > 0;
    if (filterOn)
        for (auto& f : filt)
            f.set (filterType - 1, param (pFilterFreq), sampleRate);   // LP, HP, BP, Notch

    width = juce::jlimit (0.0f, 1.0f, param (pWidth) * 0.01f);
    masterGain = juce::Decibels::decibelsToGain (param (pGain));
    mix = juce::jlimit (0.0f, 1.0f, param (pDryWet) * 0.01f);

    for (int i = 0; i < kNumRes; ++i)
    {
        resOn[i] = rp (i, fOn) > 0.5f;
        resGain[i] = juce::Decibels::decibelsToGain (rp (i, fGain), -100.0f);

        // pan: resonator I is a balance control, II-V constant power,
        // snapped to exactly one side at the extremes
        const double pan = juce::jlimit (-1.0, 1.0, (double) rp (i, fPan) * 0.01);
        if (i == 0)
        {
            panL[0] = (float) (pan <= 0.0 ? 1.0 : 1.0 - pan);
            panR[0] = (float) (pan >= 0.0 ? 1.0 : 1.0 + pan);
        }
        else if (pan <= -0.9995) { panL[i] = 1.0f; panR[i] = 0.0f; }
        else if (pan >= 0.9995)  { panL[i] = 0.0f; panR[i] = 1.0f; }
        else
        {
            const double theta = (pan + 1.0) * juce::MathConstants<double>::pi * 0.25;
            panL[i] = (float) std::cos (theta);
            panR[i] = (float) std::sin (theta);
        }

        // II-V: the stepped list runs -24..+24, stored as its index 0..48
        const double knobNote = baseNote + (i > 0 ? std::round (rp (i, fPitch)) - 24.0 : 0.0);
        const double note = juce::jlimit (0.0, 135.0, (midiNote[i] >= 0 ? (double) midiNote[i] : knobNote)
                                                      + rp (i, fFine) / 100.0);
        shownNote[i].store (resOn[i] ? juce::jlimit (0, 127, (int) std::lround (note)) : -1, std::memory_order_relaxed);

        if (! resOn[i])
            continue;

        const double decay = perRes ? param (pResDecayBase + i) : param (pDecay);
        const double color = perRes ? param (pResColorBase + i) : param (pColor);
        voices[i].setParams (note, decay, color, constMode, modeB);
    }
}

void ResonateBankModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (--ctrlLeft < 0)
    {
        updateCoefficients();
        ctrlLeft = 31;
    }

    std::array<float, 2> s {};
    for (int ch = 0; ch < 2; ++ch)
    {
        float x = inputs[0][(size_t) ch];
        if (filterOn)
            x = filt[ch].process (x);
        s[(size_t) ch] = stage[ch].process (x);      // x0.25 and the 327 Hz high-pass
    }

    const float mono = 0.5f * (s[0] + s[1]);
    const float o1 = resOn[0] ? voices[0].process (mono) * resGain[0] : 0.0f;
    float wetL = o1 * panL[0], wetR = o1 * panR[0];

    float busL = 0.0f, busR = 0.0f;
    for (int i = 1; i < kNumRes; ++i)
    {
        if (! resOn[i])
            continue;
        const float o = voices[i].process (s[(size_t) kSourceChannel[i]]) * resGain[i];
        busL += o * panL[i];
        busR += o * panR[i];
    }
    const float mid = 0.5f * (busL + busR), side = 0.5f * (busL - busR) * width;
    wetL += mid + side;
    wetR += mid - side;

    outputs[0][0] = inputs[0][0] * (1.0f - mix) + wetL * masterGain * mix;
    outputs[0][1] = inputs[0][1] * (1.0f - mix) + wetR * masterGain * mix;
}

//==============================================================================
// the cyan note readout: what each resonator plays (knobs or MIDI)
class ResonateNoteDisplay : public juce::Component, private juce::Timer
{
public:
    explicit ResonateNoteDisplay (ResonateBankModule& m) : module (&m) { startTimerHz (12); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0f1a1d));
        g.fillRoundedRectangle (b, 4.0f);

        const float w = b.getWidth() / (float) ResonateBankModule::kNumRes;
        for (int i = 0; i < ResonateBankModule::kNumRes; ++i)
        {
            auto cell = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight());
            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (kRoman[i], cell.removeFromTop (9.0f).reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.setColour (notes[i] >= 0 ? juce::Colour (0xff3fe0ff) : juce::Colours::white.withAlpha (0.2f));
            g.drawText (notes[i] >= 0 ? midiNoteName (notes[i]) : juce::String ("off"), cell.translated (0.0f, -2.0f),
                        juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* m = module.get();
        if (m == nullptr || getWidth() <= 0)
            return;
        const int i = juce::jlimit (0, ResonateBankModule::kNumRes - 1,
                                    e.x * ResonateBankModule::kNumRes / getWidth());
        const auto id = "on" + juce::String (i + 1);
        m->setParameter (id, m->getParameter (id) > 0.5f ? 0.0f : 1.0f);
        timerCallback();
    }

private:
    void timerCallback() override
    {
        auto* m = dynamic_cast<ResonateBankModule*> (module.get());
        if (m == nullptr)
            return;
        bool changed = false;
        for (int i = 0; i < ResonateBankModule::kNumRes; ++i)
        {
            // until the audio runs the knobs are the truth
            const bool on = m->getParameter ("on" + juce::String (i + 1)) > 0.5f;
            int n = on ? m->soundingNote (i) : -1;
            if (n < 0 && on)
                n = juce::jlimit (0, 127, (int) std::lround (m->getParameter ("note")
                                                             + (i > 0 ? m->getParameter ("pitch" + juce::String (i + 1)) - 24.0f : 0.0f)));
            if (n != notes[i]) { notes[i] = n; changed = true; }
        }
        if (changed)
            repaint();
    }

    juce::WeakReference<SynthModule> module;
    int notes[ResonateBankModule::kNumRes] { -2, -2, -2, -2, -2 };
};

std::unique_ptr<juce::Component> ResonateBankModule::createExtraContentComponent()
{
    return std::make_unique<ResonateNoteDisplay> (*this);
}

//==============================================================================
static ModuleDescriptor resonateBankDescriptor()
{
    constexpr int kNumRes = ResonateBankModule::kNumRes;
    ModuleDescriptor d;
    d.typeId = "fx.resonate5";
    d.displayName = "Resonate";
    d.description =
        "Five tuned comb resonators with a measured 'Modelled' engine: resonator I "
        "sets the pitch (Note) and hears the mono sum, II-V are tuned around it in semitones (default G3, "
        "+2, +9, +9, +2) and take left and right in turn. Decay and Color shape the ring (or each "
        "resonator's own in Per Res mode), Const keeps the decay equal at every pitch, Odd inverts the "
        "feedback for a hollow tone. Notes on Midi In retune the resonators round-robin. Click a resonator "
        "in the note readout to switch it on or off.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 61;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        midiIn   ("midiIn",   "Midi In"),
        audioOut ("audioOut", "Audio Out")
    };

    juce::StringArray semis;
    for (int i = -24; i <= 24; ++i)
        semis.add (i > 0 ? "+" + juce::String (i) : juce::String (i));

    d.params = {
        makeSteppedList ("note", "Note", midiNoteNameChoices(), 55, 0),     // G3
        makeRotary ("decay",  "Decay",   0.0f, 100.0f, 75.0f, 0),
        makeRotary ("color",  "Color",   0.0f, 100.0f, 45.0f, 0),
        makeRotary ("gain",   "Gain",    -48.0f, 12.0f, 0.0f, 0, "dB"),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),

        makeCombo ("mode",   "Mode",  { "Full", "Odd" }, 0, 1, 2),
        makeCombo ("const",  "Const", { "Free", "Const" }, 0, 1, 2),
        makeRotary ("width",  "Width", 0.0f, 100.0f, 100.0f, 1, "%", false, 1.0f),

        makeCombo ("perRes", "Per Res", { "Shared", "Per Res" }, 0, 2, 2),
        makeCombo ("filter", "Filter", { "Filter Off", "Lowpass", "Highpass", "Bandpass", "Notch" }, 0, 2, 2),
        makeRotary ("filterFreq", "Filter Hz", 20.0f, 12000.0f, 1000.0f, 2, {}, true),
    };

    const int defaultPitch[kNumRes] = { 0, 2, 9, 9, 2 };
    const float defaultPan[kNumRes] = { 0.0f, -100.0f, 100.0f, -100.0f, 100.0f };
    for (int i = 0; i < kNumRes; ++i)
    {
        const juce::String n (i + 1), r (kRoman[i]);
        const int row = 3 + i;
        // switched on and off by clicking the resonator in the note readout
        d.params.push_back (makeCombo ("on" + n, r + " On", { r + " Off", r + " On" }, 1, row, 1).hide());
        auto pitch = makeSteppedList ("pitch" + n, r + " Pitch", semis, 24 + defaultPitch[i], row);
        if (i == 0)
            pitch.hide();   // resonator I is the Note
        d.params.push_back (pitch);
        d.params.push_back (makeRotary ("fine" + n, r + " Fine", -50.0f, 50.0f, 0.0f, row, "ct"));
        d.params.push_back (makeRotary ("gain" + n, r + " Gain", -48.0f, 12.0f, 0.0f, row, "dB"));
        d.params.push_back (makeRotary ("pan" + n, r + " Pan", -100.0f, 100.0f, defaultPan[i], row, {}, false, 1.0f));
    }
    for (int i = 0; i < kNumRes; ++i)
        d.params.push_back (makeRotary ("decay" + juce::String (i + 1), juce::String (kRoman[i]) + " Decay",
                                        0.0f, 100.0f, 50.0f, 8).visibleWhen ("perRes", 1.0f));
    for (int i = 0; i < kNumRes; ++i)
        d.params.push_back (makeRotary ("color" + juce::String (i + 1), juce::String (kRoman[i]) + " Color",
                                        0.0f, 100.0f, 50.0f, 9).visibleWhen ("perRes", 1.0f));
    return d;
}

AQUANODE_REGISTER_MODULE (ResonateBankModule, resonateBankDescriptor)
