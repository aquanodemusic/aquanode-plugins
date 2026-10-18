#include "EffectAdders.h"
#include "../PluginProcessor.h"
#include "../Modules/SpectralLockModule.h"

namespace aquanode::presets
{

namespace
{
    // one module of an added chain: its type and how to set it up
    struct Step
    {
        juce::String type;
        std::function<void (PatchSketch&, int id, Dice&)> setup;
    };

    int findAudioOut (AquanodeModularAudioProcessor& p)
    {
        for (int id : p.getModuleIds())
            if (auto* inst = p.getInstance (id))
                if (inst->descriptor().typeId == "io.audioOut")
                    return id;
        return -1;
    }

    // Puts `steps` in front of Audio Out: everything that fed Audio Out now
    // feeds the first step, the last step feeds Audio Out, and Audio Out
    // moves right to make room.
    void insertBeforeOutput (AquanodeModularAudioProcessor& p, const std::vector<Step>& steps, Dice& dice)
    {
        const int out = findAudioOut (p);
        if (out < 0 || steps.empty())
            return;

        const auto outPos = p.getInstance (out)->position;

        std::vector<std::pair<int, juce::String>> sources;
        const auto& cables = p.getCables();
        for (int i = (int) cables.size() - 1; i >= 0; --i)
            if (cables[(size_t) i].toModule == out && cables[(size_t) i].toSocket == "audioIn")
            {
                sources.push_back ({ cables[(size_t) i].fromModule, cables[(size_t) i].fromSocket });
                p.removeCable (i);
            }

        PatchSketch s (p);
        int prev = -1, x = outPos.x;
        for (const auto& step : steps)
        {
            const int id = s.add (step.type, 0);
            if (id < 0)
                continue;
            if (step.setup != nullptr)
                step.setup (s, id, dice);
            p.setModulePosition (id, { x, outPos.y });
            x += 300;

            if (prev < 0)
                for (const auto& src : sources)
                    p.addCable (src.first, src.second, id, "audioIn");
            else
                p.addCable (prev, "audioOut", id, "audioIn");
            prev = id;
        }

        if (prev >= 0)
        {
            p.addCable (prev, "audioOut", out, "audioIn");
            p.setModulePosition (out, { x, outPos.y });
        }
    }

