//==============================================================================
// 83ChorusVerb - Patch version
//
// The plugin's four effects as modules of their own, running the plugin's own
// DSP classes (cv83::Chorus, Delay, Phaser, Reverb) sample by sample:
//
//     Audio In -> Chorus -> Delay -> Phaser -> Reverb -> Audio Out
//
// Every knob keeps the plugin's parameter id and range, so presets and the GUI
// card's state copy across one to one, and every knob takes cables. Each stage
// keeps its On switch with the plugin's click-free enable ramp. VirtualDX7's
// patch version uses the same four modules for its effects section - they are
// the same effects.
//==============================================================================
#include "PluginChorusVerbConfig.h"

namespace aqp_chorusverb
{
    #include AQP_CHORUSVERB_SRC(PluginProcessor.h)
}

#include "PatchKit.h"
#include "ChorusVerbPatch.h"

namespace aquanode::plugins::chorusverb
{
using namespace aquanode;
namespace ids = aqp_chorusverb::cv83::ids;

namespace
{
    const juce::String kPlugin { "83chorusverb" };

    ModuleDescriptor baseDescriptor (const juce::String& typeId, const juce::String& kind, int order)
    {
        ModuleDescriptor d;
        d.typeId = typeId;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        return d;
    }

    //==========================================================================
    // One effect stage: owns the plugin's effect object and hands it the
    // module's (modulated) knob values whenever they change.
    template <typename Fx>
    class StageModule : public SynthModule
    {
    public:
        VoiceMode voiceMode() const override { return VoiceMode::Global; }

        void prepare (double sr) override
        {
            SynthModule::prepare (sr);
            fx.prepare (sr, 1);
            lastValues.assign (getDescriptor().params.size(), std::numeric_limits<float>::quiet_NaN());
        }

        void reset() override { fx.reset(); }

        void processSample (const StereoFrame* in, StereoFrame* out) override
        {
            bool changed = false;
            for (size_t i = 0; i < lastValues.size(); ++i)
            {
                const float v = param ((int) i);
                if (v != lastValues[i])
                {
                    lastValues[i] = v;
                    changed = true;
                }
            }
            if (changed)
                apply (fx, lastValues);

            float l = in[0][0], r = in[0][1];
            fx.process (&l, &r, 1);
            out[0] = { l, r };
        }

    protected:
        // the plugin's own setter calls, in its own order (cv83::FxChain::process)
        virtual void apply (Fx& effect, const std::vector<float>& v) = 0;

        Fx fx;
        std::vector<float> lastValues;
    };

    //==========================================================================
    using CvChorus = aqp_chorusverb::cv83::Chorus;
    using CvDelay  = aqp_chorusverb::cv83::Delay;
    using CvPhaser = aqp_chorusverb::cv83::Phaser;
    using CvReverb = aqp_chorusverb::cv83::Reverb;

