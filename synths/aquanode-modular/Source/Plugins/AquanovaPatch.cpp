//==============================================================================
// Aquanova - Patch version
//
// The Supernova-style voice, as the hardware panel lays it out - every
// destination module carries its own modulation amounts (Env 2, Env 3, LFO 1,
// LFO 2, Wheel, and Aftertouch where the hardware has it), and the sources are
// patched into it:
//
//   Osc 1..3, Noise, Ring Mod --(audio + level)--> Mixer -> Filter -> Amp (Env 1)
//   Env 2, Env 3, LFO 1, LFO 2 --> every module that they modulate
//   (voices summed) -> Vocoder -> EQ -> Distortion -> Comb
//        -> Chorus / Delay / Reverb sends, wired in the Fx Config's order
//        -> Effects Mix -> Panner -> Output
//
// Every knob is the hardware parameter with its own range (0..127, -64..+63,
// or the hardware's value list), so presets, SysEx dumps and the GUI card's
// state map across one to one. The DSP is Aquanova's own (DSP/*.h) and the
// voice code is Voice::renderNextSample split along its sections.
//
// Where the patch differs: unison renders its stacked copies inside the
// oscillators and noise (each copy detuned and panned as the engine does it)
// instead of as whole extra voices, so with unison on, the ring modulators
// and the filter's overdrive see the stack summed. Mono legato is emulated
// (see kit::LegatoTracker).
//==============================================================================
#include "PluginAquanovaConfig.h"

namespace aqp_aquanova
{
    #include AQP_AQUANOVA_SRC(Parameters.h)
    #include AQP_AQUANOVA_SRC(DSP/Oscillator.h)
    #include AQP_AQUANOVA_SRC(DSP/Filter.h)
    #include AQP_AQUANOVA_SRC(DSP/Envelope.h)
    #include AQP_AQUANOVA_SRC(DSP/Lfo.h)
    #include AQP_AQUANOVA_SRC(DSP/Effects.h)
    #include AQP_AQUANOVA_SRC(DSP/Vocoder.h)
    #include AQP_AQUANOVA_SRC(PluginProcessor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;
namespace AQ = aqp_aquanova::aquanova;
namespace P = aqp_aquanova::aquanova::P;

namespace
{
    const juce::String kPlugin { "aquanova" };
    constexpr int kMaxUnison = 8;

    //==========================================================================
    // Hardware parameters as module knobs
    //==========================================================================
    struct HwEntry
    {
        int hw;                 // hardware index (the Osc 1 / Env 2 / LFO 1 one for repeated blocks)
        const char* id;         // module parameter id
        const char* label;
        int row;
        int width;
        bool shared;            // a global parameter (no Osc / LFO / Env block offset)
    };

    const AQ::ParamDesc* descFor (int hw)
    {
        const int slot = AQ::paramSlotForIndex (hw);
        return slot >= 0 ? &AQ::kParams[slot] : nullptr;
    }

    ParamSpec specFor (const HwEntry& e)
    {
        const auto* d = descFor (e.hw);
        jassert (d != nullptr);
        ParamSpec s;
        if (d == nullptr)
            return makeRotary (e.id, e.label, 0.0f, 127.0f, 0.0f, e.row, {}, false, 1.0f);

        const auto& values = AQ::getValueList (d->valueList);
        if (d->discrete && values.size() == d->max - d->min + 1)
            s = e.width >= 2 ? makeCombo (e.id, e.label, values, d->def - d->min, e.row, e.width)
                             : makeSteppedList (e.id, e.label, values, d->def - d->min, e.row);
        else if (d->bipolar)
            s = makeRotary (e.id, e.label, -64.0f, 63.0f, (float) (d->def - 64), e.row, {}, false, 1.0f);
        else
            s = makeRotary (e.id, e.label, (float) d->min, (float) d->max, (float) d->def, e.row, {}, false, 1.0f);
        return s;
    }

    // the module value of a hardware raw value (and back)
    float rawToKnob (int hw, int raw)
    {
        const auto* d = descFor (hw);
        if (d == nullptr) return (float) raw;
        if (d->discrete) return (float) (raw - d->min);
        return d->bipolar ? (float) (raw - 64) : (float) raw;
    }

    //==========================================================================
    // A module whose knobs are hardware parameters: raw / uni / bip / flag
    // read them exactly like Aquanova's ParamAccess, so the voice code can be
    // taken over as it is.
    class HwModule : public SynthModule
    {
    public:
        void prepare (double sr) override
        {
            SynthModule::prepare (sr);
            slot.fill (-1);
            for (const auto& e : table())
            {
                const auto* d = descFor (e.hw);
                const int i = paramIndex (e.id);
                if (i >= 0 && juce::isPositiveAndBelow (e.hw, (int) slot.size()))
                {
                    slot[(size_t) e.hw] = (short) i;
                    offsetOf[(size_t) e.hw] = d == nullptr ? 0 : (d->discrete ? d->min : (d->bipolar ? 64 : 0));
                }
            }
        }

        virtual const std::vector<HwEntry>& table() const = 0;

    protected:
        int raw (int hw) const noexcept
        {
            const int s = slot[(size_t) hw];
            return s < 0 ? 0 : (int) std::lround (param (s)) + offsetOf[(size_t) hw];
        }
        float uni (int hw) const noexcept { return (float) raw (hw) * (1.0f / 127.0f); }
        float bip (int hw) const noexcept
        {
            const int v = raw (hw) - 64;
            return v < 0 ? (float) v * (1.0f / 64.0f) : (float) v * (1.0f / 63.0f);
        }
        bool flag (int hw) const noexcept { return raw (hw) != 0; }

        float wheel() const { return midiControls != nullptr ? midiControls->modWheel : 0.0f; }
        float aftertouch() const { return midiControls != nullptr ? midiControls->aftertouch : 0.0f; }

    private:
        std::array<short, 400> slot {};
        std::array<int, 400> offsetOf {};
    };

    ModuleDescriptor baseDescriptor (const juce::String& id, const juce::String& kind, ModuleSection s,
                                     int order, const std::vector<HwEntry>& table)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.aquanova." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        for (const auto& e : table)
            d.params.push_back (specFor (e));
        return d;
    }

    juce::StringArray voiceList()
    {
        juce::StringArray a { "mono" };
        for (int i = 2; i <= kMaxVoices; ++i) a.add (juce::String (i));
        return a;
    }

    // the voices / legato pair on every sound source
    void addVoiceControls (ModuleDescriptor& d, int row)
    {
        d.params.push_back (makeSteppedList ("voices", "Voices", voiceList(), 15, row).noMod());
        d.params.push_back (makeCombo ("legato", "Legato", { "Off", "On" }, 0, row, 1));
    }

    // LFO 1 / 2 Sync: "Off" then 32nd triplet .. 8 bar dotted, as Hz at the tempo
    float syncedLfoHz (int syncIndex, double bpm) noexcept
    {
        static const double beats[34] = { 1.0/6, 0.125, 1.0/3, 0.25, 2.0/3, 0.375, 0.5, 4.0/3, 0.75, 1.0,
            8.0/3, 1.5, 2.0, 16.0/3, 3.0, 4.0, 32.0/3, 6.0, 8.0, 64.0/3, 12.0, 80.0/3, 16.0, 18.0,
            112.0/3, 20.0, 128.0/3, 24.0, 28.0, 30.0, 32.0, 36.0, 42.0, 48.0 };
        const double beatSeconds = 60.0 / juce::jmax (20.0, bpm);
        return (float) (1.0 / juce::jmax (0.001, beats[juce::jlimit (0, 33, syncIndex - 1)] * beatSeconds));
    }

    // unison: count, and each copy's detune (cents) and pan, as SynthEngine::startVoices
    struct Unison
    {
        int count { 1 };
        float cents[kMaxUnison] {}, pan[kMaxUnison] {}, gainL[kMaxUnison] {}, gainR[kMaxUnison] {};
        float gain { 1.0f };

        void set (int uniRaw, float detuneUni)
        {
            count = juce::jlimit (1, kMaxUnison, uniRaw == 0 ? 1 : (uniRaw == 1 ? 2 : uniRaw));
            gain = 1.0f / std::sqrt ((float) count);
            for (int i = 0; i < count; ++i)
            {
                const float spread = count > 1 ? ((float) i / (float) (count - 1)) * 2.0f - 1.0f : 0.0f;
                cents[i] = count > 1 ? spread * detuneUni * 40.0f : 0.0f;
                pan[i] = spread * 0.7f;
                const float a = (pan[i] + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                gainL[i] = std::cos (a) * gain;
                gainR[i] = std::sin (a) * gain;
            }
        }
    };

    // the five standard modulation inputs of a voice module, in socket order
    enum { inEnv2 = 0, inEnv3, inLfo1, inLfo2 };

