//==============================================================================
// AlphaBetaFX - Patch version
//
// AlphaBetaFX is AlphaBeta's voice path as an effect: drive into the filter
// (moved by its envelope), then the stereo chorus - each in both of its
// models, Native (tanh drive, Moog ladder / state variable, the two-tap
// chorus) and Original (the FreeAlpha 3.3 drive curves, Chamberlin filter
// and four-tap chorus). The patch version is the two stages:
//
//   AlphaBetaFX - Filter (drive, filter, filter envelope) --> AlphaBetaFX - Chorus
//
// Both run AlphaBetaFX's own DSP classes sample by sample, with its knobs
// read once every 32 samples as the plugin reads them once per block. The
// Filter's envelope follows the input level (Auto) or plays from its Midi In
// (MIDI); Env Out is the envelope for anything else to follow.
//==============================================================================
#include "PluginAlphaBetaFXConfig.h"

namespace aqp_alphabetafx
{
    #include AQP_ALPHABETAFX_SRC(PluginProcessor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;
namespace ab = aqp_alphabetafx;
namespace AOF = aqp_alphabetafx::AlphaOrigFX;

namespace
{
    const juce::String kPlugin { "alphabetafx" };
    constexpr int kControlBlock = 32;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.alphabetafx." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }
}

//==============================================================================
// AlphaBetaFX - Filter
//==============================================================================
class AlphaBetaFXFilterModule final : public SynthModule
{
public:
    enum { pMode, pDrive, pCut, pRes, pType, pCurve, pEnvOn, pEnvAmt, pAtk, pDcy, pSus, pRel, pEnvMidi, pRetrig };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("filter", "Filter", ModuleSection::Filter, 0);
        d.description = "AlphaBetaFX's drive and filter with its envelope, in either model: Native (tanh drive, "
                        "Moog ladder for the low-passes, state variable for BP / HP, with the quadratic or cubic "
                        "resonance curve) or Original (FreeAlpha 3.3: its drive curves and Chamberlin filter). "
                        "The envelope sweeps the cutoff up to 3 octaves either way (Amount); it starts on "
                        "the input's level (Auto) or on the notes at Midi In (MIDI). Env Out is the envelope.";
        d.sockets = { audioIn ("audioIn", "Audio In"), midiIn ("midiIn", "Midi In"),
                      audioOut ("audioOut", "Audio Out"), modOut ("envOut", "Env Out") };
        d.params = {
            makeCombo ("mode", "Model", { "Native", "Original" }, 0, 0, 2).noMod(),
            makeRotary (ab::FXPID::DRV, "Drive", 0.0f, 1.0f, 0.0f, 0),
            makeRotary (ab::FXPID::FCUT, "Cutoff", 20.0f, 20000.0f, 2000.0f, 0, "Hz", true),
            makeRotary (ab::FXPID::FRES, "Reso", 0.0f, 1.0f, 0.3f, 0),
            makeCombo (ab::FXPID::FTYP, "Type", { "LP12", "LP24", "LP24+", "BP", "HP" }, 1, 0, 2),
            makeCombo (ab::FXPID::RTYP, "Res Curve", { "Quadratic", "Cubic" }, 1, 0, 2).visibleWhen ("mode", 0.0f),

            makeCombo (ab::FXPID::EON, "Envelope", { "Off", "On" }, 0, 1, 2),
            makeRotary (ab::FXPID::EAMT, "Amount", -1.0f, 1.0f, 0.5f, 1),
            makeRotary (ab::FXPID::EATK, "Attack", 0.001f, 2.0f, 0.01f, 1, "s", true),
            makeRotary (ab::FXPID::EDCY, "Decay", 0.001f, 2.0f, 0.2f, 1, "s", true),
            makeRotary (ab::FXPID::ESUS, "Sustain", 0.0f, 1.0f, 0.5f, 1),
            makeRotary (ab::FXPID::EREL, "Release", 0.001f, 4.0f, 0.3f, 1, "s", true),
            makeCombo (ab::FXPID::EMID, "Trigger", { "Auto", "MIDI" }, 0, 1, 2),
            makeRotary (ab::FXPID::ERTG, "Retrigger", 5.0f, 500.0f, 80.0f, 1, "ms", true).visibleWhen (ab::FXPID::EMID, 0.0f)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (! envMidi)
            return;
        if (m.isNoteOn())
            env.noteOn (m.getFloatVelocity());
        else if (m.isNoteOff())
            env.noteOff();
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const float f = (float) sr;
        for (auto* l : { &ladderL, &ladderR }) { l->setSampleRate (f); l->reset(); }
        for (auto* s : { &svfL, &svfR })       { s->setSampleRate (f); s->reset(); }
        env.setSampleRate (f);
        env.reset();
        for (auto& fl : origFilt) fl.reset();
        lastOrigCut = -1.0;
        lastMode = -1;
        counter = 0;
    }