    enum { cOn, cRate, cDepth, cDelay, cSpread, cMix, cVoices };
    enum { dOn, dTimeL, dTimeR, dFb, dHp, dMix, dSelfFb };
    enum { pOn, pRate, pDepth, pCentre, pFb, pStages, pMix };
    enum { rOn, rSize, rFb, rDamp, rRate, rDepth, rFreeze, rMix };
}

ModuleDescriptor chorusDescriptor()
{
    auto d = baseDescriptor ("plugin.83chorusverb.chorus", "Chorus", 0);
    d.description = "83ChorusVerb's (and VirtualDX7's) chorus: a modulated delay line per channel, the LFO "
                    "phase offset between them is Spread, and Unison reads 1-6 taps for an ensemble.";
    d.params = {
        kit::makeOnOff (ids::chorusOn, "On", true, 0),
        makeRotary (ids::chorusRate,   "Rate",   0.1f, 10.0f, 0.5f, 0, "Hz", true),
        makeRotary (ids::chorusDepth,  "Depth",  0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary (ids::chorusDelay,  "Delay",  1.0f, 20.0f, 7.0f, 0, "ms"),
        makeRotary (ids::chorusSpread, "Spread", 0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary (ids::chorusMix,    "Mix",    0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary (ids::chorusVoices, "Unison", 1.0f, (float) CvChorus::maxVoices, 1.0f, 1, {}, false, 1.0f)
    };
    return d;
}

ModuleDescriptor delayDescriptor()
{
    auto d = baseDescriptor ("plugin.83chorusverb.delay", "Delay", 1);
    d.description = "The stereo delay with its own time per channel and a high-pass in the feedback loop. "
                    "Self-Feedback lets Feedback above 1.00 run away into self-oscillation.";
    d.params = {
        kit::makeOnOff (ids::delayOn, "On", true, 0),
        makeRotary (ids::delayTimeL, "Time L",   20.0f, (float) CvDelay::maxTimeMs, 250.0f, 0, "ms", true),
        makeRotary (ids::delayTimeR, "Time R",   20.0f, (float) CvDelay::maxTimeMs, 375.0f, 0, "ms", true),
        makeRotary (ids::delayFb,    "Feedback", 0.0f, 1.2f, 0.3f, 0),
        makeRotary (ids::delayHp,    "High-Pass", 20.0f, 2000.0f, 100.0f, 1, "Hz", true),
        makeRotary (ids::delayMix,   "Mix",      0.0f, 100.0f, 35.0f, 1, "%"),
        kit::makeOnOff (ids::delaySelfFb, "Self-Fb", false, 1)
    };
    return d;
}

ModuleDescriptor phaserDescriptor()
{
    auto d = baseDescriptor ("plugin.83chorusverb.phaser", "Phaser", 2);
    d.description = "2-12 allpass stages per channel swept by an LFO around Center, with feedback.";
    d.params = {
        kit::makeOnOff (ids::phaserOn, "On", false, 0),
        makeRotary (ids::phaserRate,   "Rate",     0.05f, 10.0f, 0.5f, 0, "Hz", true),
        makeRotary (ids::phaserDepth,  "Depth",    0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary (ids::phaserCentre, "Center",   100.0f, 10000.0f, 800.0f, 0, "Hz", true),
        makeRotary (ids::phaserFb,     "Feedback", 0.0f, 100.0f, 20.0f, 1, "%"),
        makeSteppedList (ids::phaserStages, "Stages", { "2", "4", "6", "8", "10", "12" }, 1, 1),
        makeRotary (ids::phaserMix,    "Mix",      0.0f, 100.0f, 50.0f, 1, "%")
    };
    return d;
}

ModuleDescriptor reverbDescriptor()
{
    auto d = baseDescriptor ("plugin.83chorusverb.reverb", "Reverb", 3);
    d.description = "The modulated diffusion reverb, with Freeze holding the tail indefinitely.";
    d.params = {
        kit::makeOnOff (ids::reverbOn, "On", true, 0),
        makeRotary (ids::reverbSize,  "Size",      0.1f, 8.0f, 1.0f, 0, {}, true),
        makeRotary (ids::reverbFb,    "Feedback",  0.0f, 1.2f, 0.7f, 0),
        makeRotary (ids::reverbDamp,  "Damping",   0.0f, 100.0f, 40.0f, 0, "%"),
        makeRotary (ids::reverbRate,  "Mod Rate",  0.01f, 8.0f, 0.25f, 1, "Hz", true),
        makeRotary (ids::reverbDepth, "Mod Depth", 0.0f, 50.0f, 4.0f, 1),
        kit::makeOnOff (ids::reverbFreeze, "Freeze", false, 1),
        makeRotary (ids::reverbMix,   "Mix",       0.0f, 100.0f, 35.0f, 1, "%")
    };
    return d;
}

} // namespace aquanode::plugins::chorusverb

//==============================================================================
using namespace aquanode;
using namespace aquanode::plugins::chorusverb;

class ChorusVerbChorusModule final : public StageModule<CvChorus>
{
    void apply (CvChorus& fx, const std::vector<float>& v) override
    {
        fx.setEnabled (v[cOn] >= 0.5f);
        fx.setRate (v[cRate]);
        fx.setDepth (v[cDepth]);
        fx.setBaseDelay (v[cDelay]);
        fx.setSpread (v[cSpread]);
        fx.setVoices (juce::roundToInt (v[cVoices]));
        fx.setDryWet (v[cMix]);
    }
};

class ChorusVerbDelayModule final : public StageModule<CvDelay>
{
    void apply (CvDelay& fx, const std::vector<float>& v) override
    {
        fx.setEnabled (v[dOn] >= 0.5f);
        fx.setTimeL (v[dTimeL]);
        fx.setTimeR (v[dTimeR]);
        fx.setFeedback (v[dSelfFb] >= 0.5f ? v[dFb] : juce::jmin (1.0f, v[dFb]));
        fx.setHighPass (v[dHp]);
        fx.setDryWet (v[dMix]);
    }
};

class ChorusVerbPhaserModule final : public StageModule<CvPhaser>
{
    void apply (CvPhaser& fx, const std::vector<float>& v) override
    {
        fx.setEnabled (v[pOn] >= 0.5f);
        fx.setRate (v[pRate]);
        fx.setDepth (v[pDepth]);
        fx.setCentre (v[pCentre]);
        fx.setFeedback (v[pFb]);
        fx.setStages ((int) v[pStages]);
        fx.setDryWet (v[pMix]);
    }
};

class ChorusVerbReverbModule final : public StageModule<CvReverb>
{
    void apply (CvReverb& fx, const std::vector<float>& v) override
    {
        fx.setEnabled (v[rOn] >= 0.5f);
        fx.setSize (v[rSize]);
        fx.setFeedback (v[rFb]);
        fx.setDamping (v[rDamp]);
        fx.setModRate (v[rRate]);
        fx.setModDepth (v[rDepth]);
        fx.setFreeze (v[rFreeze] >= 0.5f);
        fx.setDryWet (v[rMix]);
    }
};

AQUANODE_REGISTER_MODULE (ChorusVerbChorusModule, aquanode::plugins::chorusverb::chorusDescriptor)
AQUANODE_REGISTER_MODULE (ChorusVerbDelayModule,  aquanode::plugins::chorusverb::delayDescriptor)
AQUANODE_REGISTER_MODULE (ChorusVerbPhaserModule, aquanode::plugins::chorusverb::phaserDescriptor)
AQUANODE_REGISTER_MODULE (ChorusVerbReverbModule, aquanode::plugins::chorusverb::reverbDescriptor)

//==============================================================================
namespace aquanode::plugins::chorusverb
{
    Chain buildChain (kit::Builder& b, juce::AudioProcessor& original, int x, int y,
                      const std::function<juce::String (const juce::String&)>& map)
    {
        Chain c;
        c.chorus = b.add ("plugin.83chorusverb.chorus", x, y);
        c.delay  = b.add ("plugin.83chorusverb.delay",  x + 300, y);
        c.phaser = b.add ("plugin.83chorusverb.phaser", x + 600, y);
        c.reverb = b.add ("plugin.83chorusverb.reverb", x + 900, y);

        for (int m : { c.chorus, c.delay, c.phaser, c.reverb })
            b.copy (m, original, map);

        b.cable (c.chorus, "audioOut", c.delay,  "audioIn");
        b.cable (c.delay,  "audioOut", c.phaser, "audioIn");
        b.cable (c.phaser, "audioOut", c.reverb, "audioIn");
        return c;
    }

    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const auto c = buildChain (b, original, 0, 0, {});

        PatchPorts ports;
        ports.name = "83ChorusVerb";
        ports.audioIn = { { c.chorus, "audioIn" } };
        ports.audioOut = { { c.reverb, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
