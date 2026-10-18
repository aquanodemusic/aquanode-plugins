#include "RandomAmpGrowl.h"
#include "../PluginProcessor.h"
#include "../Modules/MultiLFOModule.h"

namespace aquanode::presets
{

juce::String buildRandomAmpGrowl (AquanodeModularAudioProcessor& p, juce::uint32 seed)
{
    PatchSketch s (p);
    Dice d (seed ^ 0x9e3779b9u);

    // ---- modulators --------------------------------------------------------------
    // the swell: the layers fade in over a short attack and out on release
    const int adsr = s.add ("util.adsr", 0);
    s.set (adsr, "attack", d.range (20.0f, 180.0f));
    s.set (adsr, "decay", d.range (600.0f, 2000.0f));
    s.set (adsr, "sustain", d.range (0.8f, 1.0f));
    s.set (adsr, "release", d.range (90.0f, 320.0f));

    // the morph sweep: mostly one-shot, so every note walks through the
    // patterns once and lands on its last one; sometimes it loops in time
    auto sweepLfo = [&] (bool loop)
    {
        const int lfo = s.add ("util.multilfo", 0);
        const auto shape = loop ? d.chance (0.5f) ? juce::String ("Rolled")
                                                  : juce::StringArray { "Triangle", "Saw Up", "Hold Decay", "Stairs Down" }[d.index (4)]
                                : juce::StringArray { "Saw Up", "Saw Up", "Hold Decay", "Saw Down", "Exp Decay", "Triangle", "Rolled" }[d.index (7)];
        if (shape == "Rolled")
        {
            if (auto* m = dynamic_cast<MultiLFOModule*> (s.dsp (lfo)))
                m->rollShape ((juce::uint32) d.r.nextInt() | 1u);
        }
        else
            s.setChoice (lfo, "shape", shape);
        s.setChoice (lfo, "division", loop ? juce::StringArray { "1/8", "1/4", "1/4", "1/2" }[d.index (4)]
                                           : juce::StringArray { "1/8", "1/4", "1/2", "1/2", "1/1" }[d.index (5)]);
        s.setChoice (lfo, "mode", loop ? "Retrigger" : "One Shot");
        s.set (lfo, "smooth", d.range (2.0f, 12.0f));
        return lfo;
    };
    const bool looping = d.chance (0.3f);
    const int sweep = sweepLfo (looping);
    const int sweep2 = d.chance (0.4f) ? sweepLfo (looping || d.chance (0.3f)) : sweep;

    // the sub's pulse
    const int pulse = s.add ("util.multilfo", 0);
    s.setChoice (pulse, "shape", juce::StringArray { "Pump", "Square", "Gate 3/4", "Triangle" }[d.index (4)]);
    s.setChoice (pulse, "division", d.chance (0.6f) ? "1/4" : "1/8");
    s.set (pulse, "smooth", d.range (3.0f, 10.0f));

    // ---- oscillators ---------------------------------------------------------------
    auto warp = [&] (float shape, int transpose, int unison, float volume)
    {
        const int o = s.add ("osc.warp", 1);
        s.set (o, "shape", shape);
        s.set (o, "transpose", (float) transpose);
        s.set (o, "unison", (float) unison);
        s.set (o, "volume", volume);
        s.setChoice (o, "morphType", "Random Amp");
        return o;
    };

    // body: a triangle-to-saw blend, wide unison, its morph swept by the note
    const int body = warp (d.range (30.0f, 62.0f), 0, juce::StringArray { "5", "7", "9", "9", "12" }[d.index (5)].getIntValue(),
                           d.range (0.6f, 0.8f));
    s.set (body, "detune", d.range (14.0f, 45.0f));
    s.set (body, "spread", d.range (80.0f, 100.0f));
    s.set (body, "blend", d.range (65.0f, 90.0f));
    s.set (body, "morph", d.range (0.0f, 12.0f));
    if (d.chance (0.3f))
    {
        s.setChoice (body, "warpMode", juce::StringArray { "Bend", "Sync", "Mirror", "Formant" }[d.index (4)]);
        s.set (body, "warp", d.range (8.0f, 35.0f));
    }

    // top: one or two octaves (or a twelfth) up, mostly a single voice
    const int top = warp (d.range (40.0f, 80.0f), juce::StringArray { "24", "24", "12", "19" }[d.index (4)].getIntValue(),
                          d.chance (0.7f) ? 1 : 3, d.range (0.3f, 0.55f));
    s.set (top, "detune", d.range (6.0f, 20.0f));
    s.set (top, "morph", d.range (0.0f, 8.0f));

    // sub: a plain sine, pulsed
    const int sub = s.add ("osc.warp", 1);
    s.set (sub, "shape", 0.0f);
    s.set (sub, "transpose", d.chance (0.5f) ? -12.0f : 0.0f);
    s.set (sub, "unison", 1.0f);
    s.setChoice (sub, "morphType", "None");
    s.set (sub, "volume", 0.15f);

    for (int o : { body, top, sub })
        s.wire (adsr, "modOut", o, "envIn");
    s.mod (sweep, "modOut", body, "morph", d.range (0.7f, 1.0f));
    s.mod (sweep2, d.chance (0.25f) ? "invOut" : "modOut", top, "morph", d.range (0.5f, 1.0f));
    s.mod (pulse, "modOut", sub, "volume", d.range (0.35f, 0.6f));

    // ---- the voice chain ---------------------------------------------------------------
    const int mixer = s.add ("util.mixer", 2);
    s.wire (body, "audioOut", mixer, "in1");
    s.wire (top, "audioOut", mixer, "in2");
    s.wire (sub, "audioOut", mixer, "in3");
    s.set (mixer, "master", 0.75f);

    int chain = mixer;
    auto insert = [&] (int id, int column)
    {
        juce::ignoreUnused (column);
        s.wire (chain, "audioOut", id, "audioIn");
        chain = id;
    };

    if (d.chance (0.45f))
    {
        const int shaper = s.add ("fx.waveshaper", 3);
        s.set (shaper, "drive", d.range (1.5f, 5.0f));
        s.set (shaper, "out", 0.8f);
        s.set (shaper, "dryWet", d.range (30.0f, 70.0f));
        insert (shaper, 3);
    }
    if (d.chance (0.5f))
    {
        const int filter = s.add ("filter.aquafilter", 3);
        s.setChoice (filter, "type", "LP24");
        s.set (filter, "cutoff", d.range (5000.0f, 14000.0f));
        s.set (filter, "resonance", d.range (0.05f, 0.3f));
        insert (filter, 3);
    }

    const int polysum = s.add ("util.polysum", 4);
    insert (polysum, 4);

    // ---- space -------------------------------------------------------------------------
    if (d.chance (0.75f))
    {
        const int ch = s.add ("fx.chorus", 5);
        s.set (ch, "rate", d.range (0.2f, 1.0f));
        s.set (ch, "depth", d.range (30.0f, 70.0f));
        s.set (ch, "baseDelay", d.range (5.0f, 12.0f));
        s.set (ch, "spread", d.range (70.0f, 100.0f));
        s.set (ch, "dryWet", d.range (25.0f, 50.0f));
        insert (ch, 5);
    }
    if (d.chance (0.55f))
    {
        const int fl = s.add ("fx.flanger", 5);
        s.set (fl, "rate", d.range (0.1f, 0.8f));
        s.set (fl, "depth", d.range (20.0f, 60.0f));
        s.set (fl, "manual", d.range (0.5f, 3.0f));
        s.set (fl, "feedback", d.range (-50.0f, 50.0f));
        s.set (fl, "dryWet", d.range (20.0f, 45.0f));
        insert (fl, 5);
    }
    if (d.chance (0.6f))
    {
        const int dl = s.add ("fx.delay", 6);
        s.setChoice (dl, "timeL", d.chance (0.5f) ? "1/8" : "1/4");
        s.setChoice (dl, "timeR", d.chance (0.5f) ? "1/4" : "1/2");
        s.set (dl, "feedback", d.range (0.15f, 0.4f));
        s.set (dl, "highPass", d.range (200.0f, 600.0f));
        s.set (dl, "dryWet", d.range (12.0f, 28.0f));
        insert (dl, 6);
    }
    if (d.chance (0.7f))
    {
        const int rv = s.add ("fx.reverb", 6);
        s.set (rv, "roomSize", d.range (40.0f, 80.0f));
        s.set (rv, "decay", d.range (1.2f, 3.5f));
        s.set (rv, "damping", d.range (30.0f, 60.0f));
        s.set (rv, "dryWet", d.range (12.0f, 28.0f));
        insert (rv, 6);
    }

    const int out = s.add ("io.audioOut", 7);
    s.set (out, "level", 0.8f);
    s.wire (chain, "audioOut", out, "audioIn");

    s.layout();
    return "Random Amp Growl";
}

} // namespace aquanode::presets
