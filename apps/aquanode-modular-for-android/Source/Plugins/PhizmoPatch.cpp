//==============================================================================
// Phizmo - Patch version
//
// A Phizmo Preset layers up to four Sounds (each two transwave oscillators
// with their own envelopes, filter, LFO, modulation matrix and evolution
// curves). Every oscillator sends to the dry bus or the insert-effect bus,
// and everything feeds the reverb send; the buses are summed, soft-clipped
// and set to the output gain, then the insert effect, the reverb return and
// the output limiter follow. The patch version is that, module by module:
//
//   Phizmo - Sound (one per Sound) --dry / insert / reverb-->
//   Phizmo - Bus (noise, soft clip, gain) --dry-----------------> Output
//                                         --insert--> Insert FX -> Output
//                                         --reverb--> Reverb ----> Output
//
// Each Sound module plays its Sound with Phizmo's own voice engine (the
// plugin's renderVoiceBuses, voices of that Sound only); its knobs are that
// Sound's edit-buffer parameters and take cables. The modulation matrix
// routings, the drawn curves and the wavetables travel in the module's state.
//==============================================================================
#include "PluginPhizmoConfig.h"

namespace aqp_phizmo
{
    #include AQP_PHIZMO_SRC(PluginProcessor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "phizmo" };
    using PZ = aqp_phizmo::PhizmoAudioProcessor;
    namespace phz = aqp_phizmo::phz;
    constexpr int kSounds = 4;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.phizmo." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    juce::StringArray offOn() { return { "Off", "On" }; }
    ParamSpec knob (const char* id, const char* label, float lo, float hi, float def, int row,
                    const char* unit = "", bool log = false, float step = 0.0f)
    {
        return makeRotary (id, label, lo, hi, def, row, unit, log, step);
    }
}

