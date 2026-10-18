//==============================================================================
// PhizmOsc - Patch version
//
// PhizmOsc's voice, along its own sections:
//
//   Scanner (the evolution curves A and B, scan style, evo / pos LFOs,
//   keytrack, velocity, the transwave envelope) --Pos A / Pos B-->
//   Osc A (--link: phase + frame jump-->) Osc B
//   Osc A, Osc B + Amp Env A, Amp Env B --> Mixer (A/B mix, ring mod, velocity)
//   --> Filter (per voice, or after the voices like the plugin's default)
//   (voices summed) --> Bus (noise, soft clip) --> [Filter] --> Chorus --> Reverb
//   LFOs: the global pitch LFO and the filter's LFO; Pitch Env per voice.
//
// The modules run PhizmOsc's own code (TranswaveAudioProcessor::
// synthesiseVoice / processBlock) sample by sample, and the oscillators carry
// the plugin's wavetables with them (saved in the patch). Where the patch
// differs: PhizmOsc's filter LFO is held at one value per host block (its
// phase restarts every block); here it runs freely at Pos LFO Rate.
//==============================================================================
#include "PluginPhizmOscConfig.h"

namespace aqp_phizmosc
{
    #include AQP_PHIZMOSC_SRC(PluginProcessor.h)
}

#include "PatchKit.h"
#include "PatchWidgets.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "phizmosc" };
    constexpr int kEvoPoints = 32;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.phizmosc." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    juce::StringArray voiceList()
    {
        juce::StringArray a { "mono" };
        for (int i = 2; i <= kMaxVoices; ++i) a.add (juce::String (i));
        return a;
    }

    // PhizmOsc's shared random source (fastRand), one per module here
    struct FastRand
    {
        unsigned int seed { 123456789 };
        float next() { seed = 214013 * seed + 2531011; return ((seed >> 16) & 0x7FFF) / 32768.f; }
    };

    enum class EnvStage { Idle, Attack, Decay, Sustain, Release };

    // advanceEnvExp: linear attack, exponential decay and release, floor 1e-4
    float advanceEnv (float level, EnvStage& stage, float att, float sus, float decCoeff, float relCoeff, float isr)
    {
        constexpr float kFloor = 1e-4f;
        switch (stage)
        {
            case EnvStage::Attack:  level += isr / att; if (level >= 1.0f) { level = 1.0f; stage = EnvStage::Decay; } break;
            case EnvStage::Decay:   level = sus + (level - sus) * decCoeff; if (level <= sus + kFloor) { level = sus; stage = EnvStage::Sustain; } break;
            case EnvStage::Sustain: level = sus; break;
            case EnvStage::Release: level *= relCoeff; if (level <= kFloor) { level = 0.0f; stage = EnvStage::Idle; } break;
            case EnvStage::Idle:    level = 0.0f; break;
        }
        return level;
    }
}

//==============================================================================
// PhizmOsc - Envelope (Amp A, Amp B, Filter, Transwave)
//==============================================================================
class PhizmOscEnvModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("env", "Envelope", ModuleSection::Utility, 5);
        d.description = "PhizmOsc's envelope: linear attack, exponential decay and release. Used four times - "
                        "the two oscillators' amps, the filter and the transwave position. Active Out is 1 "
                        "while it runs (the global filter averages its envelopes over the sounding voices).";
        d.sockets = { modOut ("envOut", "Env Out"), modOut ("activeOut", "Active Out") };
        d.params = {
            makeRotary ("attack",  "Attack",  0.001f, 8.0f, 0.01f, 0, "s", true),
            makeRotary ("decay",   "Decay",   0.001f, 8.0f, 0.3f, 0, "s", true),
            makeRotary ("sustain", "Sustain", 0.0f, 1.0f, 0.8f, 0),
            makeRotary ("release", "Release", 0.001f, 8.0f, 0.5f, 0, "s", true)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void voiceNoteOn (int v, int, bool) override { stage[(size_t) v] = EnvStage::Attack; level[(size_t) v] = 0.0f; }
    void voiceNoteOff (int v) override { if (stage[(size_t) v] != EnvStage::Idle) stage[(size_t) v] = EnvStage::Release; }
    void voiceReset (int v) override { stage[(size_t) v] = EnvStage::Idle; level[(size_t) v] = 0.0f; }

    void blockStart() override
    {
        const float isr = (float) (1.0 / sampleRate);
        decCoeff = std::exp (-isr * 5.0f / juce::jmax (0.001f, param (1)));
        relCoeff = std::exp (-isr * 5.0f / juce::jmax (0.001f, param (3)));
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        auto& lv = level[(size_t) v];
        lv = advanceEnv (lv, stage[(size_t) v], juce::jmax (0.001f, param (0)), param (2), decCoeff, relCoeff,
                         (float) (1.0 / sampleRate));
        out[0] = { lv, 0.0f };
        out[1] = { stage[(size_t) v] != EnvStage::Idle ? 1.0f : 0.0f, 0.0f };
    }

    double voiceTailSeconds() const override { return getParameterBase (3) * 2.0 + 0.05; }

private:
    std::array<EnvStage, kMaxVoices> stage {};
    std::array<float, kMaxVoices> level {};
    float decCoeff { 0.99f }, relCoeff { 0.99f };
};