    std::vector<SocketSpec> modInputs()
    {
        return { modIn ("env2In", "Env 2"), modIn ("env3In", "Env 3"), modIn ("lfo1In", "LFO 1"), modIn ("lfo2In", "LFO 2") };
    }
}

//==============================================================================
// Aquanova - Envelope
//==============================================================================
static const std::vector<HwEntry> kEnvTable {
    { P::Env2Attack,      "attack",      "Attack",   0, 1, false },
    { P::Env2Decay,       "decay",       "Decay",    0, 1, false },
    { P::Env2Sustain,     "sustain",     "Sustain",  0, 1, false },
    { P::Env2Release,     "release",     "Release",  0, 1, false },
    { P::Env2Velocity,    "velocity",    "Velocity", 0, 1, false },
    { P::Env2Delay,       "delay",       "Delay",    1, 1, false },
    { P::Env2KeyTracking, "keyTrack",    "Key Track", 1, 1, false },
    { P::Env2ADRepeat,    "adRepeat",    "AD Repeat", 1, 1, false },
    { P::Env2SustainTime, "sustainTime", "Sus Time", 1, 1, false },
    { P::Env2SustainRate, "sustainRate", "Sus Rate", 1, 1, false },
    { P::ConstantGate,    "constantGate", "Const Gate", 2, 2, true },
};

class AquanovaEnvModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("env", "Envelope", ModuleSection::Utility, 10, kEnvTable);
        d.description = "One of Aquanova's three envelopes (Env 1 is the amp): delay, attack, decay, sustain "
                        "and release on the hardware's 0..127 times, sustain time and rate, AD repeat, key "
                        "tracking and velocity. Single trigger reshapes from the current level on legato notes.";
        d.sockets = { modOut ("envOut", "Env Out") };
        d.params.push_back (makeCombo ("trigger", "Trigger", { "Multi", "Single" }, 0, 2, 2));
        d.params.push_back (makeCombo ("legato", "Legato", { "Off", "On" }, 0, 2, 1));
        // Env 1 decides how long a voice lives (with Constant Gate on, past
        // the key-up for as long as it runs)
        d.params.push_back (makeCombo ("ampEnv", "Amp Env", { "Off", "On" }, 0, 2, 1).hide());
        return d;
    }

    bool canHoldVoicesOpen() const override { return true; }
    bool voiceHeldOpen (int v) const override
    {
        return getParameterBase (iAmp) >= 0.5f && flag (P::ConstantGate)
            && ! held[(size_t) v] && env[(size_t) v].isActive();
    }

    const std::vector<HwEntry>& table() const override { return kEnvTable; }
    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        HwModule::prepare (sr);
        for (auto& e : env) e.prepare (sr);
        iTrig = paramIndex ("trigger"); iLegato = paramIndex ("legato"); iAmp = paramIndex ("ampEnv");
        tracker.reset();
        held.fill (false);
    }
    void reset() override { for (auto& e : env) e.reset(); tracker.reset(); held.fill (false); }
    void voiceReset (int v) override { env[(size_t) v].reset(); held[(size_t) v] = false; }

    void voiceVelocity (int v, float vel) override
    {
        // the envelope is (re)started here, where the velocity is known
        const int from = pendingFrom;
        auto& e = env[(size_t) v];
        bool wasSounding = pendingWasSounding;
        if (from >= 0)
        {
            e = env[(size_t) from];
            wasSounding = true;
        }
        const bool single = param (iTrig) >= 0.5f;
        e.noteOn (settings(), vel, pendingNote, ! single || ! wasSounding);
    }

    void voiceNoteOn (int v, int note, bool) override
    {
        pendingWasSounding = held[(size_t) v] && env[(size_t) v].isActive();
        pendingFrom = tracker.noteOn (v, param (iLegato) >= 0.5f);
        pendingNote = note;
        held[(size_t) v] = true;
    }

    void voiceNoteOff (int v) override
    {
        tracker.noteOff();
        held[(size_t) v] = false;
        if (! flag (P::ConstantGate))
            env[(size_t) v].noteOff();
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        out[0] = { env[(size_t) v].process(), 0.0f };
    }

    double voiceTailSeconds() const override
    {
        return 0.001 * std::pow (20000.0, getParameterBase (paramIndex ("release")) / 127.0) * 3.0 + 0.05;
    }

private:
    AQ::Envelope::Settings settings() const
    {
        AQ::Envelope::Settings s;
        s.attack = raw (P::Env2Attack);       s.decay = raw (P::Env2Decay);
        s.sustain = raw (P::Env2Sustain);     s.release = raw (P::Env2Release);
        s.velocityAmount = uni (P::Env2Velocity);
        s.delay = raw (P::Env2Delay);         s.keyTrack = uni (P::Env2KeyTracking);
        s.adRepeat = raw (P::Env2ADRepeat);   s.sustainTime = raw (P::Env2SustainTime);
        s.sustainRate = bip (P::Env2SustainRate);
        return s;
    }

    std::array<AQ::Envelope, kMaxVoices> env;
    std::array<bool, kMaxVoices> held {};
    kit::LegatoTracker tracker;
    int iTrig { 0 }, iLegato { 0 }, iAmp { 0 }, pendingFrom { -1 }, pendingNote { 60 };
    bool pendingWasSounding { false };
};

//==============================================================================
// Aquanova - LFO
//==============================================================================
static const std::vector<HwEntry> kLfoTable {
    { P::LFO1Type,            "type",       "Wave",       0, 2, false },
    { P::LFO1Speed,           "speed",      "Speed",      0, 1, false },
    { P::LFO1Range,           "range",      "Range",      0, 2, false },
    { P::LFO1Sync,            "sync",       "Sync",       1, 1, false },
    { P::LFO1Delay,           "delay",      "Delay",      1, 1, false },
    { P::LFO1FadeMode,        "fadeMode",   "Fade",       1, 1, false },
    { P::LFO1Offset,          "offset",     "Offset",     1, 1, false },
    { P::LFO1Soften,          "soften",     "Soften",     1, 1, false },
    { P::LFO1SpeedEnv3,       "speedEnv3",  "Spd Env3",   2, 1, false },
    { P::LFO1SpeedWheel,      "speedWheel", "Spd Wheel",  2, 1, false },
    { P::LFO1SpeedAftertouch, "speedAT",    "Spd AT",     2, 1, false },
    { P::LFO1Trigger,         "trigger",    "Key Sync",   2, 1, false },
    { P::LFO1DelayTrigger,    "delayTrigger", "Dly Trig", 2, 1, false },
};

class AquanovaLfoModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("lfo", "LFO", ModuleSection::Utility, 11, kLfoTable);
        d.description = "One of Aquanova's two per-voice LFOs: square, saw, triangle or S/H in three speed "
                        "ranges or synced, with delay or fade-in, offset and soften. Env 3, the wheel and "
                        "aftertouch speed it up or slow it down by up to 4 octaves each.";
        d.sockets = { modIn ("env3In", "Env 3"), modOut ("lfoOut", "LFO Out") };
        d.params.push_back (makeCombo ("legato", "Legato", { "Off", "On" }, 0, 3, 1));
        return d;
    }

    const std::vector<HwEntry>& table() const override { return kLfoTable; }
    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        HwModule::prepare (sr);
        for (auto& l : lfo) l.prepare (sr);
        iLegato = paramIndex ("legato");
        tracker.reset();
    }
    void reset() override { for (auto& l : lfo) l.reset(); tracker.reset(); }

    void voiceNoteOn (int v, int, bool) override
    {
        const int from = tracker.noteOn (v, param (iLegato) >= 0.5f);
        if (from >= 0)
            lfo[(size_t) v] = lfo[(size_t) from];
        lfo[(size_t) v].noteOn (raw (P::LFO1Trigger) == 1, raw (P::LFO1DelayTrigger) == 0);
    }
    void voiceNoteOff (int) override { tracker.noteOff(); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        const float e3 = in[0][0];
        const int sync = raw (P::LFO1Sync);
        const float base = sync > 0 ? syncedLfoHz (sync, tempoBpm)
                                    : AQ::Lfo::speedToHz (raw (P::LFO1Speed), raw (P::LFO1Range));
        const float hz = base * std::pow (2.0f, bip (P::LFO1SpeedEnv3) * e3 * 4.0f
                                              + bip (P::LFO1SpeedWheel) * wheel() * 4.0f
                                              + bip (P::LFO1SpeedAftertouch) * aftertouch() * 4.0f);
        const float value = lfo[(size_t) v].process (hz, raw (P::LFO1Type), raw (P::LFO1Delay),
                                                     raw (P::LFO1FadeMode) != 0, bip (P::LFO1Offset),
                                                     uni (P::LFO1Soften));
        out[0] = { value, 0.0f };
    }

private:
    std::array<AQ::Lfo, kMaxVoices> lfo;
    kit::LegatoTracker tracker;
    int iLegato { 0 };
};