//==============================================================================
// Phizmo - Sound
//==============================================================================
class PhizmoSoundModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("sound", "Sound", ModuleSection::Oscillator, 0);
        d.description = "One of a Phizmo Preset's four Sounds, played by Phizmo's own engine: two transwave "
                        "oscillators scanning their wavetables along the evolution curves, with envelopes, "
                        "filter, LFO and the modulation matrix. Low / High Key and Level are its layer; Bus 1 / "
                        "Bus 2 send each oscillator to the insert effect or dry. Its three outputs go to the "
                        "Phizmo Bus. The matrix, curves and wavetables come with the Sound (saved in the patch).";
        d.sockets = { midiIn ("midiIn", "Midi In"),
                      audioOut ("dryOut", "Dry Out"), audioOut ("insOut", "Insert Out"), audioOut ("revOut", "Reverb Out") };
        d.params = {
            makeSteppedList ("sound", "Sound", { "1", "2", "3", "4" }, 0, 0).noMod(),
            knob ("low", "Low Key", 0, 127, 0, 0, "", false, 1.0f),
            knob ("high", "High Key", 0, 127, 127, 0, "", false, 1.0f),
            knob ("layerGain", "Level", 0.0f, 1.0f, 1.0f, 0),
            makeCombo ("busA", "Bus 1", { "Insert", "Dry" }, 0, 1, 1),
            makeCombo ("busB", "Bus 2", { "Insert", "Dry" }, 0, 1, 1),
            makeCombo ("oscAOn", "Osc 1", offOn(), 1, 1, 1),
            makeCombo ("oscBOn", "Osc 2", offOn(), 1, 1, 1),
            knob ("oscMix", "A/B Mix", 0.0f, 1.0f, 0.5f, 1),

            knob ("levelA", "Level 1", 0.0f, 1.0f, 1.0f, 2),
            knob ("octaveA", "Oct 1", -2, 2, 0, 2, "", false, 1.0f),
            knob ("tuneA", "Tune 1", -24, 24, 0, 2, "st", false, 1.0f),
            knob ("fineA", "Fine 1", -100, 100, 0, 2, "ct"),
            knob ("panA", "Pan 1", -1.0f, 1.0f, 0.0f, 2),
            knob ("levelB", "Level 2", 0.0f, 1.0f, 1.0f, 3),
            knob ("octaveB", "Oct 2", -2, 2, 0, 3, "", false, 1.0f),
            knob ("tuneB", "Tune 2", -24, 24, 0, 3, "st", false, 1.0f),
            knob ("fineB", "Fine 2", -100, 100, 0, 3, "ct"),
            knob ("panB", "Pan 2", -1.0f, 1.0f, 0.0f, 3),
            knob ("detune", "Detune", -100, 100, 0, 4, "ct"),
            knob ("spread", "Spread", 0, 50, 0, 4, "ct"),
            knob ("uniDetune", "Uni Det", 0, 50, 0, 4, "ct"),
            knob ("ringMod", "Ring", 0.0f, 1.0f, 0.0f, 4),
            knob ("glide", "Glide", 0.0f, 4.0f, 0.0f, 4, "s", true),

            knob ("evoTime", "Evo Time", 0.1f, 100.0f, 4.0f, 5, "s", true),
            knob ("evoTimeB", "Evo B", 0.1f, 100.0f, 4.0f, 5, "s", true),
            makeCombo ("scanStyle", "Scan", { "Forward", "Fwd Stay", "Back & Forth", "Bwd Stay", "Backward" }, 0, 5, 2),
            knob ("evoMode", "Mode 1", 0, 3, 0, 6, "", false, 1.0f),
            knob ("evoModeB", "Mode 2", 0, 3, 0, 6, "", false, 1.0f),
            knob ("waveStartA", "Start 1", 0.0f, 1.0f, 0.0f, 6),
            knob ("waveStartB", "Start 2", 0.0f, 1.0f, 0.0f, 6),
            knob ("jumpProb", "Jump", 0.0f, 1.0f, 0.0f, 6),
            knob ("waveModA", "W.Mod 1", -1.0f, 1.0f, 0.0f, 7),
            knob ("waveModB", "W.Mod 2", -1.0f, 1.0f, 0.0f, 7),
            knob ("evoLFORate", "Evo LFO", 0.01f, 20.0f, 0.3f, 7, "Hz", true),
            knob ("evoLFODepth", "Evo Dep", 0.0f, 1.0f, 0.0f, 7),
            knob ("keytrack", "Key>Frame", 0.0f, 1.0f, 0.0f, 7),

            makeCombo ("filterType", "Filter", { "LP 4-pole", "Band Pass", "High Pass" }, 0, 8, 2),
            knob ("filterFreq", "Cutoff", 20.0f, 20000.0f, 20000.0f, 8, "Hz", true),
            knob ("filterQ", "Reso", 0.1f, 12.0f, 0.707f, 8, "", true),
            knob ("filterEnvAmt", "Env Amt", -1.0f, 1.0f, 0.0f, 8),
            knob ("filterKeytrack", "F.Key", -1.0f, 1.0f, 0.0f, 9),
            knob ("filterLFODep", "F.LFO", 0.0f, 1.0f, 0.0f, 9),
            knob ("twToFilter", "TW>Filt", -1.0f, 1.0f, 0.0f, 9),
            knob ("grit", "Grit", 0.0f, 1.0f, 0.0f, 9),
            knob ("bitCrush", "Bits", 4.0f, 16.0f, 16.0f, 9),

            knob ("attack", "A 1", 0.001f, 8.0f, 0.01f, 10, "s", true),
            knob ("decay", "D 1", 0.001f, 8.0f, 0.3f, 10, "s", true),
            knob ("sustain", "S 1", 0.0f, 1.0f, 0.8f, 10),
            knob ("release", "R 1", 0.001f, 8.0f, 0.5f, 10, "s", true),
            knob ("ampVelAmt", "Vel", 0.0f, 1.0f, 0.6f, 10),
            knob ("attack2", "A 2", 0.001f, 8.0f, 0.01f, 11, "s", true),
            knob ("decay2", "D 2", 0.001f, 8.0f, 0.3f, 11, "s", true),
            knob ("sustain2", "S 2", 0.0f, 1.0f, 0.8f, 11),
            knob ("release2", "R 2", 0.001f, 8.0f, 0.5f, 11, "s", true),
            knob ("filterAtt", "F.A", 0.001f, 8.0f, 0.01f, 12, "s", true),
            knob ("filterDec", "F.D", 0.001f, 8.0f, 0.5f, 12, "s", true),
            knob ("filterSus", "F.S", 0.0f, 1.0f, 0.0f, 12),
            knob ("filterRel", "F.R", 0.001f, 8.0f, 0.5f, 12, "s", true),
            knob ("twAmt", "TW Amt", -1.0f, 1.0f, 0.0f, 13),
            knob ("twAtt", "TW A", 0.001f, 8.0f, 0.01f, 13, "s", true),
            knob ("twDec", "TW D", 0.001f, 8.0f, 1.0f, 13, "s", true),
            knob ("twSus", "TW S", 0.0f, 1.0f, 0.0f, 13),
            knob ("twRel", "TW R", 0.001f, 8.0f, 0.5f, 13, "s", true),

            makeCombo ("lfoShape", "LFO", { "Triangle", "Sine+Tri", "Sine", "Rise Tri", "Rise Sine", "Saw",
                                            "Square", "Pos Ramp", "Noise" }, 2, 14, 2),
            knob ("lfoSpeed", "LFO Rate", 0.01f, 20.0f, 1.0f, 14, "Hz", true),
            knob ("posLFODepth", "Pos LFO", 0.0f, 1.0f, 0.0f, 14),
            knob ("pitchEnvAmt", "P.Env", -24.0f, 24.0f, 0.0f, 15, "st"),
            knob ("pitchEnvDec", "P.Dec", 0.001f, 4.0f, 0.3f, 15, "s", true),
            knob ("stereoWidth", "Width", 0.0f, 1.0f, 0.5f, 15),
            knob ("stereoPhase", "B Phase", 0.0f, 1.0f, 0.0f, 15),

            makeCombo ("mono", "Mono", offOn(), 0, 16, 1).noMod(),
            knob ("bendRange", "Bend", 0, 12, 2, 16, "st", false, 1.0f),
            makeCombo ("velCurve", "Vel Curve", { "Curve 1", "Curve 2", "Curve 3", "Curve 4", "Fixed 64", "Fixed 127" }, 1, 16, 2)
        };
        return d;
    }

    PhizmoSoundModule()
    {
        auto p = createOriginal (kPlugin, 48000.0);
        pz.reset (dynamic_cast<PZ*> (p.get()));
        if (pz != nullptr)
            p.release();
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override { midiQueue.addEvent (m, pos); }
    void midiAllNotesOff() override { midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos); }

    // which of the Preset's Sounds this module plays (also its edit buffer)
    void useSound (int k)
    {
        k = juce::jlimit (0, kSounds - 1, k);
        if (pz == nullptr)
            return;
        pz->setEditSound (k);
        for (int s = 0; s < kSounds; ++s)
            if (auto* on = kit::findParam (*pz, "soundOn" + juce::String (s)))
                on->setValueNotifyingHost (s == k ? 1.0f : 0.0f);
        sound = k;
        relink();
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        if (pz == nullptr)
            return;
        {
            const juce::ScopedLock sl (pz->getCallbackLock());
            pz->releaseResources();
            pz->setRateAndBufferSizeDetails (sr, kBlock);
            pz->prepareToPlay (sr, kBlock);
        }
        useSound ((int) std::lround (getParameterBase (0)));
        midiQueue.ensureSize (1024);
        for (auto& b : bus) b.fill (0.0f);
        for (auto& o : out) o.fill (0.0f);
        pos = 0;
    }

    void reset() override { midiQueue.clear(); midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0); }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        for (int b = 0; b < 3; ++b)
            outputs[b] = { out[(size_t) b * 2][(size_t) pos], out[(size_t) b * 2 + 1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

    juce::String saveCustomState() const override
    {
        if (pz == nullptr) return {};
        juce::MemoryBlock mb;
        pz->getStateInformation (mb);
        return mb.toBase64Encoding();
    }
    void loadCustomState (const juce::String& s) override
    {
        juce::MemoryBlock mb;
        if (pz != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
        {
            pz->setStateInformation (mb.getData(), (int) mb.getSize());
            useSound ((int) std::lround (getParameterBase (0)));
        }
    }

    PZ* plugin() const { return pz.get(); }

private:
    static constexpr int kBlock = 32;

    void relink()
    {
        links.clear();
        const auto& d = getDescriptor();
        const juce::String k (sound);
        for (int i = 1; i < (int) d.params.size(); ++i)
        {
            auto id = d.params[(size_t) i].id;
            if (id == "low") id = "soundLow" + k;
            else if (id == "high") id = "soundHigh" + k;
            else if (id == "layerGain") id = "soundGain" + k;
            else if (id == "busA") id = "fxBusA" + k;
            else if (id == "busB") id = "fxBusB" + k;
            if (auto* prm = kit::findParam (*pz, id))
                links.push_back ({ prm, i, -1.0f });
        }
    }

    void runBlock()
    {
        if (pz == nullptr)
            return;
        if ((int) std::lround (getParameterBase (0)) != sound)
        {
            // the Sound selector moved: switch on the message thread
            juce::WeakReference<SynthModule> weak (this);
            const int k = (int) std::lround (getParameterBase (0));
            juce::MessageManager::callAsync ([weak, k] { if (auto* m = dynamic_cast<PhizmoSoundModule*> (weak.get())) m->useSound (k); });
            sound = k;   // asked once
        }
        for (auto& l : links)
        {
            const float v = l.param->convertTo0to1 (param (l.knob));
            if (v != l.last) { l.param->setValueNotifyingHost (v); l.last = v; }
        }

        for (auto& b : bus) b.fill (0.0f);
        bool rendered = false;
        {
            const juce::ScopedTryLock sl (pz->getCallbackLock());
            if (sl.isLocked())
            {
                pz->handleMidiAndArp (midiQueue, kBlock);
                rendered = pz->renderVoiceBuses (kBlock, bus[0].data(), bus[1].data(), bus[2].data(), bus[3].data(),
                                                 bus[4].data(), bus[5].data(), 1u << sound, false);
            }
        }
        midiQueue.clear();
        for (size_t c = 0; c < 6; ++c)
            for (int i = 0; i < kBlock; ++i)
                out[c][(size_t) i] = rendered && std::isfinite (bus[c][(size_t) i]) ? bus[c][(size_t) i] : 0.0f;
    }

    struct Link { juce::RangedAudioParameter* param; int knob; float last; };

    std::unique_ptr<PZ> pz;
    std::vector<Link> links;
    juce::MidiBuffer midiQueue;
    std::array<std::array<float, kBlock>, 6> bus {}, out {};
    int pos { 0 }, sound { -1 };
};

//==============================================================================
// Phizmo - Bus
//==============================================================================
class PhizmoBusModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("bus", "Bus", ModuleSection::Utility, 1);
        d.description = "Where Phizmo's Sounds meet: the dry, insert and reverb buses summed, Phizmo's noise "
                        "(through its own filter) added to the dry bus, each bus soft-clipped and set to the "
                        "output Gain - as the plugin does before its effects.";
        d.sockets = { audioIn ("dryIn", "Dry In"), audioIn ("insIn", "Insert In"), audioIn ("revIn", "Reverb In"),
                      audioOut ("dryOut", "Dry Out"), audioOut ("insOut", "Insert Out"), audioOut ("revOut", "Reverb Out") };
        d.params = {
            knob ("noiseLevel", "Noise", 0.0f, 1.0f, 0.0f, 0),
            makeCombo ("noiseFilterType", "N.Filter", { "Low Pass", "Band Pass", "High Pass" }, 0, 0, 2),
            knob ("noiseFreq", "N.Cutoff", 20.0f, 20000.0f, 4000.0f, 0, "Hz", true),
            knob ("noiseQ", "N.Reso", 0.3f, 12.0f, 0.7f, 1),
            knob ("gain", "Gain", 0.0f, 1.0f, 0.7f, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        gain.reset (sr, 0.05);
        gain.setCurrentAndTargetValue (param (4));
        noiseFilter.reset();
    }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        if (--counter < 0)
        {
            counter = 31;
            noiseFilter.setType ((int) std::lround (param (1)), param (2), juce::jmax (0.3f, param (3)), sampleRate);
        }
        float dl = in[0][0], dr = in[0][1];
        const float noise = param (0);
        if (noise > 0.0001f)
        {
            dl += noiseFilter.processL (aqp_phizmo::fastRand() * 2.f - 1.f) * noise * noise * 0.08f;
            dr += noiseFilter.processR (aqp_phizmo::fastRand() * 2.f - 1.f) * noise * noise * 0.08f;
        }
        gain.setTargetValue (param (4));
        const float g = gain.getNextValue();
        outputs[0] = { PZ::softClip (dl) * g, PZ::softClip (dr) * g };
        outputs[1] = { PZ::softClip (in[1][0]) * g, PZ::softClip (in[1][1]) * g };
        outputs[2] = { PZ::softClip (in[2][0]) * g, PZ::softClip (in[2][1]) * g };
    }

private:
    aqp_phizmo::StereoFilter4Pole noiseFilter;
    juce::SmoothedValue<float> gain;
    int counter { 0 };
};

//==============================================================================
// Phizmo - Insert FX
//==============================================================================
class PhizmoInsertModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("insert", "Insert FX", ModuleSection::Effect, 2);
        d.description = "Phizmo's insert effect: 41 algorithms (EQ, reverbs, choruses, flangers, delays, "
                        "distortions, speakers, chatter box, vocoder and their combinations) with Variation, "
                        "Mix and F, the real-time effect-mod macro.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        juce::StringArray algos;
        for (int a = 0; a < phz::PhizmoInsert::NumAlgos; ++a)
            algos.add (phz::PhizmoInsert::algoName (a));
        d.params = {
            makeCombo ("fxAlgo", "Effect", algos, 0, 0, 3),
            knob ("fxVariation", "Variation", 0.0f, 1.0f, 0.35f, 1),
            knob ("fxMix", "Mix", 0.0f, 1.0f, 0.35f, 1),
            knob ("phizF", "F", -1.0f, 1.0f, 0.0f, 1)
        };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); fx.prepare (sr); }
    void reset() override { fx.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        float l = in[0][0], r = in[0][1];
        fx.process (l, r, (int) std::lround (param (0)), param (1), param (2), param (3));
        out[0] = { l, r };
    }