//==============================================================================
// PhizmOsc - Pitch Env
//==============================================================================
class PhizmOscPitchEnvModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("pitchenv", "Pitch Env", ModuleSection::Utility, 6);
        d.description = "PhizmOsc's pitch envelope: every note starts Amount semitones off and falls back to "
                        "pitch over Decay. Pitch Out is in semitones, into the oscillators' Pitch In.";
        d.sockets = { modOut ("pitchOut", "Pitch Out") };
        d.params = {
            makeRotary ("amount", "Amount", -24.0f, 24.0f, 0.0f, 0, "st"),
            makeRotary ("decay",  "Decay",  0.001f, 4.0f, 0.3f, 0, "s", true)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void voiceNoteOn (int v, int, bool) override { level[(size_t) v] = 1.0f; }
    void blockStart() override { coeff = std::exp (-(float) (1.0 / sampleRate) * 5.0f / juce::jmax (0.001f, param (1))); }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        auto& l = level[(size_t) v];
        if (l > 0.0f) { l *= coeff; if (l < 1e-4f) l = 0.0f; }
        out[0] = { l * param (0), 0.0f };
    }

private:
    std::array<float, kMaxVoices> level {};
    float coeff { 0.99f };
};

//==============================================================================
// PhizmOsc - LFOs (global)
//==============================================================================
class PhizmOscLfoModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("lfos", "LFOs", ModuleSection::Utility, 7);
        d.description = "PhizmOsc's two global LFOs: the pitch LFO (Pitch Out, in semitones, for the "
                        "oscillators' Pitch In - at the plugin's effective half speed) and the filter's LFO.";
        d.sockets = { modOut ("pitchOut", "Pitch Out"), modOut ("filterOut", "Filter LFO") };
        d.params = {
            makeRotary ("pitchLFO",     "Pitch Depth", 0.0f, 12.0f, 0.0f, 0, "st"),
            makeRotary ("pitchLFORate", "Pitch Rate",  0.01f, 20.0f, 1.0f, 0, "Hz", true),
            makeRotary ("posLFORate",   "Filter Rate", 0.01f, 20.0f, 0.5f, 0, "Hz", true)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void reset() override { pitchPhase = filterPhase = 0.0; }

    void processSample (const StereoFrame*, StereoFrame* out) override
    {
        pitchPhase += param (1) * 0.5 / sampleRate;
        if (pitchPhase > 1.0) pitchPhase -= 1.0;
        filterPhase += param (2) / sampleRate;
        if (filterPhase > 1.0) filterPhase -= 1.0;
        out[0] = { (float) std::sin (pitchPhase * twoPi) * param (0), 0.0f };
        out[1] = { (float) std::sin (filterPhase * twoPi), 0.0f };
    }

private:
    double pitchPhase { 0.0 }, filterPhase { 0.0 };
};