//==============================================================================
// Aquanova - Oscillator
//==============================================================================
static const std::vector<HwEntry> kOscTable {
    { P::Osc1Type,         "type",      "Type",      0, 2, false },
    { P::Osc1Octave,       "octave",    "Octave",    0, 1, false },
    { P::Osc1Semitone,     "semitone",  "Semitone",  0, 1, false },
    { P::Osc1FineTune,     "fine",      "Fine",      0, 1, false },
    { P::Osc1MixLevel,     "level",     "Level",     1, 1, false },
    { P::Osc1PulseWidth,   "pw",        "Width",     1, 1, false },
    { P::Osc1Sync,         "sync",      "Sync",      1, 1, false },
    { P::Osc1Soften,       "soften",    "Soften",    1, 1, false },
    { P::Osc1PitchManual,  "pitchMan",  "Pitch",     1, 1, false },
    { P::Osc1KeySync,      "keySync",   "Key Sync",  2, 1, false },
    { P::Osc1SyncSkew,     "syncSkew",  "Skew",      2, 1, false },
    { P::Osc1FormantWidth, "formant",   "Formant",   2, 1, false },
    { P::VCODrift,         "drift",     "Drift",     2, 1, true },
    { P::OscsStartPhase,   "startPhase", "Start Ph", 2, 1, true },

    { P::Osc1MixEnv2,  "mixEnv2",  "Mix Env2",  3, 1, false },
    { P::Osc1MixEnv3,  "mixEnv3",  "Mix Env3",  3, 1, false },
    { P::Osc1MixLFO1,  "mixLfo1",  "Mix LFO1",  3, 1, false },
    { P::Osc1MixLFO2,  "mixLfo2",  "Mix LFO2",  3, 1, false },
    { P::Osc1MixWheel, "mixWheel", "Mix Whl",   3, 1, false },

    { P::Osc1PitchEnv2,  "pitchEnv2",  "Pch Env2", 4, 1, false },
    { P::Osc1PitchEnv3,  "pitchEnv3",  "Pch Env3", 4, 1, false },
    { P::Osc1PitchLFO1,  "pitchLfo1",  "Pch LFO1", 4, 1, false },
    { P::Osc1PitchLFO2,  "pitchLfo2",  "Pch LFO2", 4, 1, false },
    { P::Osc1PitchWheel, "pitchWheel", "Pch Whl",  4, 1, false },

    { P::Osc1WidthEnv2,  "widthEnv2",  "PW Env2", 5, 1, false },
    { P::Osc1WidthEnv3,  "widthEnv3",  "PW Env3", 5, 1, false },
    { P::Osc1WidthLFO1,  "widthLfo1",  "PW LFO1", 5, 1, false },
    { P::Osc1WidthLFO2,  "widthLfo2",  "PW LFO2", 5, 1, false },
    { P::Osc1WidthWheel, "widthWheel", "PW Whl",  5, 1, false },

    { P::Osc1SyncEnv2,  "syncEnv2",  "Sync Env2", 6, 1, false },
    { P::Osc1SyncEnv3,  "syncEnv3",  "Sync Env3", 6, 1, false },
    { P::Osc1SyncLFO1,  "syncLfo1",  "Sync LFO1", 6, 1, false },
    { P::Osc1SyncLFO2,  "syncLfo2",  "Sync LFO2", 6, 1, false },
    { P::Osc1SyncWheel, "syncWheel", "Sync Whl",  6, 1, false },

    { P::Osc1SoftenEnv2,    "softEnv2",  "Soft Env2", 7, 1, false },
    { P::Osc1HardnessEnv3,  "softEnv3",  "Soft Env3", 7, 1, false },
    { P::Osc1HardnessLFO1,  "softLfo1",  "Soft LFO1", 7, 1, false },
    { P::Osc1HardnessLFO2,  "softLfo2",  "Soft LFO2", 7, 1, false },
    { P::Osc1HardnessWheel, "softWheel", "Soft Whl",  7, 1, false },

    { P::UnisonVoices,    "unison",      "Unison",     8, 1, true },
    { P::UnisonDetune,    "uniDetune",   "Uni Detune", 8, 1, true },
    { P::PortamentoTime,  "portaTime",   "Porta",      8, 1, true },
    { P::PortamentoGlide, "portaGlide",  "Porta Exp",  8, 1, true },
    { P::PortamentoType,  "portaType",   "Gliss",      8, 1, true },
    { P::GlideType,       "glideType",   "Glide Type", 9, 2, true },
    { P::Osc1BendRange,   "bendRange",   "Bend",       9, 1, true },
};

class AquanovaOscModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("osc", "Oscillator", ModuleSection::Oscillator, 0, kOscTable);
        d.description = "One Aquanova oscillator: square (with width), saw, double saw or the Audio In, with "
                        "sync and its skew and formant, soften, drift - and the hardware's amounts for Env 2, "
                        "Env 3, LFO 1, LFO 2 and the wheel on level, pitch, width, sync and softness. FM In "
                        "phase-modulates it (oscillator 3's FM). Unison stacks detuned copies.";
        d.sockets = modInputs();
        d.sockets.push_back (audioIn ("fmIn", "FM In"));
        d.sockets.push_back (audioIn ("extIn", "Ext In"));
        d.sockets.push_back (midiIn ("addMidiIn", "Add Midi In"));
        d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
        d.sockets.push_back (audioOut ("waveOut", "Wave Out"));
        d.sockets.push_back (modOut ("levelOut", "Level Out"));
        addVoiceControls (d, 9);
        return d;
    }

    const std::vector<HwEntry>& table() const override { return kOscTable; }
    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    double voiceTailSeconds() const override { return 0.05; }

    void prepare (double sr) override
    {
        HwModule::prepare (sr);
        for (auto& vs : voices) { for (auto& o : vs.osc) o.prepare (sr); vs.rng.setSeed (juce::Random::getSystemRandom().nextInt64()); }
        iLegato = paramIndex ("legato");
        pool.resetAll();
        tracker.reset();
    }

    void reset() override
    {
        for (auto& vs : voices) for (auto& o : vs.osc) o.reset();
        pool.resetAll();
        tracker.reset();
    }

    void voiceReset (int v) override { pool.resetVoice (v); voices[(size_t) v].held = false; voices[(size_t) v].sounding = false; }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& vs = voices[(size_t) v];
        const int from = tracker.noteOn (v, param (iLegato) >= 0.5f);
        bool wasSounding = vs.held && vs.sounding;
        if (from >= 0)
        {
            const auto& src = voices[(size_t) from];
            for (int c = 0; c < kMaxUnison; ++c) vs.osc[(size_t) c] = src.osc[(size_t) c];
            vs.glideCurrent = src.glideCurrent;
            wasSounding = true;
        }

        vs.note = note;
        vs.held = true;
        vs.sounding = true;
        vs.uni.set (raw (P::UnisonVoices), uni (P::UnisonDetune));
        vs.glideTarget = (float) note;

        static const int fixedSemis[10] = { 0, 0, -2, 2, -5, 5, -7, 7, -12, 12 };
        const int portTime = raw (P::PortamentoTime), glideType = raw (P::GlideType);
        if (portTime == 0)                              vs.glideCurrent = vs.glideTarget;
        else if (glideType >= 2 && glideType <= 9)      vs.glideCurrent = vs.glideTarget + (float) fixedSemis[glideType];
        else if (glideType == 1 && ! wasSounding)       vs.glideCurrent = vs.glideTarget;

        const int startPhase = raw (P::OscsStartPhase);
        for (int c = 0; c < vs.uni.count; ++c)
        {
            if (startPhase > 0)              vs.osc[(size_t) c].setStartPhase ((float) startPhase / 128.0f);
            else if (raw (P::Osc1KeySync))   vs.osc[(size_t) c].setStartPhase (0.0f);
            vs.driftPhase[c] = vs.rng.nextFloat();
        }

        pool.noteOn (v, voiceLimit());
    }

    void voiceNoteOff (int v) override
    {
        tracker.noteOff();
        voices[(size_t) v].held = false;
        pool.noteOff (v, voiceLimit());
    }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& vs = voices[(size_t) v];
        const float e2 = in[inEnv2][0], e3 = in[inEnv3][0], lfoA = in[inLfo1][0], lfoB = in[inLfo2][0];
        const float wh = wheel();

        // --- glide (Voice::glideTowardsTarget)
        float glide = vs.glideTarget;
        {
            const int t = raw (P::PortamentoTime);
            if (t == 0)
                vs.glideCurrent = vs.glideTarget;
            else
            {
                const float seconds = 0.002f * std::pow (5000.0f, (float) t / 127.0f);
                const float diff = vs.glideTarget - vs.glideCurrent;
                if (raw (P::PortamentoGlide) == 1)
                    vs.glideCurrent += diff * (1.0f - std::exp (-1.0f / juce::jmax (1.0f, seconds * (float) sampleRate)));
                else
                {
                    const float step = 1.0f / juce::jmax (1.0f, seconds * (float) sampleRate);
                    vs.glideCurrent += juce::jlimit (-std::abs (diff), std::abs (diff), (diff >= 0.0f ? 1.0f : -1.0f) * step * 24.0f);
                }
            }
            glide = raw (P::PortamentoType) == 1 ? std::round (vs.glideCurrent) : vs.glideCurrent;
        }

        const float bend = midiControls != nullptr ? midiControls->bend * (float) (raw (P::Osc1BendRange) - 12) : 0.0f;

        float semis = (float) (raw (P::Osc1Octave) - 2) * 12.0f
                    + (float) (raw (P::Osc1Semitone) - 12)
                    + (float) (raw (P::Osc1FineTune) - 64) / 64.0f
                    + bip (P::Osc1PitchManual) * 12.0f;
        semis += bip (P::Osc1PitchEnv2) * e2 * 24.0f + bip (P::Osc1PitchEnv3) * e3 * 24.0f
               + bip (P::Osc1PitchLFO1) * lfoA * 12.0f + bip (P::Osc1PitchLFO2) * lfoB * 12.0f
               + bip (P::Osc1PitchWheel) * wh * 12.0f;

        const float pw = juce::jlimit (0.02f, 0.98f, uni (P::Osc1PulseWidth)
                       + bip (P::Osc1WidthEnv2) * e2 + bip (P::Osc1WidthEnv3) * e3
                       + bip (P::Osc1WidthLFO1) * lfoA + bip (P::Osc1WidthLFO2) * lfoB + bip (P::Osc1WidthWheel) * wh);
        const float sync = juce::jlimit (0.0f, 1.0f, uni (P::Osc1Sync)
                         + bip (P::Osc1SyncEnv2) * e2 + bip (P::Osc1SyncEnv3) * e3
                         + bip (P::Osc1SyncLFO1) * lfoA + bip (P::Osc1SyncLFO2) * lfoB + bip (P::Osc1SyncWheel) * wh);
        const float soften = juce::jlimit (0.0f, 1.0f, uni (P::Osc1Soften)
                           + bip (P::Osc1SoftenEnv2) * e2 + bip (P::Osc1HardnessEnv3) * e3
                           + bip (P::Osc1HardnessLFO1) * lfoA + bip (P::Osc1HardnessLFO2) * lfoB + bip (P::Osc1HardnessWheel) * wh);
        const float level = juce::jlimit (0.0f, 1.0f, uni (P::Osc1MixLevel)
                          + bip (P::Osc1MixEnv2) * e2 + bip (P::Osc1MixEnv3) * e3
                          + bip (P::Osc1MixLFO1) * lfoA + bip (P::Osc1MixLFO2) * lfoB + bip (P::Osc1MixWheel) * wh);

        const int type = raw (P::Osc1Type);
        const bool fm = isInputConnected (4);
        const float fmIn = 0.5f * (in[4][0] + in[4][1]);
        const float driftAmt = uni (P::VCODrift) * 0.04f;
        const float g = pool.nextGain (v, sampleRate);

        float wave = 0.0f, l = 0.0f, r = 0.0f;
        for (int c = 0; c < vs.uni.count; ++c)
        {
            // a slow random walk for the analogue drift (per stacked copy)
            vs.driftPhase[c] += 0.6f / (float) sampleRate;
            if (vs.driftPhase[c] >= 1.0f) { vs.driftPhase[c] -= 1.0f; vs.drift[c] = vs.rng.nextFloat() * 2.0f - 1.0f; }

            const float note = glide + bend + vs.uni.cents[c] * 0.01f + semis + driftAmt * vs.drift[c];
            const float hz = 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);

            float x;
            if (type == AQ::Oscillator::AudioIn1)      x = in[5][0];
            else if (type == AQ::Oscillator::AudioIn2) x = in[5][1];
            else x = vs.osc[(size_t) c].process (hz, type, pw, sync, uni (P::Osc1SyncSkew), uni (P::Osc1FormantWidth), soften);

            if (fm)
                x = std::tanh (x + fmIn * 0.7f);

            wave += x * vs.uni.gain;
            l += x * vs.uni.gainL[c];
            r += x * vs.uni.gainR[c];
        }

        out[0] = { l * level * g, r * level * g };     // Audio Out: level and the stack's panning
        out[1] = { wave * g, wave * g };               // Wave Out: the plain wave (FM, ring mod, vocoder)
        out[2] = { level * g, 0.0f };                  // Level Out: into the Mixer's Level In
    }