private:
    phz::PhizmoInsert fx;
};

//==============================================================================
// Phizmo - Reverb
//==============================================================================
class PhizmoReverbModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("reverb", "Reverb", ModuleSection::Effect, 3);
        d.description = "Phizmo's global reverb (an 8-line FDN) in its eight variations, as a send: Reverb Out "
                        "is the wet return at Amount, for the Phizmo Output.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        juce::StringArray vars;
        for (int v = 0; v < 8; ++v)
            vars.add (phz::PhizmoReverb::variationName (v));
        d.params = { makeCombo ("revVariation", "Variation", vars, 3, 0, 3),
                     knob ("revAmount", "Amount", 0.0f, 1.0f, 0.25f, 0) };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); fx.prepare (sr); lastVar = -1; }
    void reset() override { fx.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int var = (int) std::lround (param (0));
        if (var != lastVar) { fx.setVariation (var); lastVar = var; }
        float l = in[0][0], r = in[0][1];
        fx.process (l, r, 1.0f);
        const float amount = juce::jlimit (0.0f, 1.0f, param (1));
        out[0] = { l * amount, r * amount };
    }
private:
    phz::PhizmoReverb fx;
    int lastVar { -1 };
};

//==============================================================================
// Phizmo - Output
//==============================================================================
class PhizmoOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("output", "Output", ModuleSection::Utility, 4);
        d.description = "Phizmo's end: the dry bus, the insert effect's return and the reverb return summed, "
                        "through the plugin's output limiter (fast attack, slow release, stereo-linked).";
        d.sockets = { audioIn ("dryIn", "Dry In"), audioIn ("insIn", "Insert In"), audioIn ("revIn", "Reverb In"),
                      audioOut ("audioOut", "Audio Out") };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void reset() override { limGain = 1.0f; }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        float l = in[0][0] + in[1][0] + in[2][0];
        float r = in[0][1] + in[1][1] + in[2][1];
        const float peak = juce::jmax (std::abs (l), std::abs (r));
        constexpr float kCeiling = 0.98f;
        const float target = peak > kCeiling ? kCeiling / peak : 1.0f;
        limGain += (target < limGain ? 0.35f : 0.0008f) * (target - limGain);
        limGain = juce::jlimit (0.05f, 1.0f, limGain);
        out[0] = { l * limGain, r * limGain };
    }
