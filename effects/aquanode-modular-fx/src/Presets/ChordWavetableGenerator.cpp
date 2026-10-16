#include "ChordWavetableGenerator.h"
#include "../PluginProcessor.h"
#include "../Modules/ChordWavetableModule.h"
#include "../Modules/MultiLFOModule.h"

// The colour-bass patch generator.
//
// What it builds follows the common colour-bass recipe, with every value
// drawn at random from generic, round ranges:
//   * a BODY layer: a chord wavetable (Chord Wavetable, Minor 9 / Major 9 ...)
//     near the played pitch, its spectral morph swept by a tempo LFO
//   * a SHIMMER layer: a saw or chord table with 8-16 unison voices STACKED
//     over a chord, transposed two or three octaves up onto a chord tone
//     (+24, +27, +31, +34 ...), quieter than the body
//   * optionally a SUB layer: a near-sine an octave down
//   * oscillator levels opened and closed by a tempo-synced shape LFO (the
//     rhythmic chop), with an ADSR on Env In for the note shape
//   * per voice: a comb filter tracking the played note (the keytracked
//     flanger/delay trick), drive, a low-pass whose cutoff follows the chop
//   * global: some of flanger / phaser / chorus / delay / reverb, then a
//     three-band compressor, bass-mono width and a limiter before the output
// Four archetypes vary the proportions: Chord (a chord-stacked bass), Growl, Pluck and Pad.

namespace aquanode
{
namespace
{
    enum Archetype { ChordBass = 0, Growl, Pluck, Pad };

    // chord indices in ChordWavetableModule::chordChoices() (0 = Auto)
    enum Chord { cMinor9 = 1, cMinor7, cMinor, cMajor9, cMajor7, cSus2, cSus4, cPower, cSaw, cSquare };
    // stack indices
    enum Stack { sNormal = 0, sOct, sOct2, sPower, sPower2, sMajor, sMinor, sHarmonic, sOddHarm };
    // morph types
    enum Morph { mOff = 0, mRandomAmp, mLowPass, mHighPass, mPhaseDisp };
    // Multi LFO shapes
    enum Shape { shSawDown = 0, shSawUp, shTriangle, shHoldDecay, shSpike, shPump, shSquare, shGate34,
                 shDoublePluck, shQuadChop, shStairs, shExpDecay, shRolled };
    // Multi LFO divisions
    enum Div { d8_1 = 0, d4_1, d2_1, d1_1, d1_2, d1_4, d1_8, d1_16, d1_32 };

    // consecutive seeds must not give correlated patches: juce::Random's
    // first outputs for nearby seeds are close, so scramble the seed first
    juce::int64 scrambleSeed (juce::uint32 x)
    {
        x += 0x9E3779B9u;
        x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
        x = (x ^ (x >> 13)) * 0xC2B2AE35u;
        x ^= x >> 16;
        return (juce::int64) x;
    }

    struct Builder
    {
        AquanodeModularAudioProcessor& p;
        juce::Random r;

        // layout: every module is created at the origin and placed at the end
        enum Row { rowMod, rowOsc, rowChain };
        std::vector<std::pair<int, Row>> placed;

        float range (float lo, float hi) { return juce::jmap (r.nextFloat(), lo, hi); }
        bool chance (float prob) { return r.nextFloat() < prob; }
        template <typename T> T pick (std::initializer_list<T> items)
        {
            const int n = (int) items.size();
            return *(items.begin() + r.nextInt (n));
        }
        template <typename T> T pickW (std::initializer_list<std::pair<T, float>> items)
        {
            float total = 0.0f;
            for (auto& it : items) total += it.second;
            float x = r.nextFloat() * total;
            for (auto& it : items) { x -= it.second; if (x <= 0.0f) return it.first; }
            return (items.end() - 1)->first;
        }

        int add (const char* type, Row row)
        {
            const int id = p.addModule (type, { 0, 0 });
            placed.push_back ({ id, row });
            return id;
        }

        // approximate card heights (world units) so a column never overlaps
        int cardHeight (int id)
        {
            auto* inst = p.getInstance (id);
            if (inst == nullptr) return 200;
            const auto& t = inst->descriptor().typeId;
            if (t == "osc.chordwavetable")             return 610;
            if (t == "util.multilfo")             return 390;
            if (t == "fx.threebandcomp")             return 310;
            if (t == "util.mixer")         return 240;
            if (t == "filter.combfilter" || t == "filter.formant") return 250;
            if (t == "filter.aquafilter")  return 230;
            if (t == "fx.phaser")          return 220;
            if (t == "fx.waveshaper")      return 200;
            if (t == "util.polysum" || t == "fx.stereowidth") return 185;
            return 170;
        }