//==============================================================================
// PhizmOsc - Scanner
//==============================================================================
class PhizmOscScannerModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("scanner", "Scanner", ModuleSection::Utility, 2);
        d.description = "PhizmOsc's evolution: one scan position per note running through the two drawn "
                        "curves (A and B) at Evo Time, in the chosen scan style, plus the evo and position "
                        "LFOs, keytrack, velocity, frame snap and the transwave envelope (TW Env In). Pos A "
                        "and Pos B are the oscillators' frame positions.";
        d.sockets = { modIn ("twIn", "TW Env In"), modOut ("posA", "Pos A"), modOut ("posB", "Pos B"),
                      modOut ("twMod", "TW Mod"), modOut ("offsetOut", "Offset Out") };
        d.params = {
            makeRotary ("evoTime", "Evo Time", 0.1f, 100.0f, 4.0f, 0, "s", true),
            makeCombo ("scanStyle", "Scan", { "Forward", "Fwd Stay", "Back & Forth", "Bwd Stay", "Backward" }, 0, 0, 2),
            makeRotary ("evoTimeB", "Time B", 0.1f, 100.0f, 4.0f, 0, "s", true),
            makeRotary ("evoBPhaseOff", "B Offset", 0.0f, 1.0f, 0.0f, 0),
            makeRotary ("evoLFORate", "Evo LFO", 0.01f, 20.0f, 0.3f, 1, "Hz", true),
            makeRotary ("evoLFODepth", "Evo Depth", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("posLFORate", "Pos LFO", 0.01f, 20.0f, 0.5f, 1, "Hz", true),
            makeRotary ("posLFODepth", "Pos Depth", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("keytrack", "Keytrack", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("frameSnap", "Snap", 0.0f, 1.0f, 0.0f, 2),
            makeRotary ("twAmt", "TW Amt", -1.0f, 1.0f, 0.0f, 2),
            makeRotary ("twVelAmt", "TW Vel", 0.0f, 1.0f, 0.0f, 2),
            makeCombo ("velToFrame", "Vel>Frame", { "Off", "On" }, 0, 2, 1),
            makeCombo ("evoStepped", "Step A", { "Off", "On" }, 0, 3, 1),
            makeCombo ("evoSteppedB", "Step B", { "Off", "On" }, 0, 3, 1),
            makeCombo ("evoPhaseCarry", "Carry", { "Off", "On" }, 0, 3, 1),
            makeCombo ("evoRestart", "Restart", { "Off", "On" }, 0, 3, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    int extraContentHeight() const override { return 110; }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        std::vector<CurveDisplay::Layer> layers {
            { &curveA, "Curve A", kit::accentOf (kPlugin).brighter (0.3f) },
            { &curveB, "Curve B", juce::Colour (0xff9fd0ff) }
        };
        return std::make_unique<CurveDisplay> (*this, std::move (layers),
                                               [this] { return livePhase.load (std::memory_order_relaxed); });
    }

    juce::String saveCustomState() const override { return curveA.toBase64() + "|" + curveB.toBase64(); }
    void loadCustomState (const juce::String& s) override
    {
        curveA.fromBase64 (s.upToFirstOccurrenceOf ("|", false, false));
        curveB.fromBase64 (s.fromFirstOccurrenceOf ("|", false, false));
    }
    PointCurve& curve (int i) { return i == 0 ? curveA : curveB; }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& vs = voices[(size_t) v];
        const bool carry = param (15) >= 0.5f && param (16) < 0.5f;
        vs.curvePhase = carry && lastActive >= 0 ? voices[(size_t) lastActive].curvePhase : 0.0;
        vs.scanDir = 1;
        vs.finished = false;
        vs.note = note;
        vs.evoLfoPhase = (double) (note & 0xF) / 16.0;
        vs.posLfoPhase = (double) ((note * 7) & 0xF) / 16.0;
        lastActive = v;
    }
    void voiceVelocity (int v, float vel) override { voices[(size_t) v].velocity = vel; }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& vs = voices[(size_t) v];
        const double inc = 1.0 / (juce::jmax (0.1f, param (0)) * sampleRate);
        const double incB = 1.0 / (juce::jmax (0.1f, param (2)) * sampleRate);

        vs.evoLfoPhase += param (4) / sampleRate;  if (vs.evoLfoPhase > 1.0) vs.evoLfoPhase -= 1.0;
        vs.posLfoPhase += param (6) / sampleRate;  if (vs.posLfoPhase > 1.0) vs.posLfoPhase -= 1.0;
        const float evoLfo = (float) std::sin (vs.evoLfoPhase * twoPi);
        const float posLfo = (float) std::sin (vs.posLfoPhase * twoPi);

        if (! vs.finished)
        {
            switch ((int) param (1))
            {
                case 0: vs.curvePhase += inc; if (vs.curvePhase >= 1.0) vs.curvePhase -= 1.0; break;
                case 1: vs.curvePhase += inc; if (vs.curvePhase >= 1.0) { vs.curvePhase = 1.0; vs.finished = true; } break;
                case 2: vs.curvePhase += inc * vs.scanDir;
                        if (vs.curvePhase >= 1.0) { vs.curvePhase = 1.0; vs.scanDir = -1; }
                        else if (vs.curvePhase <= 0.0) { vs.curvePhase = 0.0; vs.scanDir = 1; }
                        break;
                case 3: vs.curvePhase -= inc; if (vs.curvePhase <= 0.0) { vs.curvePhase = 0.0; vs.finished = true; } break;
                default: vs.curvePhase -= inc; if (vs.curvePhase <= 0.0) vs.curvePhase += 1.0; break;
            }
        }
        if (v == lastActive)
            livePhase.store ((float) vs.curvePhase, std::memory_order_relaxed);

        double phB = vs.curvePhase + param (3) + (incB - inc) * (vs.curvePhase / juce::jmax (inc, 1e-12));
        phB = std::fmod (phB, 1.0);
        if (phB < 0.0) phB += 1.0;

        const float keytrackMod = ((float) (vs.note - 60) / 60.0f) * param (8) * 0.5f;
        float twDepth = param (10);
        const float twVel = param (11);
        twDepth *= (1.0f - twVel) + vs.velocity * twVel;
        const float twMod = in[0][0] * twDepth;

        const float evoAmt = evoLfo * param (5) * 0.5f;
        float a = juce::jlimit (0.0f, 1.0f, eval (curveA, (float) vs.curvePhase, param (13) >= 0.5f) + twMod + evoAmt + keytrackMod);
        float b = juce::jlimit (0.0f, 1.0f, eval (curveB, (float) phB, param (14) >= 0.5f) + twMod + evoAmt + keytrackMod);

        const float snap = param (9);
        if (snap > 0.001f)
        {
            const float steps = std::pow (2.0f, (1.0f - snap) * 6.0f + 1.0f);
            a = std::round (a * steps) / steps;
            b = std::round (b * steps) / steps;
        }

        // the velocity offset and the position LFO go out separately: Osc A's
        // frame jump is measured from the curve position alone
        const float extra = (param (12) >= 0.5f ? vs.velocity : 0.0f) + posLfo * param (7) * 0.5f;
        out[0] = { a, 0.0f };
        out[1] = { b, 0.0f };
        out[2] = { twMod, 0.0f };
        out[3] = { extra, 0.0f };
    }

private:
    // TranswaveAudioProcessor::evalCurve
    static float eval (const PointCurve& c, float t, bool stepped)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const float fi = t * (float) (kEvoPoints - 1);
        const int i0 = juce::jlimit (0, kEvoPoints - 1, (int) fi);
        const int i1 = juce::jlimit (0, kEvoPoints - 1, i0 + 1);
        const float v0 = c.get (i0);
        return stepped ? v0 : v0 + (fi - (float) i0) * (c.get (i1) - v0);
    }

    struct VoiceState
    {
        double curvePhase { 0.0 }, evoLfoPhase { 0.0 }, posLfoPhase { 0.0 };
        int scanDir { 1 }, note { 60 };
        bool finished { false };
        float velocity { 1.0f };
    };

    std::array<VoiceState, kMaxVoices> voices {};
    PointCurve curveA { kEvoPoints, 0.0f, 1.0f, 0.0f }, curveB { kEvoPoints, 0.0f, 1.0f, 0.0f };
    std::atomic<float> livePhase { 0.0f };
    int lastActive { -1 };

public:
    PhizmOscScannerModule()
    {
        for (int i = 0; i < kEvoPoints; ++i)
        {
            curveA.set (i, (float) i / (float) (kEvoPoints - 1));
            curveB.set (i, (float) i / (float) (kEvoPoints - 1));
        }
    }
};