private:
    struct VoiceState
    {
        std::array<AQ::Oscillator, kMaxUnison> osc;
        Unison uni;
        float drift[kMaxUnison] {}, driftPhase[kMaxUnison] {};
        float glideCurrent { 60.0f }, glideTarget { 60.0f };
        int note { 60 };
        bool held { false }, sounding { false };
        juce::Random rng;
    };

    std::array<VoiceState, kMaxVoices> voices;
    ModuleVoicePool pool;
    kit::LegatoTracker tracker;
    int iLegato { 0 };
};

//==============================================================================
// Aquanova - Noise
//==============================================================================
static const std::vector<HwEntry> kNoiseTable {
    { P::NoiseMixLevel, "level",    "Level",    0, 1, false },
    { P::NoiseSoften,   "soften",   "Soften",   0, 1, false },
    { P::UnisonVoices,  "unison",   "Unison",   0, 1, true },
    { P::NoiseMixEnv2,  "mixEnv2",  "Mix Env2", 1, 1, false },
    { P::NoiseMixEnv3,  "mixEnv3",  "Mix Env3", 1, 1, false },
    { P::NoiseMixLFO1,  "mixLfo1",  "Mix LFO1", 1, 1, false },
    { P::NoiseMixLFO2,  "mixLfo2",  "Mix LFO2", 1, 1, false },
    { P::NoiseMixWheel, "mixWheel", "Mix Whl",  1, 1, false },
};

class AquanovaNoiseModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("noise", "Noise", ModuleSection::Oscillator, 1, kNoiseTable);
        d.description = "Aquanova's noise source, with its soften (a sweepable colour) and level amounts for "
                        "Env 2, Env 3, LFO 1, LFO 2 and the wheel. Wave Out carries the noise to oscillator "
                        "3's FM In for the hardware's FM Noise.";
        d.sockets = modInputs();
        d.sockets.push_back (midiIn ("addMidiIn", "Add Midi In"));
        d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
        d.sockets.push_back (audioOut ("waveOut", "Wave Out"));
        d.sockets.push_back (modOut ("levelOut", "Level Out"));
        d.params.push_back (makeSteppedList ("voices", "Voices", voiceList(), 15, 2).noMod());
        return d;
    }

    const std::vector<HwEntry>& table() const override { return kNoiseTable; }
    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        HwModule::prepare (sr);
        for (auto& vs : voices) for (auto& n : vs.noise) n.prepare (sr);
        pool.resetAll();
    }
    void reset() override { pool.resetAll(); }
    void voiceReset (int v) override { pool.resetVoice (v); }
    void voiceNoteOn (int v, int, bool) override
    {
        voices[(size_t) v].uni.set (raw (P::UnisonVoices), 0.0f);
        pool.noteOn (v, voiceLimit());
    }
    void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& vs = voices[(size_t) v];
        const float level = juce::jlimit (0.0f, 1.0f, uni (P::NoiseMixLevel)
                          + bip (P::NoiseMixEnv2) * in[inEnv2][0] + bip (P::NoiseMixEnv3) * in[inEnv3][0]
                          + bip (P::NoiseMixLFO1) * in[inLfo1][0] + bip (P::NoiseMixLFO2) * in[inLfo2][0]
                          + bip (P::NoiseMixWheel) * wheel());
        const float g = pool.nextGain (v, sampleRate);
        float wave = 0.0f, l = 0.0f, r = 0.0f;
        for (int c = 0; c < vs.uni.count; ++c)
        {
            const float x = vs.noise[(size_t) c].process (uni (P::NoiseSoften));
            wave += x * vs.uni.gain;
            l += x * vs.uni.gainL[c];
            r += x * vs.uni.gainR[c];
        }
        out[0] = { l * level * g, r * level * g };
        out[1] = { wave * g, wave * g };
        out[2] = { level * g, 0.0f };
    }

private:
    struct VoiceState { std::array<AQ::NoiseSource, kMaxUnison> noise; Unison uni; };
    std::array<VoiceState, kMaxVoices> voices;
    ModuleVoicePool pool;
};

//==============================================================================
// Aquanova - Ring Mod
//==============================================================================
static const std::vector<HwEntry> kRingTable {
    { P::Ring1x3Level, "ring1",      "1x3 Level", 0, 1, false },
    { P::Ring1x3Env2,  "ring1Env2",  "1x3 Env2",  0, 1, false },
    { P::Ring1x3Env3,  "ring1Env3",  "1x3 Env3",  0, 1, false },
    { P::Ring1x3LFO1,  "ring1Lfo1",  "1x3 LFO1",  0, 1, false },
    { P::Ring1x3LFO2,  "ring1Lfo2",  "1x3 LFO2",  0, 1, false },
    { P::Ring1x3Wheel, "ring1Wheel", "1x3 Whl",   1, 1, false },
    { P::Ring2x3Level, "ring2",      "2x3 Level", 2, 1, false },
    { P::Ring2x3Env2,  "ring2Env2",  "2x3 Env2",  2, 1, false },
    { P::Ring2x3Env3,  "ring2Env3",  "2x3 Env3",  2, 1, false },
    { P::Ring2x3LFO1,  "ring2Lfo1",  "2x3 LFO1",  2, 1, false },
    { P::Ring2x3LFO2,  "ring2Lfo2",  "2x3 LFO2",  2, 1, false },
    { P::Ring2x3Wheel, "ring2Wheel", "2x3 Whl",   3, 1, false },
};

class AquanovaRingModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("ring", "Ring Mod", ModuleSection::Effect, 2, kRingTable);
        d.description = "Aquanova's two ring modulators, oscillator 1 x 3 and 2 x 3, each with its level and "
                        "the usual Env 2 / Env 3 / LFO / wheel amounts. Take the oscillators' Wave Outs in.";
        d.sockets = modInputs();
        d.sockets.push_back (audioIn ("osc1In", "Osc 1 Wave"));
        d.sockets.push_back (audioIn ("osc2In", "Osc 2 Wave"));
        d.sockets.push_back (audioIn ("osc3In", "Osc 3 Wave"));
        d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
        d.sockets.push_back (modOut ("levelOut", "Level Out"));
        return d;
    }

    const std::vector<HwEntry>& table() const override { return kRingTable; }
    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void processVoiceSample (int, const StereoFrame* in, StereoFrame* out) override
    {
        const float e2 = in[inEnv2][0], e3 = in[inEnv3][0], a = in[inLfo1][0], b = in[inLfo2][0], wh = wheel();
        const float r1 = juce::jlimit (0.0f, 1.0f, uni (P::Ring1x3Level) + bip (P::Ring1x3Env2) * e2 + bip (P::Ring1x3Env3) * e3
                                                   + bip (P::Ring1x3LFO1) * a + bip (P::Ring1x3LFO2) * b + bip (P::Ring1x3Wheel) * wh);
        const float r2 = juce::jlimit (0.0f, 1.0f, uni (P::Ring2x3Level) + bip (P::Ring2x3Env2) * e2 + bip (P::Ring2x3Env3) * e3
                                                   + bip (P::Ring2x3LFO1) * a + bip (P::Ring2x3LFO2) * b + bip (P::Ring2x3Wheel) * wh);
        const float o1 = in[4][0], o2 = in[5][0], o3 = in[6][0];
        // centre-panned like a single Aquanova voice (cos / sin of a quarter pi)
        const float y = (o1 * o3 * r1 + o2 * o3 * r2) * 0.70710678f;
        out[0] = { y, y };
        out[1] = { r1 + r2, 0.0f };
    }
};

//==============================================================================
// Aquanova - Mixer
//==============================================================================
class AquanovaMixerModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.aquanova.mixer";
        d.displayName = kit::moduleName (kPlugin, "Mixer");
        d.description = "Aquanova's mixer: every source's Audio Out into Audio In and its Level Out into Level "
                        "In, so the sum is kept from overloading the filter the way the hardware's mixer does "
                        "(scaled by 0.9 / the summed levels once they pass 1).";
        d.section = ModuleSection::Utility;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 403;
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("levelIn", "Level In"), audioOut ("audioOut", "Audio Out") };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    void processVoiceSample (int, const StereoFrame* in, StereoFrame* out) override { mix (in, out); }
    void processSample (const StereoFrame* in, StereoFrame* out) override { mix (in, out); }

private:
    static void mix (const StereoFrame* in, StereoFrame* out)
    {
        const float k = 0.9f / juce::jmax (1.0f, in[1][0]);
        out[0] = { in[0][0] * k, in[0][1] * k };
    }
};