    // ---- the chains ----------------------------------------------------------
    Step springer()
    {
        return { "fx.springer", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "sizeMs", d.range (20.0f, 45.0f));
            s.set (id, "coils", (float) (2 + d.index (3)));
            s.set (id, "damping", d.range (25.0f, 55.0f));
            s.set (id, "dryWet", d.range (20.0f, 35.0f));
        } };
    }

    Step aquaton()
    {
        return { "fx.aquaton", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "size", d.range (1.5f, 4.0f));
            s.set (id, "feedback", d.range (0.6f, 0.85f));
            s.set (id, "damping", d.range (30.0f, 60.0f));
            s.set (id, "dryWet", d.range (25.0f, 40.0f));
        } };
    }

    Step smoothHall()
    {
        return { "fx.smoothhall", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "time", d.range (2.0f, 6.0f));
            s.set (id, "preDelay", d.range (15.0f, 80.0f));
            s.set (id, "highDamp", d.range (5000.0f, 14000.0f));
            s.set (id, "dryWet", d.range (25.0f, 40.0f));
        } };
    }

    Step echo()
    {
        return { "fx.echo", [] (PatchSketch& s, int id, Dice& d)
        {
            static const char* divs[] = { "1/4", "1/8.", "1/8", "1/4." };
            if (! s.setChoice (id, "sync", "Tempo"))
                s.set (id, "time", d.range (280.0f, 600.0f));
            s.setChoice (id, "division", divs[d.index (4)]);
            s.set (id, "feedback", d.range (30.0f, 55.0f));
            s.set (id, "mix", d.range (18.0f, 30.0f));
            s.set (id, "wobble", d.range (0.0f, 30.0f));
        } };
    }

    Step crossDelay()
    {
        return { "fx.crossdelay", [] (PatchSketch& s, int id, Dice& d)
        {
            const float t = d.range (300.0f, 650.0f);
            s.set (id, "timeL", t);
            s.set (id, "timeR", t * d.range (1.15f, 1.5f));
            s.set (id, "feedbackL", d.range (25.0f, 45.0f));
            s.set (id, "feedbackR", d.range (25.0f, 45.0f));
            s.setChoice (id, "mode", d.chance (0.5f) ? "Cross" : "Stereo");
            s.set (id, "highDamp", d.range (4000.0f, 9000.0f));
            s.set (id, "dryWet", d.range (18.0f, 30.0f));
        } };
    }

    Step specFreeze()
    {
        return { "fx.specfreeze", [] (PatchSketch& s, int id, Dice& d)
        {
            s.setChoice (id, "freeze", "On");
            s.setChoice (id, "freezeMode", "Evolving Hold");
            s.set (id, "evolveRate", d.range (3.0f, 20.0f));
            s.set (id, "diffusion", d.range (20.0f, 60.0f));
            s.set (id, "dryWet", d.range (20.0f, 40.0f));
        } };
    }

    Step specDelay()
    {
        return { "fx.specdelay", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "maxTime", d.range (0.4f, 1.5f));
            s.set (id, "feedback", d.range (25.0f, 55.0f));
            s.set (id, "dryWet", d.range (25.0f, 45.0f));
        } };
    }

    Step specEnhance()
    {
        return { "fx.specenhance", [] (PatchSketch& s, int id, Dice& d)
        {
            s.setChoice (id, "mode", "Boost");
            s.set (id, "dryWet", d.range (40.0f, 80.0f));
        } };
    }

    Step spectralLock()
    {
        return { "fx.spectrallock", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "mix", d.range (35.0f, 65.0f));
            s.set (id, "amount", d.range (70.0f, 100.0f));
            s.set (id, "glide", d.range (10.0f, 40.0f));
            s.set (id, "shimmer", d.range (10.0f, 45.0f));
            s.set (id, "spray", d.range (0.0f, 25.0f));
        } };
    }

    Step liquidChorus()
    {
        return { "fx.liquidchor", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "speed", d.range (0.15f, 0.8f));
            s.set (id, "feedback", d.range (0.0f, 30.0f));
            s.set (id, "dryWet", d.range (30.0f, 50.0f));
        } };
    }

    Step ensemble()
    {
        return { "fx.ensemble", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "speed", d.range (2.0f, 5.0f));
            s.set (id, "mix", d.range (60.0f, 100.0f));
        } };
    }

    Step phaser()
    {
        return { "fx.stereophaser", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "rate", d.range (0.05f, 0.4f));
            s.set (id, "lrPhase", d.range (90.0f, 180.0f));
            s.set (id, "manual", d.range (800.0f, 6000.0f));
            s.set (id, "depth", d.range (30.0f, 60.0f));
            s.set (id, "resonance", d.range (20.0f, 60.0f));
            s.set (id, "dryWet", d.range (40.0f, 60.0f));
        } };
    }

    Step tapeWobble()
    {
        return { "fx.tapewobble", [] (PatchSketch& s, int id, Dice& d)
        {
            s.set (id, "wow", d.range (15.0f, 40.0f));
            s.set (id, "flutter", d.range (10.0f, 30.0f));
            s.set (id, "hiss", d.range (0.0f, 6.0f));
        } };
    }

    // picks `count` different entries of `pool`, keeping their order
    std::vector<Step> pick (const std::vector<Step>& pool, int count, Dice& dice)
    {
        std::vector<int> idx;
        for (int i = 0; i < (int) pool.size(); ++i) idx.push_back (i);
        for (int i = (int) idx.size() - 1; i > 0; --i) std::swap (idx[(size_t) i], idx[(size_t) dice.index (i + 1)]);
        idx.resize ((size_t) juce::jmin (count, (int) idx.size()));
        std::sort (idx.begin(), idx.end());
        std::vector<Step> out;
        for (int i : idx) out.push_back (pool[(size_t) i]);
        return out;
    }

    EffectAdder chainAdder (const juce::String& label, const juce::String& body,
                            std::function<std::vector<Step> (Dice&)> chooser)
    {
        EffectAdder a;
        a.id = "add." + label;
        a.label = label;
        a.tooltipBody = body;
        a.apply = [label, chooser] (AquanodeModularAudioProcessor& p, juce::uint32 seed) -> juce::String
        {
            Dice dice (seed);
            insertBeforeOutput (p, chooser (dice), dice);
            return label;
        };
        return a;
    }

    // Midi Add at the start of the chain: every generator hears the chord
    EffectAdder midiAddAdder()
    {
        EffectAdder a;
        a.id = "add.MidiAdd";
        a.label = "Midi Add";
        a.tooltipBody = "Adds a Midi Add at the start of the patch and patches its Midi Out into the Add Midi In "
                        "of every generator, so each played key becomes a chord (a different voicing with every "
                        "tap).";
        a.apply = [] (AquanodeModularAudioProcessor& p, juce::uint32 seed) -> juce::String
        {
            static const float voicings[][4] = {
                { 7, 12, 19, -12 }, { 4, 7, 12, -12 }, { 3, 7, 10, 12 }, { 5, 7, 12, 17 },
                { 7, 14, 19, 24 }, { 4, 11, 14, -12 }, { 3, 10, 14, 17 }, { 7, 12, 16, 24 } };
            Dice dice (seed);
            const auto& v = voicings[dice.index (8)];

            juce::Point<int> topLeft { 1 << 30, 1 << 30 };
            std::vector<int> targets;
            for (int id : p.getModuleIds())
                if (auto* inst = p.getInstance (id))
                {
                    topLeft.x = juce::jmin (topLeft.x, inst->position.x);
                    topLeft.y = juce::jmin (topLeft.y, inst->position.y);
                    if (inst->descriptor().inputIndexOf ("addMidiIn") >= 0)
                        targets.push_back (id);
                }
            if (targets.empty())
                return {};

            PatchSketch s (p);
            const int add = s.add ("util.midiadd", 0);
            if (add < 0)
                return {};
            for (int k = 0; k < 4; ++k)
                s.set (add, "note" + juce::String (k + 1), v[k]);
            p.setModulePosition (add, { topLeft.x - 300, topLeft.y });
            for (int t : targets)
                p.addCable (add, "midiOut", t, "addMidiIn");
            return "Midi Add";
        };
        return a;
    }
}