private:
    float limGain { 1.0f };
};

AQUANODE_REGISTER_MODULE (PhizmoSoundModule,  PhizmoSoundModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoBusModule,    PhizmoBusModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoInsertModule, PhizmoInsertModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoReverbModule, PhizmoReverbModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoOutputModule, PhizmoOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::phizmo
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        auto* src = dynamic_cast<PZ*> (&original);
        auto P = [&] (const juce::String& id, float def) { return kit::param (original, id, def); };

        const int busM = b.add ("plugin.phizmo.bus", 1300, 200);
        b.copy (busM, original);
        const int ins = b.add ("plugin.phizmo.insert", 1650, 0);
        b.copy (ins, original);
        const int rev = b.add ("plugin.phizmo.reverb", 1650, 400);
        b.copy (rev, original);
        const int outM = b.add ("plugin.phizmo.output", 2000, 200);

        // the O macro, folded into what it is assigned to (reverb or effect mix)
        const float phizO = P ("phizO", 0.0f);
        const int oDest = (int) std::lround (P ("phizODest", 0.0f));
        if (oDest == 0) b.set (rev, "revAmount", juce::jlimit (0.0f, 1.0f, P ("revAmount", 0.25f) + phizO * 0.5f));
        if (oDest == 4) b.set (ins, "fxMix", juce::jlimit (0.0f, 1.0f, P ("fxMix", 0.35f) + phizO * 0.5f));

        b.cable (busM, "dryOut", outM, "dryIn");
        b.cable (busM, "insOut", ins, "audioIn");
        b.cable (ins, "audioOut", outM, "insIn");
        b.cable (busM, "revOut", rev, "audioIn");
        b.cable (rev, "audioOut", outM, "revIn");

        juce::MemoryBlock state;
        if (src != nullptr)
            src->getStateInformation (state);

        PatchPorts ports;
        ports.name = "Phizmo";
        ports.audioOut = { { outM, "audioOut" } };

        int y = 0;
        for (int k = 0; k < kSounds; ++k)
        {
            if (P ("soundOn" + juce::String (k), k == 0 ? 1.0f : 0.0f) < 0.5f)
                continue;
            const int m = b.add ("plugin.phizmo.sound", 0, y);
            y += 700;
            auto* sm = dynamic_cast<PhizmoSoundModule*> (b.dsp (m));
            if (sm == nullptr || sm->plugin() == nullptr)
                continue;

            // the whole Preset (matrix, curves, wavetables), playing Sound k
            if (state.getSize() > 0)
                sm->plugin()->setStateInformation (state.getData(), (int) state.getSize());
            b.set (m, "sound", (float) k);
            sm->useSound (k);

            // the knobs: Sound k's own values
            if (src != nullptr)
            {
                src->setEditSound (k);
                b.copy (m, original, [k] (const juce::String& id) -> juce::String
                {
                    const juce::String s (k);
                    if (id == "low") return "soundLow" + s;
                    if (id == "high") return "soundHigh" + s;
                    if (id == "layerGain") return "soundGain" + s;
                    if (id == "busA") return "fxBusA" + s;
                    if (id == "busB") return "fxBusB" + s;
                    return id;
                });
                b.set (m, "sound", (float) k);
            }

            b.cable (m, "dryOut", busM, "dryIn");
            b.cable (m, "insOut", busM, "insIn");
            b.cable (m, "revOut", busM, "revIn");
            ports.midiIn.push_back ({ m, "midiIn" });
        }
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* pz = dynamic_cast<PZ*> (&original);
        return pz != nullptr && pz->loadPreset (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