//==============================================================================
// Aquanova - Filter
//==============================================================================
static const std::vector<HwEntry> kFilterTable {
    { P::FilterType,      "type",      "Type",      0, 2, false },
    { P::FilterCutoff,    "cutoff",    "Cutoff",    0, 1, false },
    { P::FilterResonance, "resonance", "Reso",      0, 1, false },
    { P::FilterSlope,     "slope",     "Slope",     0, 1, false },
    { P::FilterTracking,  "tracking",  "Key Track", 1, 1, false },
    { P::FilterOverdrive, "overdrive", "Drive",     1, 1, false },
    { P::FilterOverdriveCurve, "odCurve", "Drv Curve", 1, 1, false },
    { P::FilterQNormalise, "qNorm",    "Q Norm",    1, 1, false },
    { P::FilterWidth,     "width",     "Width",     1, 1, false },
    { P::FilterOscsBypass, "bypass",   "Bypass",    2, 1, false },

    { P::FilterFreqEnv2,  "freqEnv2",  "Frq Env2",  3, 1, false },
    { P::FilterFreqEnv3,  "freqEnv3",  "Frq Env3",  3, 1, false },
    { P::FilterFreqLFO1,  "freqLfo1",  "Frq LFO1",  3, 1, false },
    { P::FilterFreqLFO2,  "freqLfo2",  "Frq LFO2",  3, 1, false },
    { P::FilterFreqWheel, "freqWheel", "Frq Whl",   3, 1, false },
    { P::FilterFreqAftertouch, "freqAT", "Frq AT",  2, 1, false },

    { P::FilterResEnv2,  "resEnv2",  "Res Env2", 4, 1, false },
    { P::FilterResEnv3,  "resEnv3",  "Res Env3", 4, 1, false },
    { P::FilterResLFO1,  "resLfo1",  "Res LFO1", 4, 1, false },
    { P::FilterResLFO2,  "resLfo2",  "Res LFO2", 4, 1, false },
    { P::FilterResWheel, "resWheel", "Res Whl",  4, 1, false },
    { P::FilterResAftertouch, "resAT", "Res AT", 2, 1, false },
};

class AquanovaFilterModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("filter", "Filter", ModuleSection::Filter, 4, kFilterTable);
        d.description = "Aquanova's filter: twelve types including the dual ones, 12-48 dB slopes, overdrive "
                        "with its curve, key tracking, Q normalise and width, with the hardware's amounts for "
                        "Env 2, Env 3, LFO 1, LFO 2, wheel and aftertouch on cutoff and resonance.";
        d.sockets = { audioIn ("audioIn", "Audio In") };
        for (auto& s : modInputs()) d.sockets.push_back (s);
        d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
        return d;
    }

    const std::vector<HwEntry>& table() const override { return kFilterTable; }
    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        HwModule::prepare (sr);
        for (auto& f : filters) { f[0].prepare (sr); f[1].prepare (sr); }
    }
    void reset() override { for (auto& f : filters) { f[0].reset(); f[1].reset(); } }
    void voiceNoteOn (int v, int note, bool) override { notes[(size_t) v] = note; }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        const float e2 = in[1][0], e3 = in[2][0], a = in[3][0], b = in[4][0], wh = wheel(), at = aftertouch();
        float cutoffSemis = (float) raw (P::FilterCutoff) * (132.0f / 127.0f);
        cutoffSemis += bip (P::FilterFreqEnv2) * e2 * 96.0f + bip (P::FilterFreqEnv3) * e3 * 96.0f
                     + bip (P::FilterFreqLFO1) * a * 48.0f + bip (P::FilterFreqLFO2) * b * 48.0f
                     + bip (P::FilterFreqWheel) * wh * 48.0f + bip (P::FilterFreqAftertouch) * at * 48.0f
                     + bip (P::FilterTracking) * (float) (notes[(size_t) v] - 60);
        const float cutoffHz = juce::jlimit (15.0f, (float) sampleRate * 0.48f, 8.1758f * std::pow (2.0f, cutoffSemis / 12.0f));
        const float res = juce::jlimit (0.0f, 1.0f, uni (P::FilterResonance)
                        + bip (P::FilterResEnv2) * e2 + bip (P::FilterResEnv3) * e3 + bip (P::FilterResLFO1) * a
                        + bip (P::FilterResLFO2) * b + bip (P::FilterResWheel) * wh + bip (P::FilterResAftertouch) * at);

        const float bypass = uni (P::FilterOscsBypass);
        for (int c = 0; c < 2; ++c)
        {
            const float x = in[0][(size_t) c];
            float y = filters[(size_t) v][(size_t) c].process (x, cutoffHz, res, raw (P::FilterType), raw (P::FilterSlope),
                                                              uni (P::FilterQNormalise), uni (P::FilterOverdrive),
                                                              uni (P::FilterWidth), uni (P::FilterOverdriveCurve));
            if (bypass > 0.0f)
                y = y * (1.0f - bypass) + x * bypass;
            out[0][(size_t) c] = y;
        }
    }

private:
    std::array<std::array<AQ::Filter, 2>, kMaxVoices> filters;
    std::array<int, kMaxVoices> notes {};
};

//==============================================================================
// Aquanova - Amp
//==============================================================================
class AquanovaAmpModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.aquanova.amp";
        d.displayName = kit::moduleName (kPlugin, "Amp");
        d.description = "Aquanova's voice amplifier: the filtered voice times Env 1 (which already carries "
                        "the velocity).";
        d.section = ModuleSection::Utility;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 405;
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("envIn", "Env 1"), audioOut ("audioOut", "Audio Out") };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void processVoiceSample (int, const StereoFrame* in, StereoFrame* out) override
    {
        out[0] = { in[0][0] * in[1][0], in[0][1] * in[1][0] };
    }
};

//==============================================================================
// The effects
//==============================================================================
namespace
{
    // a global effects stage over hardware parameters
    template <typename Derived>
    class HwFxModule : public HwModule
    {
    public:
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
    };

    ModuleDescriptor fxDescriptor (const juce::String& id, const juce::String& kind, int order,
                                   const std::vector<HwEntry>& t, const juce::String& description,
                                   bool chainIn = false)
    {
        auto d = baseDescriptor (id, kind, ModuleSection::Effect, order, t);
        d.description = description;
        d.sockets = { audioIn ("audioIn", "Audio In") };
        if (chainIn) d.sockets.push_back (audioIn ("chainIn", "Chain In"));
        d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
        return d;
    }

    double fxBpm (double bpm) { return juce::jmax (20.0, bpm); }
}

static const std::vector<HwEntry> kVocoderTable {
    { P::VocoderBalance,    "balance",   "Balance",   0, 1, false },
    { P::VocoderSpeed,      "speed",     "Speed",     0, 1, false },
    { P::VocoderWidth,      "width",     "Width",     0, 1, false },
    { P::VocSibilanceLevel, "sibLevel",  "Sibilance", 0, 1, false },
    { P::VocSibilanceType,  "sibType",   "Sib Type",  0, 1, false },
    { P::VocoderModSource,  "modSource", "Modulator", 1, 2, false },
    { P::VocoderAudioInput, "audioInput", "Audio In", 1, 2, false },
};

class AquanovaVocoderModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = fxDescriptor ("vocoder", "Vocoder", 20, kVocoderTable,
            "Aquanova's 42-band vocoder, first in its effects chain. Its modulator is the oscillator "
            "Wave Outs patched into Mod In, or (Modulator = Audio In) Ext In. Also carries the DC blocker "
            "that sits in front of the effects.");
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("modIn", "Mod In"), audioIn ("extIn", "Ext In"),
                      audioOut ("audioOut", "Audio Out") };
        d.params.push_back (makeCombo ("dcBlock", "DC Block", { "Off", "On" }, 1, 2, 1));
        d.params.push_back (makeRotary ("dcSpeed", "DC Speed", 1.0f, 5.0f, 1.0f, 2, "Hz"));
        return d;
    }

    const std::vector<HwEntry>& table() const override { return kVocoderTable; }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        HwModule::prepare (sr);
        vocoder.prepare (sr);
        dc.prepare (sr);
        iDc = paramIndex ("dcBlock"); iDcSpeed = paramIndex ("dcSpeed");
    }
    void reset() override { vocoder.reset(); dc.reset(); }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        float l = in[0][0], r = in[0][1];
        if (param (iDc) >= 0.5f)
        {
            dc.setCornerHz (param (iDcSpeed));
            dc.process (&l, &r, 1);
        }

        const float balance = uni (P::VocoderBalance);
        if (balance > 0.001f)
        {
            const int src = raw (P::VocoderModSource);
            float mod;
            if (src == 0)
                mod = raw (P::VocoderAudioInput) != 0 ? in[2][1] : in[2][0];
            else
                mod = (src >= 4 && src <= 6 ? 0.7f : 1.0f) * 0.5f * (in[1][0] + in[1][1]);
            vocoder.process (l, r, mod, balance, 1.0f - uni (P::VocoderSpeed),
                             (float) raw (P::VocoderWidth) / 15.0f, (float) raw (P::VocSibilanceLevel) / 15.0f,
                             raw (P::VocSibilanceType) != 0);
        }
        out[0] = { l, r };
    }

private:
    AQ::Vocoder vocoder;
    AQ::DcBlocker dc;
    int iDc { 0 }, iDcSpeed { 0 };
};

static const std::vector<HwEntry> kEqTable {
    { P::EQBass,   "bass",   "Bass",   0, 1, false },
    { P::EQTreble, "treble", "Treble", 0, 1, false },
};
class AquanovaEqModule final : public HwFxModule<AquanovaEqModule>
{
public:
    static ModuleDescriptor descriptor() { return fxDescriptor ("eq", "EQ", 21, kEqTable, "Aquanova's bass and treble shelves."); }
    const std::vector<HwEntry>& table() const override { return kEqTable; }
    void prepare (double sr) override { HwModule::prepare (sr); eq.prepare (sr); }
    void reset() override { eq.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const auto s = eq.process ({ in[0][0], in[0][1] }, bip (P::EQBass), bip (P::EQTreble));
        out[0] = { s.l, s.r };
    }
private:
    AQ::EqSection eq;
};