//==============================================================================
// PhizmOsc - Transwave Osc
//==============================================================================
class PhizmOscOscModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("osc", "Transwave Osc", ModuleSection::Oscillator, 0);
        d.description = "One of PhizmOsc's two transwave oscillators, with its own wavetable (Load WT, or the "
                        "one the plugin had - saved with the patch). Pos In picks the frame, Pitch In adds "
                        "semitones. Osc A's Link Out drives Osc B's Link In: B reads A's phase for its "
                        "stereo pair and follows A's frame jumps. Ring Out feeds the Mixer's ring modulator.";
        d.sockets = { modIn ("posIn", "Pos In"), modIn ("pitchIn", "Pitch In"), audioIn ("linkIn", "Link In"),
                      modIn ("offsetIn", "Offset In"), midiIn ("addMidiIn", "Add Midi In"),
                      audioOut ("audioOut", "Audio Out"), audioOut ("ringOut", "Ring Out"), audioOut ("linkOut", "Link Out") };
        d.params = {
            makeCombo ("role", "Osc", { "A", "B" }, 0, 0, 1),
            makeSteppedList ("octave", "Octave", { "-2", "-1", "0", "+1", "+2" }, 2, 0),
            makeRotary ("detune", "Detune", -100.0f, 100.0f, 0.0f, 0, "ct"),
            makeRotary ("spread", "Spread", 0.0f, 50.0f, 0.0f, 0, "ct"),
            makeRotary ("uniDetune", "Uni Detune", 0.0f, 50.0f, 0.0f, 0, "ct"),
            makeRotary ("grit", "Grit", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("bitCrush", "Bits", 4.0f, 16.0f, 16.0f, 1),
            makeRotary ("stereoWidth", "Width", 0.0f, 1.0f, 0.5f, 1),
            makeRotary ("stereoPhase", "B Phase", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("wtSmooth", "Smooth", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("jumpProb", "Jump", 0.0f, 1.0f, 0.0f, 2),
            makeCombo ("frameInterp", "Interp", { "Off", "On" }, 1, 2, 1),
            makeRotary ("glide", "Glide", 0.0f, 4.0f, 0.0f, 2, "s", true),
            makeSteppedList ("voices", "Voices", voiceList(), 15, 2).noMod(),
            makeRotary ("cycle", "Cycle", 16.0f, 8192.0f, 2048.0f, 3, {}, true, 1.0f).noMod(),
            makeButton ("loadWT", "Load WT", 3, 2)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    double voiceTailSeconds() const override { return 0.05; }

    void prepare (double sr) override { SynthModule::prepare (sr); pool.resetAll(); }
    void reset() override { pool.resetAll(); }
    void voiceReset (int v) override { pool.resetVoice (v); }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& vs = voices[(size_t) v];
        vs.note = note;
        vs.phase = 0.0;
        vs.frameOffset = 0.0f;
        const double target = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
        vs.glideTarget = target;
        const float glideTime = param (12);
        const bool mono = voiceLimit() == 1;
        if (mono && lastNoteFreq > 0.0 && glideTime > 0.001f) { vs.glideStart = lastNoteFreq; vs.glideProgress = 0.0f; }
        else { vs.glideStart = target; vs.glideProgress = 1.0f; }
        lastNoteFreq = target;
        pool.noteOn (v, voiceLimit());
    }
    void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& vs = voices[(size_t) v];
        const bool isA = param (0) < 0.5f;
        const float isr = (float) (1.0 / sampleRate);

        // --- glide (log space) and pitch
        double base = vs.glideTarget;
        const float glideTime = param (12);
        if (vs.glideProgress < 1.0f && glideTime > 0.001f)
        {
            vs.glideProgress = juce::jmin (1.0f, vs.glideProgress + isr / glideTime);
            const double ls = std::log (juce::jmax (1.0, vs.glideStart)), lt = std::log (juce::jmax (1.0, vs.glideTarget));
            base = std::exp (ls + (double) vs.glideProgress * (lt - ls));
        }
        const double dm = std::pow (2.0, (double) param (2) / 1200.0);
        const float uRand = (float) ((vs.note * 1664525 + 1013904223) & 0x7FFFFFFF) / (float) 0x7FFFFFFF;
        const double um = std::pow (2.0, (uRand * 2.0f - 1.0f) * param (4) / 1200.0);
        const double spreadMul = std::pow (2.0, (isA ? -1.0 : 1.0) * (double) param (3) / 2400.0);
        const double octMul = std::pow (2.0, std::round ((double) param (1) - 2.0));
        const double pitchMul = std::pow (2.0, (double) in[1][0] / 12.0);
        const double f = base * dm * um * pitchMul * spreadMul * octMul;

        vs.phase += f / sampleRate;

        // --- frame position, and the frame jump (Osc A decides, B follows)
        float frameOffset = vs.frameOffset;
        if (vs.phase >= 1.0)
        {
            vs.phase -= 1.0;
            if (isA)
            {
                const float jp = param (10);
                if (jp > 0.0f && rng.next() < jp * 0.5f)
                    vs.frameOffset = rng.next() - in[0][0];
                frameOffset = vs.frameOffset;
            }
        }
        if (! isA && isInputConnected (2))
            frameOffset = in[2][1];

        float pos = in[0][0] + frameOffset + in[3][0];
        pos = std::fmod (pos, 1.0f);
        if (pos < 0.0f) pos += 1.0f;

        // --- grit (read position decimation) and the stereo phase
        double l = vs.phase;
        std::shared_ptr<const Table> t;
        {
            const juce::SpinLock::ScopedLockType sl (tableLock);
            t = table;
        }
        const float gr = param (5);
        if (gr > 0.0f && t != nullptr)
        {
            const int steps = juce::jmax (6, (int) (juce::jmax (6, t->cycle) * std::pow (1.0f - gr, 3.0f)));
            l = std::floor (l * steps) / (double) steps;
        }
        while (l < 0.0) l += 1.0;
        while (l >= 1.0) l -= 1.0;
        const double phB = param (8);
        double readMain = l, readSide;
        if (isA)
            readSide = std::fmod (l + phB, 1.0);
        else
        {
            readMain = std::fmod (l + phB, 1.0);
            if (readMain < 0.0) readMain += 1.0;
            readSide = isInputConnected (2) ? (double) in[2][0] : l;
        }

        float L = 0.0f, R = 0.0f, ringL, ringR;
        const bool loaded = t != nullptr && t->frames > 0;
        if (loaded)
        {
            const bool interp = param (11) >= 0.5f;
            const float fi = pos * (float) (t->frames - 1);
            const float bits = param (6), w = param (7), smooth = param (9);
            const float a = crush (t->sample (fi, readMain, interp, smooth), bits);
            const float b = crush (t->sample (fi, readSide, interp, smooth), bits);
            const float mid = (a + b) * 0.5f, side = (a - b) * 0.5f;
            L = mid + side * w;
            R = mid - side * w;
            ringL = L; ringR = R;
        }
        else
        {
            // PhizmOsc's ring modulator uses a sine on an empty slot
            const float s = std::sin ((float) (isA ? l : readMain) * juce::MathConstants<float>::twoPi);
            ringL = ringR = s;
        }

        const float g = pool.nextGain (v, sampleRate);
        out[0] = { L * g, R * g };
        out[1] = { ringL * g, ringR * g };
        out[2] = { (float) l, vs.frameOffset };   // A's read phase (after grit) and frame jump
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id != "loadWT")
            return;
        chooser = std::make_unique<juce::FileChooser> ("Load a wavetable", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        juce::WeakReference<SynthModule> weak (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [weak] (const juce::FileChooser& fc)
            {
                auto* self = dynamic_cast<PhizmOscOscModule*> (weak.get());
                if (self == nullptr) return;
                std::unique_ptr<juce::InputStream> in;
               #if JUCE_ANDROID
                if (auto doc = juce::AndroidDocument::fromDocument (fc.getURLResult()); doc.hasValue())
                    in = doc.createInputStream();
               #else
                in = fc.getResult().createInputStream();
               #endif
                if (in == nullptr) return;
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (std::move (in))))
                {
                    juce::AudioBuffer<float> data (1, (int) reader->lengthInSamples);
                    reader->read (&data, 0, data.getNumSamples(), 0, true, false);
                    self->setTable (data, (int) self->getParameter ("cycle"));
                }
            });
    }

    //=== the wavetable =======================================================
    void setTable (const juce::AudioBuffer<float>& frames, int cycleSamples)
    {
        auto t = std::make_shared<Table>();
        t->cycle = juce::jmax (16, cycleSamples);
        t->frames = frames.getNumSamples() / t->cycle;
        t->data.setSize (1, juce::jmax (1, t->frames * t->cycle));
        t->data.clear();
        if (t->frames > 0)
            t->data.copyFrom (0, 0, frames, 0, 0, t->frames * t->cycle);
        const juce::SpinLock::ScopedLockType sl (tableLock);
        table = t;
    }

    bool hasAudioToSave() const override { return table != nullptr; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        rate = 48000.0;
        const juce::SpinLock::ScopedLockType sl (tableLock);
        if (table == nullptr) return nullptr;
        return std::shared_ptr<const juce::AudioBuffer<float>> (table, &table->data);
    }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double) override
    {
        if (audio != nullptr)
            setTable (*audio, (int) getParameter ("cycle"));
    }