const std::vector<EffectAdder>& effectAdders()
{
    static const std::vector<EffectAdder> list = []
    {
        std::vector<EffectAdder> v;
        v.push_back (midiAddAdder());
        v.push_back (chainAdder ("Reverb",
            "Adds a reverb in front of Audio Out: a Springer spring, an AquaVerb or a Smooth Hall, sometimes "
            "a spring into a hall. Every tap is a different room.",
            [] (Dice& d)
            {
                std::vector<Step> v { d.chance (0.5f) ? aquaton() : smoothHall() };
                if (d.chance (0.4f)) v.insert (v.begin(), springer());
                return v;
            }));
        v.push_back (chainAdder ("Delay",
            "Adds an echo in front of Audio Out: a tempo Echo or a Stereo / Cross Delay.",
            [] (Dice& d) { return std::vector<Step> { d.chance (0.5f) ? echo() : crossDelay() }; }));
        v.push_back (chainAdder ("Modulation",
            "Adds movement in front of Audio Out: two of Liquid Chorus, Ensemble, Stereo Phaser and Tape "
            "Wobble.",
            [] (Dice& d) { return pick ({ liquidChorus(), ensemble(), phaser(), tapeWobble() }, 2, d); }));
        v.push_back (chainAdder ("Spectral",
            "Adds a spectral chain in front of Audio Out: two or three of Spectral Enhance, Spectral Delay "
            "and Spectral Freeze (evolving hold).",
            [] (Dice& d) { return pick ({ specEnhance(), specDelay(), specFreeze() }, 2 + d.index (2), d); }));
        v.push_back (chainAdder ("Color",
            "Adds a Spectral Lock in front of Audio Out: the sound is retuned to its scale and shimmers, mixed "
            "in with the dry signal.",
            [] (Dice&) { return std::vector<Step> { spectralLock() }; }));
        return v;
    }();
    return list;
}

void addSpectralLockEnding (AquanodeModularAudioProcessor& p, juce::uint32 seed, int scaleIndex)
{
    Dice dice (seed);
    auto step = spectralLock();
    const auto setup = step.setup;
    step.setup = [setup, scaleIndex] (PatchSketch& s, int id, Dice& d)
    {
        setup (s, id, d);
        const int sc = juce::jlimit (0, SpectralLockModule::numScales - 1, scaleIndex);
        for (int i = 0; i < 12; ++i)
            s.set (id, "matrix" + juce::String (i), (float) SpectralLockModule::scaleTables[sc][i]);
    };
    insertBeforeOutput (p, { step }, dice);
}

const EffectAdder* findEffectAdder (const juce::String& id)
{
    for (const auto& a : effectAdders())
        if (a.id == id)
            return &a;
    return nullptr;
}

} // namespace aquanode::presets