static const std::vector<HwEntry> kDistTable {
    { P::DistortionLevel,    "level",    "Drive",     0, 1, false },
    { P::DistortionCurve,    "curve",    "Curve",     0, 1, false },
    { P::DistortionGainComp, "gainComp", "Gain Comp", 0, 1, false },
    { P::DistortionOutput,   "output",   "Output",    0, 1, false },
};
class AquanovaDistortionModule final : public HwFxModule<AquanovaDistortionModule>
{
public:
    static ModuleDescriptor descriptor() { return fxDescriptor ("distortion", "Distortion", 22, kDistTable, "Aquanova's distortion, with its curve, gain compensation and output level."); }
    const std::vector<HwEntry>& table() const override { return kDistTable; }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const auto s = dist.process ({ in[0][0], in[0][1] }, uni (P::DistortionLevel), uni (P::DistortionOutput),
                                     uni (P::DistortionGainComp), bip (P::DistortionCurve));
        out[0] = { s.l, s.r };
    }
private:
    AQ::Distortion dist;
};

static const std::vector<HwEntry> kCombTable {
    { P::CombFrequency, "freq",   "Freq",   0, 1, false },
    { P::CombBoost,     "boost",  "Boost",  0, 1, false },
    { P::CombSpeed,     "speed",  "Speed",  0, 1, false },
    { P::CombDepth,     "depth",  "Depth",  0, 1, false },
    { P::CombSpread,    "spread", "Spread", 0, 1, false },
};
class AquanovaCombModule final : public HwFxModule<AquanovaCombModule>
{
public:
    static ModuleDescriptor descriptor() { return fxDescriptor ("comb", "Comb", 23, kCombTable, "Aquanova's comb filter, swept by its own LFO (Speed, Depth) with stereo spread."); }
    const std::vector<HwEntry>& table() const override { return kCombTable; }
    void prepare (double sr) override { HwModule::prepare (sr); comb.prepare (sr); }
    void reset() override { comb.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const auto s = comb.process ({ in[0][0], in[0][1] }, uni (P::CombFrequency), uni (P::CombBoost),
                                     uni (P::CombSpeed), uni (P::CombDepth), uni (P::CombSpread));
        out[0] = { s.l, s.r };
    }
private:
    AQ::CombSection comb;
};

static const std::vector<HwEntry> kChorusTable {
    { P::ChorusType,        "type",     "Type",     0, 2, false },
    { P::ChorusSendLevel,   "send",     "Send",     0, 1, false },
    { P::ChorusSpeed,       "speed",    "Speed",    0, 1, false },
    { P::ChorusModDepth,    "depth",    "Depth",    0, 1, false },
    { P::ChorusFeedback,    "feedback", "Feedback", 1, 1, false },
    { P::ChorusDelay,       "delay",    "Delay",    1, 1, false },
    { P::ChorusStereoWidth, "width",    "Width",    1, 1, false },
};
class AquanovaChorusModule final : public HwFxModule<AquanovaChorusModule>
{
public:
    static ModuleDescriptor descriptor() { return fxDescriptor ("chorus", "Chorus", 24, kChorusTable, "Aquanova's chorus / flanger / phaser send. Out is the effect alone - it goes into the Effects Mix (and the Delay's Chain In in the chorus-into-delay configurations)."); }
    const std::vector<HwEntry>& table() const override { return kChorusTable; }
    void prepare (double sr) override { HwModule::prepare (sr); chorus.prepare (sr, 512); }
    void reset() override { chorus.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const auto s = chorus.process ({ in[0][0], in[0][1] }, uni (P::ChorusSendLevel), uni (P::ChorusSpeed),
                                       uni (P::ChorusModDepth), uni (P::ChorusFeedback), uni (P::ChorusDelay),
                                       uni (P::ChorusStereoWidth), raw (P::ChorusType));
        out[0] = { s.l, s.r };
    }
private:
    AQ::ChorusSection chorus;
};

static const std::vector<HwEntry> kDelayTable {
    { P::DelaySendLevel, "send",     "Send",     0, 1, false },
    { P::DelayTime,      "time",     "Time",     0, 1, false },
    { P::DelaySync,      "sync",     "Sync",     0, 1, false },
    { P::DelayFeedback,  "feedback", "Feedback", 0, 1, false },
    { P::DelayHFDamp,    "damp",     "HF Damp",  1, 1, false },
    { P::DelayWidth,     "width",    "Width",    1, 1, false },
    { P::DelayRatio,     "ratio",    "Ratio",    1, 2, false },
};
class AquanovaDelayModule final : public HwFxModule<AquanovaDelayModule>
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = fxDescriptor ("delay", "Delay", 25, kDelayTable,
            "Aquanova's stereo delay send, synced or free, with HF damping, width and the L:R ratio. Chain In "
            "is what the Fx Config feeds into it (the chorus, scaled by Chain - the Config Morph).", true);
        d.params.push_back (makeRotary ("chain", "Chain", 0.0f, 127.0f, 0.0f, 1, {}, false, 1.0f));
        return d;
    }
    const std::vector<HwEntry>& table() const override { return kDelayTable; }
    void prepare (double sr) override { HwModule::prepare (sr); delay.prepare (sr); iChain = paramIndex ("chain"); }
    void reset() override { delay.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int sync = raw (P::DelaySync);
        double seconds = 0.002 + (double) raw (P::DelayTime) / 127.0 * 2.4;
        if (sync > 0)
            seconds = 1.0 / (double) syncedLfoHz (sync, fxBpm (tempoBpm));
        const float chain = param (iChain) / 127.0f;
        const AQ::StereoSample x { in[0][0] + in[1][0] * chain, in[0][1] + in[1][1] * chain };
        const auto s = delay.process (x, uni (P::DelaySendLevel), (float) seconds, uni (P::DelayFeedback),
                                      uni (P::DelayHFDamp), uni (P::DelayWidth), raw (P::DelayRatio));
        out[0] = { s.l, s.r };
    }
private:
    AQ::DelaySection delay;
    int iChain { 0 };
};

static const std::vector<HwEntry> kReverbTable {
    { P::ReverbType,      "type",  "Type",     0, 2, false },
    { P::ReverbSendLevel, "send",  "Send",     0, 1, false },
    { P::ReverbDecay,     "decay", "Decay",    0, 1, false },
    { P::ReverbHFDamp,    "damp",  "HF Damp",  0, 1, false },
    { P::ReverbEarlyRef,  "early", "Early Ref", 1, 1, false },
};
class AquanovaReverbModule final : public HwFxModule<AquanovaReverbModule>
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = fxDescriptor ("reverb", "Reverb", 26, kReverbTable,
            "Aquanova's reverb send, sixteen programs with decay, damping and early reflections. Chain In is "
            "what the Fx Config feeds into it (the delay, scaled by Chain - the Config Morph).", true);
        d.params.push_back (makeRotary ("chain", "Chain", 0.0f, 127.0f, 0.0f, 1, {}, false, 1.0f));
        return d;
    }
    const std::vector<HwEntry>& table() const override { return kReverbTable; }
    void prepare (double sr) override { HwModule::prepare (sr); reverb.prepare (sr); iChain = paramIndex ("chain"); }
    void reset() override { reverb.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float chain = param (iChain) / 127.0f;
        const AQ::StereoSample x { in[0][0] + in[1][0] * chain, in[0][1] + in[1][1] * chain };
        const auto s = reverb.process (x, uni (P::ReverbSendLevel), uni (P::ReverbDecay), uni (P::ReverbHFDamp),
                                       (float) raw (P::ReverbEarlyRef) / 7.0f, raw (P::ReverbType));
        out[0] = { s.l, s.r };
    }
private:
    AQ::ReverbSection reverb;
    int iChain { 0 };
};

static const std::vector<HwEntry> kFxMixTable {
    { P::EffectsDryLevel, "dry", "Dry", 0, 1, false },
};
class AquanovaFxMixModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("fxmix", "Effects Mix", ModuleSection::Effect, 27, kFxMixTable);
        d.description = "Where Aquanova's sends come back: the dry signal at Dry level plus the chorus, delay "
                        "and reverb returns. Reverb Level lifts the reverb in the reverb-into-delay configs.";
        d.sockets = { audioIn ("dryIn", "Dry In"), audioIn ("chorusIn", "Chorus In"), audioIn ("delayIn", "Delay In"),
                      audioIn ("reverbIn", "Reverb In"), audioOut ("audioOut", "Audio Out") };
        d.params.push_back (makeRotary ("reverbLevel", "Reverb Level", 0.0f, 1.5f, 1.0f, 0));
        return d;
    }
    const std::vector<HwEntry>& table() const override { return kFxMixTable; }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { HwModule::prepare (sr); iRev = paramIndex ("reverbLevel"); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float dry = uni (P::EffectsDryLevel), rv = param (iRev);
        for (int c = 0; c < 2; ++c)
            out[0][(size_t) c] = in[0][(size_t) c] * dry + in[1][(size_t) c] + in[2][(size_t) c] + in[3][(size_t) c] * rv;
    }
private:
    int iRev { 0 };
};

static const std::vector<HwEntry> kPanTable {
    { P::Pan,          "pan",   "Pan",   0, 1, false },
    { P::PanType,      "type",  "Type",  0, 2, false },
    { P::PanningSpeed, "speed", "Speed", 0, 1, false },
    { P::PanningDepth, "depth", "Depth", 0, 1, false },
};
class AquanovaPannerModule final : public HwFxModule<AquanovaPannerModule>
{
public:
    static ModuleDescriptor descriptor() { return fxDescriptor ("panner", "Panner", 28, kPanTable, "Aquanova's panner at the end of the effects: a position, or auto-pan / tremolo / left-right sweeps."); }
    const std::vector<HwEntry>& table() const override { return kPanTable; }
    void prepare (double sr) override { HwModule::prepare (sr); pan.prepare (sr); }
    void reset() override { pan.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const auto s = pan.process ({ in[0][0], in[0][1] }, bip (P::Pan), raw (P::PanType), uni (P::PanningSpeed), uni (P::PanningDepth));
        out[0] = { s.l, s.r };
    }
private:
    AQ::PanSection pan;
};