private:
    struct Table
    {
        juce::AudioBuffer<float> data;
        int frames { 0 }, cycle { 2048 };

        // TranswaveAudioProcessor::sampleFrameRaw
        float sample (float fi, double ph, bool interp, float smoothAmt) const
        {
            const int i0 = juce::jlimit (0, frames - 1, (int) fi);
            const int i1 = juce::jlimit (0, frames - 1, i0 + 1);
            const float bl = fi - (float) i0;
            const float* f0 = data.getReadPointer (0, i0 * cycle);
            const float* f1 = data.getReadPointer (0, i1 * cycle);
            float r;
            if (interp)
            {
                const float fsi = (float) (ph * cycle);
                const int s0 = (int) fsi % cycle, s1 = (s0 + 1) % cycle;
                const float sf = fsi - (float) (int) fsi;
                const float a = f0[s0] + sf * (f0[s1] - f0[s0]);
                const float b = f1[s0] + sf * (f1[s1] - f1[s0]);
                r = a + bl * (b - a);
            }
            else
            {
                const int si = juce::jlimit (0, cycle - 1, (int) (ph * cycle) % cycle);
                r = f0[si] + bl * (f1[si] - f0[si]);
            }
            if (smoothAmt > 0.001f)
            {
                const float fadeLen = smoothAmt * 0.15f;
                float env = 1.0f;
                if (ph < (double) fadeLen) env = (float) (ph / (double) fadeLen);
                else if (ph > 1.0 - (double) fadeLen) env = (float) ((1.0 - ph) / (double) fadeLen);
                r *= env;
            }
            return r;
        }
    };

    static float crush (float s, float bits)
    {
        if (bits >= 15.9f) return s;
        const float lv = std::pow (2.0f, bits) - 1.0f;
        return std::round (s * lv) / lv;
    }

    struct VoiceState
    {
        double phase { 0.0 }, glideStart { 0.0 }, glideTarget { 440.0 };
        float glideProgress { 1.0f }, frameOffset { 0.0f };
        int note { 60 };
    };

    std::array<VoiceState, kMaxVoices> voices {};
    mutable juce::SpinLock tableLock;
    std::shared_ptr<Table> table;
    ModuleVoicePool pool;
    FastRand rng;
    double lastNoteFreq { 0.0 };
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// PhizmOsc - Mixer
//==============================================================================
class PhizmOscMixerModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("mixer", "Mixer", ModuleSection::Utility, 3);
        d.description = "PhizmOsc's A/B mix: each oscillator through its own amp envelope and the velocity, "
                        "crossfaded by Mix, and the ring modulator (A x B, sines on empty slots) blended in "
                        "with the plugin's level compensation.";
        d.sockets = { audioIn ("aIn", "Osc A"), audioIn ("bIn", "Osc B"), audioIn ("aRing", "A Ring"), audioIn ("bRing", "B Ring"),
                      modIn ("envA", "Env A"), modIn ("envB", "Env B"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("oscMix", "A/B Mix", 0.0f, 1.0f, 0.5f, 0),
            makeRotary ("ringMod", "Ring", 0.0f, 1.0f, 0.0f, 0)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void voiceVelocity (int v, float vel) override { velocity[(size_t) v] = vel; }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        const float vel = velocity[(size_t) v];
        const float evA = in[4][0] * vel, evB = in[5][0] * vel;
        const float mix = param (0), ring = param (1);
        const float envMix = evA * (1.0f - mix) + evB * mix;
        const float comp = 1.0f + ring * (1.4142f - 1.0f);
        for (int c = 0; c < 2; ++c)
        {
            const float dry = in[0][(size_t) c] * evA * (1.0f - mix) + in[1][(size_t) c] * evB * mix;
            const float wet = ring > 0.0f ? in[2][(size_t) c] * in[3][(size_t) c] * envMix : 0.0f;
            out[0][(size_t) c] = (dry * (1.0f - ring) + wet * ring) * comp;
        }
    }

private:
    std::array<float, kMaxVoices> velocity {};
};