    void reset() override
    {
        ladderL.reset(); ladderR.reset();
        svfL.reset(); svfR.reset();
        for (auto& fl : origFilt) fl.reset();
        env.reset();
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (counter-- <= 0)
        {
            counter = kControlBlock - 1;
            readKnobs();
        }

        float L = in[0][0], R = in[0][1];
        const float envVal = env.process (0.5f * (std::abs (L) + std::abs (R)));

        float modCut = fcut;
        if (envOn && envVal > 0.0001f)
            modCut = juce::jlimit (20.0f, 20000.0f, fcut * std::exp2 (envAmt * 3.0f * envVal));

        if (original)
        {
            // the synth feeds its drive / filter at 0.35 x the oscillator level
            constexpr double kOrigIn = 0.35, kOrigOut = 1.0 / 0.35;
            if (modCut != lastOrigCut)
            {
                origF = AOF::cutoffCoefAt (AOF::hzToU (modCut), sampleRate);
                lastOrigCut = modCut;
            }
            const double l = origDrive.process (L * kOrigIn);
            const double r = origDrive.process (R * kOrigIn);
            L = (float) (origFilt[0].process (ftyp, l, origF, origQ, fres) * kOrigOut);
            R = (float) (origFilt[1].process (ftyp, r, origF, origQ, fres) * kOrigOut);
        }
        else
        {
            L = filter (ladderL, svfL, ab::tanhA (L * driveGain) * driveComp, modCut);
            R = filter (ladderR, svfR, ab::tanhA (R * driveGain) * driveComp, modCut);
        }
        out[0] = { L, R };
        out[1] = { envVal, envVal };
    }

private:
    // what AlphaBetaFX's processBlock reads at the start of each block
    void readKnobs()
    {
        const float drive = param (pDrive);
        fcut = param (pCut);
        fres = param (pRes);
        ftyp = (int) std::lround (param (pType));
        const int rtyp = (int) std::lround (param (pCurve));
        envOn = param (pEnvOn) >= 0.5f;
        envAmt = param (pEnvAmt);
        envMidi = param (pEnvMidi) >= 0.5f;
        original = param (pMode) >= 0.5f;

        const int mode = original ? 1 : 0;
        if (mode != lastMode)
        {
            // the model switched to starts clean
            if (original) { for (auto& fl : origFilt) fl.reset(); lastOrigCut = -1.0; }
            else          { ladderL.reset(); ladderR.reset(); svfL.reset(); svfR.reset(); }
            lastMode = mode;
        }

        ladderL.resCurve = ladderR.resCurve = svfL.resCurve = svfR.resCurve = rtyp;
        env.setADSR (param (pAtk), param (pDcy), param (pSus), param (pRel));
        env.setMidiMode (envMidi);
        env.setRetriggerTime (param (pRetrig));

        driveGain = 1.0f + drive * 6.0f;
        driveComp = 1.0f / (0.4f + drive * 0.6f + 0.6f);
        if (original)
            origDrive.set (drive);
        origQ = AOF::resoDamp (fres);
    }

    float filter (ab::MoogLadder& ladder, ab::SVFilter& svf, float x, float cutoff) const
    {
        switch (ftyp)
        {
            case 0:  ladder.setParams (cutoff, fres); return ladder.process12 (x);
            case 2:  ladder.setParams (cutoff, fres); return ladder.process24plus (x);
            case 3:  svf.setParams (cutoff, fres);    return svf.process (x).bp;
            case 4:  svf.setParams (cutoff, fres);    return svf.process (x).hp;
            default: ladder.setParams (cutoff, fres); return ladder.process24 (x);
        }
    }

    ab::MoogLadder ladderL, ladderR;
    ab::SVFilter svfL, svfR;
    ab::FilterEnvelope env;
    AOF::Drive origDrive;
    AOF::Filter origFilt[2];