static const std::vector<HwEntry> kOutTable {
    { P::MasterVolume,  "master",  "Master", 0, 1, false },
    { P::ProgramVolume, "program", "Volume", 0, 1, false },
};
class AquanovaOutputModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("output", "Output", ModuleSection::Effect, 29, kOutTable);
        d.description = "The end of Aquanova: master and program volume, the output DC blocker and the limiter.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params.push_back (makeCombo ("dcBlock", "DC Block", { "Off", "On" }, 1, 0, 1));
        d.params.push_back (makeRotary ("dcSpeed", "DC Speed", 1.0f, 5.0f, 1.0f, 0, "Hz"));
        return d;
    }
    const std::vector<HwEntry>& table() const override { return kOutTable; }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { HwModule::prepare (sr); dc.prepare (sr); iDc = paramIndex ("dcBlock"); iDcSpeed = paramIndex ("dcSpeed"); }
    void reset() override { dc.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float vol = uni (P::MasterVolume) * uni (P::ProgramVolume);
        float l = in[0][0] * vol, r = in[0][1] * vol;
        if (param (iDc) >= 0.5f) { dc.setCornerHz (param (iDcSpeed)); dc.process (&l, &r, 1); }
        limiter.process (&l, &r, 1);
        out[0] = { l, r };
    }
private:
    AQ::DcBlocker dc;
    AQ::OutputLimiter limiter;
    int iDc { 0 }, iDcSpeed { 0 };
};

//==============================================================================
// Aquanova - Arpeggiator
//==============================================================================
static const std::vector<HwEntry> kArpTable {
    { P::ArpSpeed,       "speed",    "Speed",   0, 1, false },
    { P::ArpSync,        "sync",     "Sync",    0, 1, false },
    { P::ArpGateTime,    "gate",     "Gate",    0, 1, false },
    { P::ArpOctaveRange, "octaves",  "Octaves", 0, 1, false },
    { 255,               "ordering", "Order",   1, 2, false },
    { 250,               "velocity", "Velocity", 1, 2, false },
    { P::ArpKeysync,     "keysync",  "Internal", 2, 2, false },
    { P::ArpLatch,       "latch",    "Latch",   2, 1, false },
};

class AquanovaArpModule final : public HwModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = baseDescriptor ("arp", "Arpeggiator", ModuleSection::Utility, 30, kArpTable);
        d.description = "Aquanova's arpeggiator: the held notes up, down or as played over 1-4 octaves, at its "
                        "own 64-191 BPM or synced to the host, with gate time and latch. Its Midi Out plays the "
                        "oscillators' and noise's Add Midi In.";
        d.sockets = { midiIn ("midiIn", "Midi In"), midiOut ("midiOut", "Midi Out") };
        return d;
    }

    const std::vector<HwEntry>& table() const override { return kArpTable; }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }
    bool isMidiProcessor() const override { return true; }

    void reset() override { numHeld = numLatched = 0; sounding = -1; step = octave = 0; phase = gateLeft = 0.0; latchActive = false; }

    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent*, int) override
    {
        if (in.isOn)
        {
            add (held, numHeld, in.note);
            if (raw (P::ArpLatch) != 0)
            {
                if (! latchActive) { numLatched = 0; latchActive = true; }
                add (latched, numLatched, in.note);
            }
        }
        else
        {
            remove (held, numHeld, in.note);
            if (numHeld == 0) latchActive = false;
        }
        return 0;
    }
    void midiAllNotesOff() override { numHeld = numLatched = 0; latchActive = false; }

    int midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents) override
    {
        int n = 0;
        const bool useLatch = raw (P::ArpLatch) != 0 && numLatched > 0;
        const int* src = useLatch ? latched : held;
        const int count = useLatch ? numLatched : numHeld;

        if (count == 0)
        {
            if (sounding >= 0 && n < maxEvents) events[n++] = { sounding, 0.0f, false };
            sounding = -1; step = octave = 0;
            return n;
        }

        int notes[128];
        for (int i = 0; i < count; ++i) notes[i] = src[i];
        const int ordering = raw (255);
        if (ordering != 4 && ordering != 5)
        {
            std::sort (notes, notes + count);
            if (ordering == 2 || ordering == 3)
                std::reverse (notes, notes + count);
        }

        const double stepTime = stepSeconds();
        const double dt = 1.0 / sr;
        if (gateLeft > 0.0)
        {
            gateLeft -= dt;
            if (gateLeft <= 0.0 && sounding >= 0 && n < maxEvents)
            {
                events[n++] = { sounding, 0.0f, false };
                sounding = -1;
            }
        }

        phase += dt;
        if (phase >= stepTime)
        {
            phase -= stepTime;
            if (sounding >= 0 && n < maxEvents) { events[n++] = { sounding, 0.0f, false }; sounding = -1; }
            if (step >= count) { step = 0; if (++octave >= raw (P::ArpOctaveRange) + 1) octave = 0; }
            const int note = juce::jlimit (0, 127, notes[step] + octave * 12);
            ++step;
            const int velMode = raw (250);
            const float vel = velMode == 1 ? 1.0f : velMode == 2 ? 0.5f : 0.8f;
            if (n < maxEvents) events[n++] = { note, vel, true };
            sounding = note;
            gateLeft = stepTime * (double) juce::jmax (0.05f, uni (P::ArpGateTime));
        }
        return n;
    }

private:
    double stepSeconds() const
    {
        const int sync = raw (P::ArpSync);
        if (raw (P::ArpKeysync) != 0 || sync <= 0)
            return (60.0 / (64.0 + (double) raw (P::ArpSpeed))) * 0.25;
        return 1.0 / (double) syncedLfoHz (sync, tempoBpm);
    }

    static void add (int* list, int& n, int note)
    {
        for (int i = 0; i < n; ++i) if (list[i] == note) return;
        if (n < 128) list[n++] = note;
    }
    static void remove (int* list, int& n, int note)
    {
        for (int i = 0; i < n; ++i)
            if (list[i] == note) { for (int j = i; j < n - 1; ++j) list[j] = list[j + 1]; --n; return; }
    }

    int held[128] {}, latched[128] {};
    int numHeld { 0 }, numLatched { 0 }, sounding { -1 }, step { 0 }, octave { 0 };
    double phase { 0.0 }, gateLeft { 0.0 };
    bool latchActive { false };
};

AQUANODE_REGISTER_MODULE (AquanovaOscModule,        AquanovaOscModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaNoiseModule,      AquanovaNoiseModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaRingModule,       AquanovaRingModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaMixerModule,      AquanovaMixerModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaFilterModule,     AquanovaFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaAmpModule,        AquanovaAmpModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaEnvModule,        AquanovaEnvModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaLfoModule,        AquanovaLfoModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaVocoderModule,    AquanovaVocoderModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaEqModule,         AquanovaEqModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaDistortionModule, AquanovaDistortionModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaCombModule,       AquanovaCombModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaChorusModule,     AquanovaChorusModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaDelayModule,      AquanovaDelayModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaReverbModule,     AquanovaReverbModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaFxMixModule,      AquanovaFxMixModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaPannerModule,     AquanovaPannerModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaOutputModule,     AquanovaOutputModule::descriptor)
AQUANODE_REGISTER_MODULE (AquanovaArpModule,        AquanovaArpModule::descriptor)

//==============================================================================
// The patch builder
//==============================================================================
namespace aquanode::plugins::aquanova
{
    namespace
    {
        // the original's raw hardware value of a parameter
        int rawOf (juce::AudioProcessor& original, int hw)
        {
            const auto* d = descFor (hw);
            if (d == nullptr) return 0;
            return (int) std::lround (kit::param (original, d->id, (float) d->def));
        }

        // sets every hardware knob of a module from the original, the block
        // offset added to the per-block parameters (Osc 2 = +40, ...)
        void copyHw (kit::Builder& b, int module, juce::AudioProcessor& original,
                     const std::vector<HwEntry>& table, int blockOffset)
        {
            for (const auto& e : table)
            {
                const int hw = e.shared ? e.hw : e.hw + blockOffset;
                b.set (module, e.id, rawToKnob (e.hw, rawOf (original, hw)));
            }
        }
    }

    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        auto raw = [&] (int hw) { return rawOf (original, hw); };

        const bool mono = raw (P::PartPolyphony) == 1;
        const float voicesIdx = mono ? 0.0f : 15.0f;