//==============================================================================
// PhizmOsc - Filter
//==============================================================================
template <bool PerVoiceFilter>
class PhizmOscFilterModuleT final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = PerVoiceFilter ? base ("filter", "Voice Filter", ModuleSection::Filter, 1)
                                : base ("busfilter", "Bus Filter", ModuleSection::Filter, 2);
        d.description = PerVoiceFilter
            ? "PhizmOsc's 4-pole low pass, one per note (the plugin's Per Voice filter): each note's filter "
              "envelope, velocity and TW Mod move its cutoff."
            : "PhizmOsc's 4-pole low pass after the voices meet (the plugin's default): the cutoff follows the "
              "sounding notes' filter and transwave envelopes, averaged over Active In, as the plugin does.";
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("envIn", "Env In"), modIn ("twIn", "TW In"),
                      modIn ("lfoIn", "LFO In"), modIn ("activeIn", "Active In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("filterFreq", "Freq", 20.0f, 20000.0f, 20000.0f, 0, "Hz", true),
            makeRotary ("filterQ", "Reso", 0.1f, 12.0f, 0.707f, 0, {}, true),
            makeRotary ("filterEnvAmt", "Env Amt", -1.0f, 1.0f, 0.0f, 0),
            makeRotary ("filterLFODep", "LFO Amt", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("twToFilter", "TW Amt", -1.0f, 1.0f, 0.0f, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return PerVoiceFilter ? VoiceMode::PerVoice : VoiceMode::Global; }
    void voiceVelocity (int v, float vel) override { velocity[(size_t) v] = vel; }
    void reset() override { for (auto& f : filters) f.reset(); global.reset(); }
    void voiceReset (int v) override { filters[(size_t) v].reset(); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        const float velScale = 0.3f + 0.7f * velocity[(size_t) v];
        const float oct = in[1][0] * param (2) * 6.0f + in[3][0] * param (3) * 3.0f + in[2][0] * param (4) * 4.0f;
        run (filters[(size_t) v], in, out, param (0) * velScale * std::pow (2.0f, oct));
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        // the plugin's global filter: envelopes averaged over the sounding voices
        const float n = juce::jmax (1.0f, in[4][0]);
        const float oct = in[1][0] / n * param (2) * 6.0f + in[3][0] * param (3) * 3.0f + in[2][0] / n * param (4) * 4.0f;
        if (counter-- <= 0)
        {
            counter = 31;
            globalCutoff = param (0) * std::pow (2.0f, oct);
            global.setLowpass (juce::jlimit (20.0f, 20000.0f, globalCutoff), juce::jmax (0.1f, param (1)), sampleRate);
        }
        out[0] = { global.processL (in[0][0]), global.processR (in[0][1]) };
    }

private:
    void run (aqp_phizmosc::StereoFilter4Pole& f, const StereoFrame* in, StereoFrame* out, float cutoff)
    {
        f.setLowpass (juce::jlimit (20.0f, 20000.0f, cutoff), juce::jmax (0.1f, param (1)), sampleRate);
        out[0] = { f.processL (in[0][0]), f.processR (in[0][1]) };
    }

    std::array<aqp_phizmosc::StereoFilter4Pole, kMaxVoices> filters;
    aqp_phizmosc::StereoFilter4Pole global;
    std::array<float, kMaxVoices> velocity {};
    float globalCutoff { 20000.0f };
    int counter { 0 };
};

//==============================================================================
// PhizmOsc - Bus (noise + soft clip), Chorus, Reverb
//==============================================================================
class PhizmOscBusModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("bus", "Voice Bus", ModuleSection::Utility, 4);
        d.description = "Where PhizmOsc's voices meet: the noise is added here, before the filter, and the sum "
                        "goes through the plugin's soft clipper.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = { makeRotary ("noiseLevel", "Noise", 0.0f, 1.0f, 0.0f, 0) };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        float l = in[0][0], r = in[0][1];
        const float noise = param (0);
        if (noise > 0.0f) { l += (rng.next() * 2.0f - 1.0f) * noise * 0.25f; r += (rng.next() * 2.0f - 1.0f) * noise * 0.25f; }
        constexpr float sc = 0.3f;
        out[0] = { (l * sc) / (1.0f + std::abs (l * sc)), (r * sc) / (1.0f + std::abs (r * sc)) };
    }