    float fcut { 2000.0f }, fres { 0.3f }, envAmt { 0.5f }, driveGain { 1.0f }, driveComp { 1.0f };
    int ftyp { 1 }, lastMode { -1 }, counter { 0 };
    bool envOn { false }, envMidi { false }, original { false };
    double lastOrigCut { -1.0 }, origF { 1.0 }, origQ { 1.0 };
};

//==============================================================================
// AlphaBetaFX - Chorus
//==============================================================================
class AlphaBetaFXChorusModule final : public SynthModule
{
public:
    enum { pMode, pWet, pTime, pRate, pOrigTime, pOrigRate };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("chorus", "Chorus", ModuleSection::Effect, 1);
        d.description = "AlphaBetaFX's stereo chorus in either model: Native (two modulated taps, Time and "
                        "Rate) or Original (FreeAlpha 3.3's four triangle-swept taps with its own Time and Rate "
                        "laws). Wet 0 is dry.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("mode", "Model", { "Native", "Original" }, 0, 0, 2).noMod(),
            makeRotary (ab::FXPID::HWET, "Wet", 0.0f, 1.0f, 0.0f, 0),
            makeRotary (ab::FXPID::HTIM, "Time", 1.0f, 50.0f, 15.0f, 0, "ms").visibleWhen ("mode", 0.0f),
            makeRotary (ab::FXPID::HRAT, "Rate", 0.1f, 10.0f, 0.5f, 0, "Hz").visibleWhen ("mode", 0.0f),
            makeRotary (ab::FXPID::OHTIM, "Time", (float) AOF::kChorusMsMin, (float) AOF::kChorusMsMax, 2.0f, 0, "ms", true)
                .visibleWhen ("mode", 1.0f),
            makeRotary (ab::FXPID::OHRAT, "Rate", (float) AOF::kChorusHzMin, (float) AOF::kChorusHzMax, 0.5f, 0, "Hz", true)
                .visibleWhen ("mode", 1.0f)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        chorus.prepare (sr, kControlBlock);
        origChorus.prepare (sr);
        origChorus.reset (param (pOrigRate));
        lastMode = -1;
        counter = 0;
    }

    void reset() override
    {
        chorus.reset();
        origChorus.reset (param (pOrigRate));
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (counter-- <= 0)
        {
            counter = kControlBlock - 1;
            original = param (pMode) >= 0.5f;
            wet = param (pWet);
            oTime = param (pOrigTime);
            oRate = param (pOrigRate);
            const int mode = original ? 1 : 0;
            if (mode != lastMode)
            {
                if (original) origChorus.reset (oRate);
                else          chorus.reset();
                lastMode = mode;
            }
            chorus.setWet (wet);
            chorus.setTime (param (pTime));
            chorus.setRate (param (pRate));
        }

        float l = in[0][0], r = in[0][1];
        if (original)
        {
            origChorus.processStereo (&l, &r, 1, wet, oTime, oRate);
        }
        else
        {
            float* chans[2] = { &l, &r };
            juce::AudioBuffer<float> one (chans, 2, 1);
            chorus.process (one);
        }
        out[0] = { l, r };
    }

private:
    ab::SimpleChorus chorus;
    AOF::Chorus origChorus;
    float wet { 0.0f }, oTime { 2.0f }, oRate { 0.5f };
    int lastMode { -1 }, counter { 0 };
    bool original { false };
};

AQUANODE_REGISTER_MODULE (AlphaBetaFXFilterModule, AlphaBetaFXFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (AlphaBetaFXChorusModule, AlphaBetaFXChorusModule::descriptor)

//==============================================================================
namespace aquanode::plugins::alphabetafx
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const auto mode = kit::param (original, ab::FXPID::MODE);

        const int filter = b.add ("plugin.alphabetafx.filter", 0, 0);
        b.copy (filter, original);
        b.set (filter, "mode", mode);

        const int chorus = b.add ("plugin.alphabetafx.chorus", 640, 0);
        b.copy (chorus, original);
        b.set (chorus, "mode", mode);
        b.cable (filter, "audioOut", chorus, "audioIn");

        PatchPorts ports;
        ports.name = "AlphaBetaFX";
        ports.audioIn = { { filter, "audioIn" } };
        ports.midiIn = { { filter, "midiIn" } };
        ports.audioOut = { { chorus, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