        // --- modulators ------------------------------------------------------
        int env[3], lfo[2];
        for (int e = 0; e < 3; ++e)
        {
            env[e] = b.add ("plugin.aquanova.env", 1700, e * 300);
            if (e == 0)
            {
                // Env 1 has no delay and keeps its own key tracking etc.
                const int map[][2] = { { P::Env2Attack, P::Env1Attack }, { P::Env2Decay, P::Env1Decay },
                    { P::Env2Sustain, P::Env1Sustain }, { P::Env2Release, P::Env1Release },
                    { P::Env2Velocity, P::Env1Velocity }, { P::Env2KeyTracking, P::Env1KeyTracking },
                    { P::Env2ADRepeat, P::Env1ADRepeat }, { P::Env2SustainTime, P::Env1SustainTime },
                    { P::Env2SustainRate, P::Env1SustainRate } };
                for (const auto& entry : kEnvTable)
                    for (const auto& m : map)
                        if (m[0] == entry.hw)
                            b.set (env[e], entry.id, rawToKnob (m[0], raw (m[1])));
                b.set (env[e], "delay", 0.0f);
                b.set (env[e], "constantGate", rawToKnob (P::ConstantGate, raw (P::ConstantGate)));
                b.set (env[e], "ampEnv", 1.0f);
            }
            else
            {
                copyHw (b, env[e], original, kEnvTable, e == 1 ? 0 : (P::Env3Attack - P::Env2Attack));
            }
            const int trig = raw (P::EnvsTriggering);
            const bool single = (trig & (0x4 >> e)) != 0;
            b.set (env[e], "trigger", single ? 1.0f : 0.0f);
            b.set (env[e], "legato", mono ? 1.0f : 0.0f);
        }
        for (int l = 0; l < 2; ++l)
        {
            lfo[l] = b.add ("plugin.aquanova.lfo", 1400, l * 420);
            copyHw (b, lfo[l], original, kLfoTable, l * P::LfoStride);
            b.set (lfo[l], "legato", mono ? 1.0f : 0.0f);
            b.cable (env[2], "envOut", lfo[l], "env3In");
        }

        auto modWire = [&] (int target)
        {
            b.cable (env[1], "envOut", target, "env2In");
            b.cable (env[2], "envOut", target, "env3In");
            b.cable (lfo[0], "lfoOut", target, "lfo1In");
            b.cable (lfo[1], "lfoOut", target, "lfo2In");
        };

        // --- sources -------------------------------------------------------
        int osc[3];
        for (int o = 0; o < 3; ++o)
        {
            osc[o] = b.add ("plugin.aquanova.osc", 0, o * 900);
            copyHw (b, osc[o], original, kOscTable, o * P::OscStride);
            b.set (osc[o], "voices", voicesIdx);
            b.set (osc[o], "legato", mono ? 1.0f : 0.0f);
            modWire (osc[o]);
        }
        const int noise = b.add ("plugin.aquanova.noise", 320, 1800);
        copyHw (b, noise, original, kNoiseTable, 0);
        b.set (noise, "voices", voicesIdx);
        modWire (noise);

        if (raw (P::FM1x3) != 0) b.cable (osc[0], "waveOut", osc[2], "fmIn");
        if (raw (P::FM2x3) != 0) b.cable (osc[1], "waveOut", osc[2], "fmIn");
        if (raw (P::FMNoise) != 0) b.cable (noise, "waveOut", osc[2], "fmIn");

        const int mixer = b.add ("plugin.aquanova.mixer", 640, 600);
        for (int o = 0; o < 3; ++o)
        {
            b.cable (osc[o], "audioOut", mixer, "audioIn");
            b.cable (osc[o], "levelOut", mixer, "levelIn");
        }
        b.cable (noise, "audioOut", mixer, "audioIn");
        b.cable (noise, "levelOut", mixer, "levelIn");

        auto ringUsed = [&] (int base)
        {
            if (raw (base) > 0) return true;
            for (int k = 1; k <= 5; ++k) if (raw (base + k) != 64) return true;
            return false;
        };
        if (ringUsed (P::Ring1x3Level) || ringUsed (P::Ring2x3Level))
        {
            const int ring = b.add ("plugin.aquanova.ring", 320, 2200);
            copyHw (b, ring, original, kRingTable, 0);
            modWire (ring);
            b.cable (osc[0], "waveOut", ring, "osc1In");
            b.cable (osc[1], "waveOut", ring, "osc2In");
            b.cable (osc[2], "waveOut", ring, "osc3In");
            b.cable (ring, "audioOut", mixer, "audioIn");
            b.cable (ring, "levelOut", mixer, "levelIn");
        }

        // --- filter and amp --------------------------------------------------
        const int filter = b.add ("plugin.aquanova.filter", 920, 600);
        copyHw (b, filter, original, kFilterTable, 0);
        b.cable (mixer, "audioOut", filter, "audioIn");
        modWire (filter);

        const int amp = b.add ("plugin.aquanova.amp", 1220, 600);
        b.cable (filter, "audioOut", amp, "audioIn");
        b.cable (env[0], "envOut", amp, "envIn");

        // --- arpeggiator -----------------------------------------------------
        if (raw (P::ArpEnabled) != 0)
        {
            const int arp = b.add ("plugin.aquanova.arp", -320, 0);
            copyHw (b, arp, original, kArpTable, 0);
            for (int o = 0; o < 3; ++o) b.cable (arp, "midiOut", osc[o], "addMidiIn");
            b.cable (arp, "midiOut", noise, "addMidiIn");
        }

        PatchPorts ports;
        ports.name = "Aquanova";

        // --- the effects -----------------------------------------------------
        int out = b.add ("plugin.aquanova.output", 2000 + 300 * 9, 600);
        copyHw (b, out, original, kOutTable, 0);
        const float dcOn = kit::param (original, AQ::kDcBlockId, 1.0f);
        const float dcHz = kit::param (original, AQ::kDcSpeedId, 1.0f);
        b.set (out, "dcBlock", dcOn); b.set (out, "dcSpeed", dcHz);

        if (raw (P::EffectsBypass) != 0)
        {
            b.cable (amp, "audioOut", out, "audioIn");
        }
        else
        {
            const int x0 = 2000;
            const int voc = b.add ("plugin.aquanova.vocoder", x0, 600);
            copyHw (b, voc, original, kVocoderTable, 0);
            b.set (voc, "dcBlock", dcOn); b.set (voc, "dcSpeed", dcHz);
            b.cable (amp, "audioOut", voc, "audioIn");

            // the modulator taps (P::VocoderModSource: 1-3 one oscillator, 4-6 pairs, 7 noise)
            const int src = raw (P::VocoderModSource);
            if (raw (P::VocoderBalance) > 0)
            {
                switch (src)
                {
                    case 1: b.cable (osc[0], "waveOut", voc, "modIn"); break;
                    case 2: b.cable (osc[1], "waveOut", voc, "modIn"); break;
                    case 3: b.cable (osc[2], "waveOut", voc, "modIn"); break;
                    case 4: b.cable (osc[0], "waveOut", voc, "modIn"); b.cable (osc[1], "waveOut", voc, "modIn"); break;
                    case 5: b.cable (osc[0], "waveOut", voc, "modIn"); b.cable (osc[2], "waveOut", voc, "modIn"); break;
                    case 6: b.cable (osc[1], "waveOut", voc, "modIn"); b.cable (osc[2], "waveOut", voc, "modIn"); break;
                    case 7: b.cable (noise, "waveOut", voc, "modIn"); break;
                    default: ports.audioIn.push_back ({ voc, "extIn" }); break;
                }
            }

            const int eq = b.add ("plugin.aquanova.eq", x0 + 300, 600);       copyHw (b, eq, original, kEqTable, 0);
            const int dist = b.add ("plugin.aquanova.distortion", x0 + 600, 600); copyHw (b, dist, original, kDistTable, 0);
            const int comb = b.add ("plugin.aquanova.comb", x0 + 900, 600);   copyHw (b, comb, original, kCombTable, 0);
            b.cable (voc, "audioOut", eq, "audioIn");
            b.cable (eq, "audioOut", dist, "audioIn");
            b.cable (dist, "audioOut", comb, "audioIn");

            const int chorus = b.add ("plugin.aquanova.chorus", x0 + 1200, 300); copyHw (b, chorus, original, kChorusTable, 0);
            const int delay = b.add ("plugin.aquanova.delay", x0 + 1500, 600);   copyHw (b, delay, original, kDelayTable, 0);
            const int reverb = b.add ("plugin.aquanova.reverb", x0 + 1800, 900); copyHw (b, reverb, original, kReverbTable, 0);
            const int mix = b.add ("plugin.aquanova.fxmix", x0 + 2100, 600);   copyHw (b, mix, original, kFxMixTable, 0);
            for (int m : { chorus, delay, reverb })
                b.cable (comb, "audioOut", m, "audioIn");
            b.cable (comb, "audioOut", mix, "dryIn");
            b.cable (chorus, "audioOut", mix, "chorusIn");
            b.cable (delay, "audioOut", mix, "delayIn");
            b.cable (reverb, "audioOut", mix, "reverbIn");

            // the Fx Config (SynthEngine::runEffects), scaled by the Config Morph
            const int order = raw (P::FxOrder);
            const float morph = (float) raw (P::EffectsMorph);
            const bool chorusIntoDelay = order == 5 || order == 6 || order == 11 || order == 12 || order == 15;
            const bool delayIntoReverb = order == 1 || order == 3 || order == 7 || order == 13;
            const bool reverbIntoDelay = order == 4 || order == 10 || order == 14;
            if (chorusIntoDelay) { b.cable (chorus, "audioOut", delay, "chainIn"); b.set (delay, "chain", morph); }
            if (delayIntoReverb) { b.cable (delay, "audioOut", reverb, "chainIn"); b.set (reverb, "chain", morph); }
            b.set (mix, "reverbLevel", reverbIntoDelay ? 1.0f + morph / 127.0f * 0.5f : 1.0f);

            const int pan = b.add ("plugin.aquanova.panner", x0 + 2400, 600);  copyHw (b, pan, original, kPanTable, 0);
            b.cable (mix, "audioOut", pan, "audioIn");
            b.cable (pan, "audioOut", out, "audioIn");
        }

        // Audio In oscillators
        for (int o = 0; o < 3; ++o)
        {
            const int type = raw (P::Osc1Type + o * P::OscStride);
            if (type == 2 || type == 3)
                ports.audioIn.push_back ({ osc[o], "extIn" });
        }

        ports.audioOut = { { out, "audioOut" } };
        return ports;
    }

    // Aquanova's own loaders: its .aquanova patches and Supernova SysEx
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* nova = dynamic_cast<aqp_aquanova::aquanova::AquaNovaProcessor*> (&original);
        return nova != nullptr && nova->loadPatch (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