private:
    FastRand rng;
};

class PhizmOscChorusModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("chorus", "Chorus", ModuleSection::Effect, 8);
        d.description = "PhizmOsc's output gain and its chorus (a sine-swept delay per side, added at half "
                        "level).";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("gain", "Gain", 0.0f, 1.0f, 0.7f, 0),
            makeRotary ("chorusRate", "Rate", 0.01f, 8.0f, 0.5f, 0, "Hz", true),
            makeRotary ("chorusDepth", "Depth", 0.0f, 1.0f, 0.0f, 0)
        };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); gain.reset (sr, 0.05); gain.setCurrentAndTargetValue (param (0)); chorus->reset(); }
    void reset() override { chorus->reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        gain.setTargetValue (param (0));
        const float g = gain.getNextValue();
        float l = in[0][0] * g, r = in[0][1] * g;
        if (param (2) > 0.001f)
            chorus->process (l, r, param (1), param (2), sampleRate);
        out[0] = { l, r };
    }
private:
    juce::SmoothedValue<float> gain;
    std::unique_ptr<aqp_phizmosc::StereoChorus> chorus = std::make_unique<aqp_phizmosc::StereoChorus>();
};

class PhizmOscReverbModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("reverb", "Reverb", ModuleSection::Effect, 9);
        d.description = "PhizmOsc's reverb (JUCE's Freeverb-style reverb) at the very end.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("reverbSize", "Size", 0.0f, 1.0f, 0.5f, 0),
            makeRotary ("reverbDamp", "Damp", 0.0f, 1.0f, 0.5f, 0),
            makeRotary ("reverbWet", "Wet", 0.0f, 1.0f, 0.0f, 0)
        };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); reverb.setSampleRate (sr); reverb.reset(); }
    void reset() override { reverb.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (++counter >= 32)
        {
            counter = 0;
            juce::Reverb::Parameters rp;
            rp.roomSize = param (0); rp.damping = param (1); rp.wetLevel = param (2);
            rp.dryLevel = 1.0f - param (2); rp.width = 1.0f; rp.freezeMode = 0.0f;
            reverb.setParameters (rp);
        }
        float l = in[0][0], r = in[0][1];
        reverb.processStereo (&l, &r, 1);
        out[0] = { l, r };
    }
private:
    juce::Reverb reverb;
    int counter { 32 };
};

AQUANODE_REGISTER_MODULE (PhizmOscOscModule,      PhizmOscOscModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscScannerModule,  PhizmOscScannerModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscMixerModule,    PhizmOscMixerModule::descriptor)
using PhizmOscFilterModule    = PhizmOscFilterModuleT<true>;
using PhizmOscBusFilterModule = PhizmOscFilterModuleT<false>;
AQUANODE_REGISTER_MODULE (PhizmOscFilterModule,    PhizmOscFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscBusFilterModule, PhizmOscBusFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscEnvModule,      PhizmOscEnvModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscPitchEnvModule, PhizmOscPitchEnvModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscLfoModule,      PhizmOscLfoModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscBusModule,      PhizmOscBusModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscChorusModule,   PhizmOscChorusModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmOscReverbModule,   PhizmOscReverbModule::descriptor)