        void layout()
        {
            constexpr int colW = 290, gap = 30, x0 = 40, y0 = 40;
            // row 1: modulators side by side
            int x = x0, rowModH = 0;
            for (auto& [id, row] : placed)
                if (row == rowMod)
                {
                    p.setModulePosition (id, { x, y0 });
                    rowModH = juce::jmax (rowModH, cardHeight (id));
                    x += colW;
                }
            // row 2: oscillators, then the signal chain flowing in columns
            // no taller than an oscillator
            const int y1 = y0 + rowModH + 50;
            x = x0;
            int oscH = 0;
            for (auto& [id, row] : placed)
                if (row == rowOsc)
                {
                    p.setModulePosition (id, { x, y1 });
                    oscH = juce::jmax (oscH, cardHeight (id));
                    x += colW;
                }
            const int colLimit = juce::jmax (oscH, 640);
            int y = y1;
            x += 20;
            for (auto& [id, row] : placed)
                if (row == rowChain)
                {
                    const int h = cardHeight (id);
                    if (y > y1 && y + h > y1 + colLimit)
                    {
                        x += colW;
                        y = y1;
                    }
                    p.setModulePosition (id, { x, y });
                    y += h + gap;
                }
        }

        void set (int id, const char* param, float value)
        {
            if (auto* inst = p.getInstance (id))
                inst->dsp->setParameter (param, value);
        }

        void wire (int from, const char* out, int to, const char* in) { p.addCable (from, out, to, in); }

        SynthModule* dsp (int id)
        {
            auto* inst = p.getInstance (id);
            return inst != nullptr ? inst->dsp.get() : nullptr;
        }
    };

}

juce::String buildChordWavetablePatch (AquanodeModularAudioProcessor& p, juce::uint32 seed, int forcedArchetype)
{
    Builder b { p, juce::Random (scrambleSeed (seed)), {} };

    // the archetype is always drawn, so a forced one leaves every other
    // random choice exactly where the same seed would put it
    Archetype arch = b.pickW<Archetype> ({ { ChordBass, 0.50f }, { Growl, 0.25f }, { Pluck, 0.13f }, { Pad, 0.12f } });
    if (forcedArchetype >= 0 && forcedArchetype <= Pad)
        arch = (Archetype) forcedArchetype;
    const bool minor = b.chance (0.6f);

    const int bodyChord = minor ? b.pick<int> ({ cMinor9, cMinor7, cMinor })
                                : b.pick<int> ({ cMajor9, cMajor7, cSus2 });
    const int shimmerStack = minor ? b.pick<int> ({ sMinor, sHarmonic, sPower2, sOct2 })
                                   : b.pick<int> ({ sMajor, sHarmonic, sPower2, sOct2 });
    // the shimmer sits two or three octaves up, on a tone of the chord
    const int shimmerTranspose = minor ? b.pick<int> ({ 24, 27, 31, 34, 36, 39, 43 })
                                       : b.pick<int> ({ 24, 28, 31, 35, 36, 40, 43 });

    // ---- modulators -------------------------------------------------------
    const int adsr = b.add ("util.adsr", Builder::rowMod);
    switch (arch)
    {
        case ChordBass:
            b.set (adsr, "attack", b.range (1.0f, 6.0f));
            b.set (adsr, "decay", b.range (600.0f, 2000.0f));
            b.set (adsr, "sustain", b.range (0.8f, 1.0f));
            b.set (adsr, "release", b.range (60.0f, 220.0f));
            break;
        case Growl:
            b.set (adsr, "attack", b.range (1.0f, 4.0f));
            b.set (adsr, "decay", b.range (500.0f, 1500.0f));
            b.set (adsr, "sustain", b.range (0.7f, 1.0f));
            b.set (adsr, "release", b.range (50.0f, 150.0f));
            break;
        case Pluck:
            b.set (adsr, "attack", b.range (1.0f, 3.0f));
            b.set (adsr, "decay", b.range (250.0f, 900.0f));
            b.set (adsr, "sustain", b.range (0.0f, 0.15f));
            b.set (adsr, "release", b.range (200.0f, 600.0f));
            break;
        case Pad:
            b.set (adsr, "attack", b.range (150.0f, 900.0f));
            b.set (adsr, "decay", b.range (800.0f, 3000.0f));
            b.set (adsr, "sustain", b.range (0.75f, 1.0f));
            b.set (adsr, "release", b.range (700.0f, 2500.0f));
            break;
    }

    // the chop: opens and closes the oscillator levels in time
    const int chop = b.add ("util.multilfo", Builder::rowMod);
    {
        int shape, div;
        switch (arch)
        {
            case ChordBass:
                shape = b.pickW<int> ({ { shSawDown, 0.25f }, { shHoldDecay, 0.15f }, { shTriangle, 0.12f }, { shQuadChop, 0.10f },
                                        { shDoublePluck, 0.10f }, { shGate34, 0.08f }, { shSpike, 0.08f }, { shRolled, 0.12f } });
                div = b.pickW<int> ({ { d1_4, 0.45f }, { d1_8, 0.25f }, { d1_2, 0.30f } });
                break;
            case Growl:
                shape = b.pickW<int> ({ { shRolled, 0.35f }, { shStairs, 0.15f }, { shSquare, 0.15f }, { shGate34, 0.15f }, { shHoldDecay, 0.2f } });
                div = b.pickW<int> ({ { d1_4, 0.4f }, { d1_2, 0.4f }, { d1_8, 0.2f } });
                break;
            case Pluck:
                shape = b.pickW<int> ({ { shExpDecay, 0.5f }, { shSawDown, 0.5f } });
                div = b.pickW<int> ({ { d1_2, 0.5f }, { d1_1, 0.5f } });
                break;
            default:
                shape = b.pickW<int> ({ { shTriangle, 0.6f }, { shSawUp, 0.2f }, { shRolled, 0.2f } });
                div = b.pickW<int> ({ { d2_1, 0.5f }, { d4_1, 0.5f } });
                break;
        }
        b.set (chop, "division", (float) div);
        b.set (chop, "min", arch == Pad ? b.range (0.35f, 0.7f) : (arch == Pluck ? 0.2f : 0.0f));
        b.set (chop, "smooth", b.range (1.0f, 4.0f));
        if (shape == shRolled)
        {
            if (auto* lfo = dynamic_cast<MultiLFOModule*> (b.dsp (chop)))
                lfo->rollShape (b.r.nextInt() | 1);
        }
        else
            b.set (chop, "shape", (float) shape);
    }

    // the shimmer sweep: spectral morph of every layer
    const int sweep = b.add ("util.multilfo", Builder::rowMod);
    {
        const int shape = b.pickW<int> ({ { shSawUp, 0.30f }, { shSawDown, 0.25f }, { shTriangle, 0.25f }, { shHoldDecay, 0.10f }, { shRolled, 0.10f } });
        if (shape == shRolled)
        {
            if (auto* lfo = dynamic_cast<MultiLFOModule*> (b.dsp (sweep)))
                lfo->rollShape (b.r.nextInt() | 1);
        }
        else
            b.set (sweep, "shape", (float) shape);
        const int div = arch == Pad ? b.pick<int> ({ d2_1, d4_1, d8_1 }) : b.pickW<int> ({ { d1_4, 0.35f }, { d1_2, 0.40f }, { d1_1, 0.25f } });
        b.set (sweep, "division", (float) div);
        b.set (sweep, "max", b.range (0.6f, 1.0f));
    }

    // growl: a vowel LFO for the formant filter, and a pitch yoink
    int vowelLfo = -1, yoink = -1;
    if (arch == Growl)
    {
        vowelLfo = b.add ("util.multilfo", Builder::rowMod);
        if (auto* lfo = dynamic_cast<MultiLFOModule*> (b.dsp (vowelLfo)))
            lfo->rollShape (b.r.nextInt() | 1);
        b.set (vowelLfo, "division", (float) b.pick<int> ({ d1_4, d1_8, d1_2 }));
        b.set (vowelLfo, "smooth", b.range (5.0f, 20.0f));

        if (b.chance (0.6f))
        {
            yoink = b.add ("util.multilfo", Builder::rowMod);
            b.set (yoink, "shape", (float) b.pick<int> ({ shSpike, shSawDown, shExpDecay }));
            b.set (yoink, "division", (float) b.pick<int> ({ d1_4, d1_2 }));
            b.set (yoink, "mode", 1.0f);          // one shot: a pitch drop at the start of the note
        }
    }

    const bool useComb = arch != Pad ? b.chance (0.55f) : b.chance (0.25f);
    int keytrack = -1;
    if (useComb)
        keytrack = b.add ("util.keytrack", Builder::rowMod);

    // ---- oscillators ------------------------------------------------------
    std::vector<int> oscs;
    auto makeOsc = [&] (int chord, int transpose, float volume)
    {
        const int id = b.add ("osc.chordwavetable", Builder::rowOsc);
        b.set (id, "chord", (float) chord);
        b.set (id, "transpose", (float) transpose);
        b.set (id, "volume", volume);
        oscs.push_back (id);
        return id;
    };
    auto finishOsc = [&] (int id)
    {
        if (auto* o = dynamic_cast<ChordWavetableModule*> (b.dsp (id)))
            o->setSeed ((juce::uint32) b.r.nextInt() | 1u);
    };

    // body
    const int body = makeOsc (arch == Growl && b.chance (0.5f) ? b.pick<int> ({ cSaw, cSquare }) : bodyChord,
                              arch == Pad ? b.pick<int> ({ 0, 12 }) : b.pickW<int> ({ { 0, 0.6f }, { 12, 0.4f } }),
                              b.range (0.65f, 0.85f));
    b.set (body, "keys", (float) b.pickW<int> ({ { 0, 0.4f }, { 1, 0.3f }, { 2, 0.2f }, { 3, 0.1f } }));
    b.set (body, "bright", b.range (50.0f, 90.0f));
    b.set (body, "density", b.range (45.0f, 90.0f));
    b.set (body, "morphType", (float) (arch == Growl ? b.pickW<int> ({ { mLowPass, 0.5f }, { mRandomAmp, 0.3f }, { mPhaseDisp, 0.2f } })
                                                       : b.pickW<int> ({ { mRandomAmp, 0.75f }, { mLowPass, 0.15f }, { mPhaseDisp, 0.10f } })));
    b.set (body, "morph", b.range (0.0f, 20.0f));
    b.set (body, "morphDepth", b.range (50.0f, 100.0f));
    b.set (body, "unison", (float) b.pickW<int> ({ { 1, 0.5f }, { 2, 0.35f }, { 3, 0.15f } }));
    b.set (body, "detune", b.range (4.0f, 14.0f));
    b.set (body, "spread", b.range (60.0f, 100.0f));
    finishOsc (body);

    // shimmer: the stacked unison layer up high
    const int shimmer = makeOsc (b.chance (0.6f) ? cSaw : bodyChord, shimmerTranspose,
                                 arch == Pad ? b.range (0.5f, 0.75f) : b.range (0.32f, 0.6f));
    b.set (shimmer, "stack", (float) shimmerStack);
    b.set (shimmer, "unison", (float) b.pickW<int> ({ { 16, 0.55f }, { 12, 0.15f }, { 8, 0.2f }, { 6, 0.1f } }));
    b.set (shimmer, "detune", b.range (6.0f, 28.0f));
    b.set (shimmer, "spread", 100.0f);
    b.set (shimmer, "bright", b.range (55.0f, 95.0f));
    b.set (shimmer, "density", b.range (40.0f, 80.0f));
    b.set (shimmer, "morphType", (float) b.pickW<int> ({ { mRandomAmp, 0.8f }, { mLowPass, 0.2f } }));
    b.set (shimmer, "morph", b.range (0.0f, 35.0f));
    b.set (shimmer, "morphDepth", b.range (40.0f, 100.0f));
    finishOsc (shimmer);

    // sub
    int sub = -1;
    if (arch != Pad && b.chance (arch == Growl ? 0.7f : 0.5f))
    {
        sub = makeOsc (cSaw, b.pick<int> ({ -12, 0 }), b.range (0.4f, 0.65f));
        b.set (sub, "bright", 0.0f);
        b.set (sub, "morphType", (float) mLowPass);
        b.set (sub, "morph", b.range (78.0f, 90.0f));    // a saw low-passed down to almost a sine
        b.set (sub, "morphDepth", 0.0f);
        b.set (sub, "spread", 0.0f);
        finishOsc (sub);
    }

    // wiring the modulators into the oscillators
    for (int id : oscs)
        b.wire (adsr, "modOut", id, "envIn");

    // the chop hits body and shimmer; the sub mostly stays steady under it
    b.wire (chop, "modOut", body, "levelIn");
    b.wire (chop, "modOut", shimmer, "levelIn");
    if (sub >= 0 && b.chance (0.4f))
        b.wire (chop, "modOut", sub, "levelIn");

    b.wire (sweep, "modOut", body, "morphIn");
    b.wire (sweep, b.chance (0.3f) ? "invOut" : "modOut", shimmer, "morphIn");
    if (b.chance (0.35f))
    {
        b.wire (sweep, "modOut", body, "posIn");
        b.set (body, "posDepth", b.range (40.0f, 100.0f));
    }

    if (yoink >= 0)
    {
        b.wire (yoink, "modOut", body, "pitchIn");
        b.set (body, "pitchDepth", b.pick<float> ({ 12.0f, 7.0f, -12.0f, 24.0f }));
        if (sub >= 0)
        {
            b.wire (yoink, "modOut", sub, "pitchIn");
            b.set (sub, "pitchDepth", 12.0f);
        }
    }

    // ---- per-voice processing ---------------------------------------------
    const int mixer = b.add ("util.mixer", Builder::rowChain);
    {
        const char* ins[] = { "in1", "in2", "in3", "in4" };
        for (size_t i = 0; i < oscs.size() && i < 4; ++i)
            b.wire (oscs[i], "audioOut", mixer, ins[i]);
        b.set (mixer, "master", 0.9f);
    }
    int chain = mixer;
    const char* chainOut = "audioOut";

    auto insert = [&] (int id, const char* in = "audioIn", const char* out = "audioOut")
    {
        b.wire (chain, chainOut, id, in);
        chain = id;
        chainOut = out;
    };

    if (useComb)
    {
        const int comb = b.add ("filter.combfilter", Builder::rowChain);
        // tuned to the played note (or an octave up): KeyTrack is centred on
        // C4 and moves the comb one semitone per semitone
        b.set (comb, "freq", 261.63f * b.pick<float> ({ 1.0f, 2.0f, 0.5f }));
        b.set (comb, "feedback", b.range (40.0f, 82.0f) * (b.chance (0.25f) ? -1.0f : 1.0f));
        b.set (comb, "damping", b.range (30.0f, 75.0f));
        b.set (comb, "blend", b.range (40.0f, 100.0f));
        b.set (comb, "pitchDepth", 100.0f);
        b.set (comb, "dryWet", b.range (25.0f, 60.0f));
        b.wire (keytrack, "modOut", comb, "pitchIn");
        insert (comb);
    }

    if (arch == Growl)
    {
        const int formant = b.add ("filter.formant", Builder::rowChain);
        b.set (formant, "vowel", b.range (0.0f, 40.0f));
        b.set (formant, "size", b.range (-4.0f, 4.0f));
        b.set (formant, "resonance", b.range (70.0f, 92.0f));
        b.set (formant, "vowelMod", b.range (50.0f, 100.0f));
        b.set (formant, "dryWet", b.range (55.0f, 90.0f));
        b.wire (vowelLfo, "modOut", formant, "vowelIn");
        insert (formant);
    }

    if (arch != Pad && b.chance (arch == Growl ? 0.95f : 0.7f))
    {
        const int shaper = b.add ("fx.waveshaper", Builder::rowChain);
        b.set (shaper, "shape", (float) b.pickW<int> ({ { 0, 0.6f }, { 1, 0.25f }, { 2, 0.15f } }));
        b.set (shaper, "drive", arch == Growl ? b.range (6.0f, 16.0f) : b.range (2.5f, 8.0f));
        b.set (shaper, "out", 0.85f);
        b.set (shaper, "dryWet", b.range (50.0f, 100.0f));
        insert (shaper);
    }

    // colour bass is bright: an open filter that the chop closes a little,
    // on about half the patches (always on pads)
    if (arch == Pad || b.chance (0.5f))
    {
        const int filter = b.add ("filter.aquafilter", Builder::rowChain);
        b.set (filter, "type", 1.0f);             // LP24
        const float cutoff = arch == Pad ? b.range (4000.0f, 12000.0f) : b.range (7000.0f, 18000.0f);
        b.set (filter, "cutoff", cutoff);
        b.set (filter, "resonance", b.range (0.1f, 0.4f));
        if (arch != Pad && b.chance (0.5f))
        {
            b.wire (chop, "modOut", filter, "cutoffMod");
            b.set (filter, "cutoff", cutoff * 0.7f);
            b.set (filter, "modDepth", b.range (12.0f, 30.0f));
        }
        insert (filter);
    }

    const int polysum = b.add ("util.polysum", Builder::rowChain);
    insert (polysum);

    // ---- global effects ---------------------------------------------------
    if (b.chance (arch == Pad ? 0.5f : 0.45f))
    {
        const int fl = b.add ("fx.flanger", Builder::rowChain);
        b.set (fl, "rate", b.range (0.1f, 1.2f));
        b.set (fl, "depth", b.range (20.0f, 70.0f));
        b.set (fl, "manual", b.range (0.5f, 4.0f));
        b.set (fl, "feedback", b.range (-60.0f, 60.0f));
        b.set (fl, "dryWet", b.range (25.0f, 50.0f));
        insert (fl);
    }
    if (b.chance (arch == Growl ? 0.5f : 0.3f))
    {
        const int ph = b.add ("fx.phaser", Builder::rowChain);
        b.set (ph, "rate", b.range (0.1f, 1.5f));
        b.set (ph, "depth", b.range (30.0f, 80.0f));
        b.set (ph, "centre", b.range (400.0f, 3000.0f));
        b.set (ph, "feedback", b.range (20.0f, 60.0f));
        b.set (ph, "dryWet", b.range (30.0f, 70.0f));
        insert (ph);
    }
    if (b.chance (arch == Pad ? 0.8f : 0.45f))
    {
        const int ch = b.add ("fx.chorus", Builder::rowChain);
        b.set (ch, "rate", b.range (0.2f, 1.2f));
        b.set (ch, "depth", b.range (30.0f, 70.0f));
        b.set (ch, "baseDelay", b.range (5.0f, 12.0f));
        b.set (ch, "spread", b.range (60.0f, 100.0f));
        b.set (ch, "dryWet", b.range (25.0f, 55.0f));
        insert (ch);
    }
    if (b.chance (arch == Pad || arch == Pluck ? 0.7f : 0.2f))
    {
        const int dl = b.add ("fx.delay", Builder::rowChain);
        b.set (dl, "timeL", (float) b.pick<int> ({ 1, 2 }));     // 1/8 or 1/4
        b.set (dl, "timeR", (float) b.pick<int> ({ 1, 2, 3 }));
        b.set (dl, "feedback", b.range (0.2f, 0.5f));
        b.set (dl, "highPass", b.range (200.0f, 700.0f));
        b.set (dl, "dryWet", arch == Pad || arch == Pluck ? b.range (15.0f, 35.0f) : b.range (8.0f, 18.0f));
        insert (dl);
    }

    // 3-Band Compressor: on nearly every patch
    {
        const int comp = b.add ("fx.threebandcomp", Builder::rowChain);
        b.set (comp, "depth", arch == Pad ? b.range (25.0f, 50.0f) : b.range (45.0f, 85.0f));
        b.set (comp, "time", b.range (30.0f, 70.0f));
        b.set (comp, "lowGain", b.range (3.0f, 6.0f));
        b.set (comp, "highGain", b.range (5.0f, 10.0f));
        b.set (comp, "inGain", b.range (4.0f, 8.0f));
        insert (comp);
    }

    if (b.chance (arch == Pad ? 0.95f : (arch == Pluck ? 0.6f : 0.15f)))
    {
        const int rv = b.add ("fx.reverb", Builder::rowChain);
        b.set (rv, "roomSize", b.range (40.0f, 85.0f));
        b.set (rv, "decay", arch == Pad ? b.range (2.5f, 7.0f) : b.range (1.0f, 2.5f));
        b.set (rv, "damping", b.range (30.0f, 65.0f));
        b.set (rv, "preDelay", b.range (5.0f, 40.0f));
        b.set (rv, "dryWet", arch == Pad ? b.range (25.0f, 45.0f) : b.range (10.0f, 22.0f));
        insert (rv);
    }

    if (arch != Pad)
    {
        const int width = b.add ("fx.stereowidth", Builder::rowChain);
        b.set (width, "width", b.range (100.0f, 140.0f));
        b.set (width, "bassMono", b.range (100.0f, 180.0f));
        insert (width);
    }

    const int limiter = b.add ("fx.limiter", Builder::rowChain);
    // the genre is mastered hot; the limiter makes up the level safely
    b.set (limiter, "gain", arch == Pad ? 12.0f : 9.0f);
    b.set (limiter, "ceiling", -0.8f);
    insert (limiter);

    const int out = b.add ("io.audioOut", Builder::rowChain);
    b.set (out, "level", 0.9f);
    insert (out, "audioIn", "");

    b.layout();

    return "Colorbass " + chordWavetableArchetypeNames()[arch] + " - " + ChordWavetableModule::chordChoices()[bodyChord];
}

} // namespace aquanode