//==============================================================================
// The patch builder
//==============================================================================
namespace aquanode::plugins::phizmosc
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        auto* tw = dynamic_cast<aqp_phizmosc::TranswaveAudioProcessor*> (&original);
        auto P = [&] (const char* id, float def) { return kit::param (original, id, def); };

        const bool mono = P ("mono", 0.0f) > 0.5f;
        const bool perVoiceFilter = P ("filterPerVoice", 0.0f) > 0.5f;

        // --- the scanner and its curves
        const int scanner = b.add ("plugin.phizmosc.scanner", 0, 300);
        b.copy (scanner, original);
        if (auto* s = dynamic_cast<PhizmOscScannerModule*> (b.dsp (scanner)); s != nullptr && tw != nullptr)
            for (int i = 0; i < kEvoPoints; ++i)
            {
                s->curve (0).set (i, tw->getCurvePoint (i, 0));
                s->curve (1).set (i, tw->getCurvePoint (i, 1));
            }

        auto makeEnv = [&] (const char* a, const char* d, const char* s, const char* r, int x, int y)
        {
            const int m = b.add ("plugin.phizmosc.env", x, y);
            b.set (m, "attack", P (a, 0.01f)); b.set (m, "decay", P (d, 0.3f));
            b.set (m, "sustain", P (s, 0.8f)); b.set (m, "release", P (r, 0.5f));
            return m;
        };
        const int envA = makeEnv ("attack", "decay", "sustain", "release", 600, 900);
        const int envB = makeEnv ("attack2", "decay2", "sustain2", "release2", 900, 900);
        const int envF = makeEnv ("filterAtt", "filterDec", "filterSus", "filterRel", 1200, 900);
        const int envT = makeEnv ("twAtt", "twDec", "twSus", "twRel", 0, 900);
        b.cable (envT, "envOut", scanner, "twIn");

        const int penv = b.add ("plugin.phizmosc.pitchenv", 0, 0);
        b.set (penv, "amount", P ("pitchEnvAmt", 0.0f)); b.set (penv, "decay", P ("pitchEnvDec", 0.3f));
        const int lfos = b.add ("plugin.phizmosc.lfos", 0, -260);
        b.copy (lfos, original);

        // --- the oscillators, with the plugin's wavetables
        int osc[2];
        for (int o = 0; o < 2; ++o)
        {
            osc[o] = b.add ("plugin.phizmosc.osc", 300, o * 560);
            b.copy (osc[o], original);
            b.set (osc[o], "role", (float) o);
            b.set (osc[o], "octave", P (o == 0 ? "octaveA" : "octaveB", 0.0f) + 2.0f);
            b.set (osc[o], "wtSmooth", P (o == 0 ? "wtSmooth" : "wtSmoothB", 0.0f));
            b.set (osc[o], "voices", mono ? 0.0f : 15.0f);
            b.set (osc[o], "cycle", P (o == 0 ? "cycleSizeA" : "cycleSizeB", 2048.0f));
            if (tw != nullptr && tw->isWavetableLoaded (o))
            {
                const int frames = tw->getNumFrames (o), cycle = tw->getCycleSamples (o);
                juce::AudioBuffer<float> data (1, frames * cycle);
                std::vector<float> f;
                for (int fr = 0; fr < frames; ++fr)
                    if (tw->getFrameSamples (o, fr, f))
                        data.copyFrom (0, fr * cycle, f.data(), juce::jmin ((int) f.size(), cycle));
                b.set (osc[o], "cycle", (float) cycle);
                if (auto* m = dynamic_cast<PhizmOscOscModule*> (b.dsp (osc[o])))
                    m->setTable (data, cycle);
            }
            b.cable (penv, "pitchOut", osc[o], "pitchIn");
            b.cable (lfos, "pitchOut", osc[o], "pitchIn");
        }
        b.cable (scanner, "posA", osc[0], "posIn");
        b.cable (scanner, "posB", osc[1], "posIn");
        b.cable (scanner, "offsetOut", osc[0], "offsetIn");
        b.cable (scanner, "offsetOut", osc[1], "offsetIn");
        b.cable (osc[0], "linkOut", osc[1], "linkIn");

        const int mixer = b.add ("plugin.phizmosc.mixer", 600, 300);
        b.copy (mixer, original);
        b.cable (osc[0], "audioOut", mixer, "aIn");
        b.cable (osc[1], "audioOut", mixer, "bIn");
        b.cable (osc[0], "ringOut", mixer, "aRing");
        b.cable (osc[1], "ringOut", mixer, "bRing");
        b.cable (envA, "envOut", mixer, "envA");
        b.cable (envB, "envOut", mixer, "envB");

        // --- filter (per voice, or after the voices meet) and the end
        const int bus = b.add ("plugin.phizmosc.bus", 1200, 300);
        b.copy (bus, original);
        const int filter = perVoiceFilter ? b.add ("plugin.phizmosc.filter", 900, 300)
                                          : b.add ("plugin.phizmosc.busfilter", 1500, 300);
        b.copy (filter, original);
        b.cable (envF, "envOut", filter, "envIn");
        b.cable (lfos, "filterOut", filter, "lfoIn");
        const int chorus = b.add ("plugin.phizmosc.chorus", 1800, 300);
        b.copy (chorus, original);
        const int reverb = b.add ("plugin.phizmosc.reverb", 2100, 300);
        b.copy (reverb, original);

        if (perVoiceFilter)
        {
            b.cable (scanner, "twMod", filter, "twIn");
            b.cable (mixer, "audioOut", filter, "audioIn");
            b.cable (filter, "audioOut", bus, "audioIn");
            b.cable (bus, "audioOut", chorus, "audioIn");
        }
        else
        {
            b.cable (envT, "envOut", filter, "twIn");
            b.cable (envA, "activeOut", filter, "activeIn");
            b.cable (mixer, "audioOut", bus, "audioIn");
            b.cable (bus, "audioOut", filter, "audioIn");
            b.cable (filter, "audioOut", chorus, "audioIn");
        }
        b.cable (chorus, "audioOut", reverb, "audioIn");

        PatchPorts ports;
        ports.name = "PhizmOsc";
        ports.audioOut = { { reverb, "audioOut" } };
        ports.midiIn = { { osc[0], "addMidiIn" }, { osc[1], "addMidiIn" } };
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* tw = dynamic_cast<aqp_phizmosc::TranswaveAudioProcessor*> (&original);
        return tw != nullptr && tw->loadPreset (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
