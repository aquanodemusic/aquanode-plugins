#include "GenreGenerator.h"
#include "../PluginProcessor.h"
#include "../Modules/WarpOscModule.h"

namespace aquanode::presets::genre
{
    extern const char* const kRecipeChunks[];
    extern const int kNumRecipeChunks;

namespace
{
    //==========================================================================
    // the recipe table, parsed once
    //==========================================================================
    const juce::Array<juce::var>& recipes()
    {
        static const juce::Array<juce::var> list = []
        {
            juce::String text;
            for (int i = 0; i < kNumRecipeChunks; ++i)
                text << kRecipeChunks[i];
            juce::Array<juce::var> out;
            const auto parsed = juce::JSON::parse (text);
            if (auto* arr = parsed.getArray())
                out = *arr;
            return out;
        }();
        return list;
    }

    float num (const juce::var& v, const char* key, float dflt = 0.0f)
    {
        const auto& x = v[key];
        return x.isVoid() ? dflt : (float) (double) x;
    }

    juce::String str (const juce::var& v, const char* key, const juce::String& dflt = {})
    {
        const auto& x = v[key];
        return x.isVoid() ? dflt : x.toString();
    }

    std::vector<std::complex<float>> decodeSpectrum (const juce::var& list)
    {
        std::vector<std::complex<float>> c (1);
        if (auto* a = list.getArray())
            for (int i = 0; i + 1 < a->size(); i += 2)
            {
                const int db2 = (int) (*a)[i];
                const float amp = db2 <= -999 ? 0.0f : std::pow (10.0f, (float) db2 / 40.0f);
                const float ph = juce::degreesToRadians ((float) (int) (*a)[i + 1]);
                c.push_back (std::polar (amp, ph));
            }
        return c;
    }

    //==========================================================================
    // a mutation is a set of small, musical nudges; seed 0 = none
    //==========================================================================
    struct Nudge
    {
        explicit Nudge (juce::uint32 seed) : on (seed != 0), dice (seed) {}
        bool on;
        Dice dice;

        float mul (float v, float amount) { return on ? dice.jitterMul (v, amount) : v; }
        float add (float v, float amount, float lo, float hi) { return on ? dice.jitterAdd (v, amount, lo, hi) : juce::jlimit (lo, hi, v); }
    };

    // where a filter's cutoff modulation goes in
    juce::String cutoffSocket (const juce::String& type)
    {
        if (type == "filter.svf" || type == "filter.ladder") return "cutoffMod";
        if (type == "filter.combfilter") return "pitchIn";
        if (type == "filter.formant") return "vowelIn";
        return "modIn";
    }

    //==========================================================================
    class Builder
    {
    public:
        Builder (AquanodeModularAudioProcessor& p, juce::var r, juce::uint32 seed)
            : s (p), recipe (std::move (r)), nudge (seed), seedValue (seed) {}

        juce::String build()
        {
            poly = juce::jlimit (1, kMaxVoices, (int) num (recipe, "poly", 8));
            glide = poly == 1 ? num (recipe, "glide", 0.0f) : 0.0f;

            // the amplitude envelope every layer shares
            ampEnv = makeEnv (recipe["amp"], 100.0f, kColAmp);

            buildLayers();
            buildFilters();
            routeLayers();
            buildMods();

            // an Arp patch plays an arpeggio from the start: an Arp on the
            // keys, its Midi Out into every layer
            if (str (recipe, "cat") == "Arp")
                buildArp();

            // sum the voices, then the effect chain, then the level
            const int sum = s.add ("util.polysum", kColOut);
            s.set (sum, "amount", 0.45f);
            for (auto& [id, sock] : finals)
                s.wire (id, sock, sum, "audioIn");

            int prev = sum;
            if (auto* fx = recipe["fx"].getArray())
                for (auto& e : *fx)
                    if (const int id = buildEffect (e); id >= 0)
                    {
                        s.wire (prev, "audioOut", id, "audioIn");
                        prev = id;
                    }

            // the level: the source's own volume times the measured trim
            // (a Volume goes up to 2x, so a quiet sound gets two in a row)
            float gain = juce::jlimit (0.02f, 12.0f, num (recipe, "gain", 1.0f) * num (recipe, "trim", 1.0f));
            do
            {
                const int trim = s.add ("util.volume", kColOut);
                s.set (trim, "level", juce::jmin (2.0f, gain));
                s.wire (prev, "audioOut", trim, "audioIn");
                prev = trim;
                gain /= 2.0f;
            }
            while (gain > 1.0f);
            const int out = s.add ("io.audioOut", kColOut);
            s.wire (prev, "audioOut", out, "audioIn");

            s.layout (1300);
            return str (recipe, "name") + (nudge.on ? " ~" : "");
        }

    private:
        static constexpr int kColMods = 0, kColAmp = 1, kColOsc = 1, kColShape = 2, kColFilter = 3, kColOut = 4;

        //----------------------------------------------------------------------
        void buildArp()
        {
            Dice d (seedValue ^ (juce::uint32) str (recipe, "name").hashCode());
            const int arp = s.add ("util.arp", kColMods);
            s.setChoice (arp, "division", juce::StringArray { "1/16", "1/16", "1/16", "1/8", "1/8T", "1/16T" }[d.index (6)]);
            s.setChoice (arp, "mode", juce::StringArray { "Up", "Up", "Up/Down", "Down", "Random", "As Played" }[d.index (6)]);
            s.set (arp, "octaves", (float) (d.chance (0.55f) ? 2 : (d.chance (0.5f) ? 1 : 3)));
            s.set (arp, "gate", d.range (35.0f, 75.0f));
            s.set (arp, "swing", d.chance (0.3f) ? d.range (8.0f, 25.0f) : 0.0f);

            for (const auto& L : layers)
            {
                auto* inst = s.processor().getInstance (L.gen);
                if (inst == nullptr)
                    continue;
                const auto& desc = inst->descriptor();
                if (desc.inputIndexOf ("addMidiIn") >= 0)
                    s.wire (arp, "midiOut", L.gen, "addMidiIn");
                else if (desc.inputIndexOf ("midiIn") >= 0)
                    s.wire (arp, "midiOut", L.gen, "midiIn");
            }
        }

        int makeEnv (const juce::var& e, float amountPct, int column)
        {
            const int id = s.add ("util.curveenv", column);
            s.set (id, "delay",   num (e, "delay"));
            s.set (id, "attack",  nudge.mul (num (e, "attack", 1.0f), 0.15f));
            s.set (id, "hold",    num (e, "hold"));
            s.set (id, "decay",   nudge.mul (num (e, "decay", 300.0f), 0.2f));
            s.set (id, "sustain", num (e, "sustain", 100.0f));
            s.set (id, "release", nudge.mul (num (e, "release", 100.0f), 0.2f));
            s.set (id, "aCurve",  juce::jlimit (-15.0f, 15.0f, num (e, "ac")));
            s.set (id, "dCurve",  juce::jlimit (-15.0f, 15.0f, num (e, "dc", -2.0f)));
            s.set (id, "rCurve",  juce::jlimit (-15.0f, 15.0f, num (e, "rc", -2.0f)));
            s.set (id, "amount",  juce::jlimit (-100.0f, 100.0f, amountPct));
            return id;
        }

        //----------------------------------------------------------------------
        struct Layer
        {
            int index { 0 };                // the recipe's osc number (1..3, 4 = sub, 0 = noise)
            int gen { -1 };                 // the generator module
            int amp { -1 };                 // a Volume after it (level modulation), or -1
            int pan { -1 };                 // a Panning after it, or -1
            float baseLevel { 1.0f };
            juce::String dest;
            int outId { -1 };
            juce::String outSock;
        };

        void buildLayers()
        {
            if (auto* oscs = recipe["osc"].getArray())
                for (auto& o : *oscs)
                    layers.push_back (makeLayer (o, (int) num (o, "i", 1)));

            const auto& n = recipe["noise"];
            if (n.isObject())
            {
                juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                obj->setProperty ("vol", n["vol"]);
                obj->setProperty ("noise", n["type"]);
                obj->setProperty ("dest", n["dest"]);
                layers.push_back (makeLayer (juce::var (obj.get()), 0));
            }
        }

        Layer makeLayer (const juce::var& o, int index)
        {
            Layer L;
            L.index = index;
            L.dest = str (o, "dest", "f1");
            const float vol = juce::jlimit (0.0f, 1.0f, num (o, "vol", 0.5f));
            L.baseLevel = vol;

            if (o["noise"].isString())
            {
                const int nz = s.add ("osc.noise", kColOsc);
                s.setChoice (nz, "type", str (o, "noise") == "pink" ? "Pink" : "White");
                s.set (nz, "level", juce::jlimit (0.0f, 1.0f, vol * 0.8f));
                s.set (nz, "voices", (float) poly);
                // noise has no envelope input: a Volume carries the amp envelope
                const int amp = s.add ("util.volume", kColShape);
                s.wire (nz, "audioOut", amp, "audioIn");
                s.wire (ampEnv, "envOut", amp, "volMod");
                L.gen = nz;
                L.outId = amp;
                L.outSock = "audioOut";
                return L;
            }

            const int w = s.add ("osc.warp", kColOsc);
            L.gen = w;
            s.set (w, "volume", juce::jlimit (0.0f, 1.0f, vol));

            float tr = num (o, "tr"), fine = num (o, "fine");
            while (fine > 100.0f) { fine -= 100.0f; tr += 1.0f; }
            while (fine < -100.0f) { fine += 100.0f; tr -= 1.0f; }
            s.set (w, "transpose", juce::jlimit (-48.0f, 48.0f, tr));
            s.set (w, "fine", fine);

            const int uni = juce::jlimit (1, WarpOscModule::kMaxUnison, (int) num (o, "uni", 1));
            s.set (w, "unison", (float) uni);
            s.set (w, "detune", juce::jlimit (0.0f, 200.0f, nudge.mul (num (o, "det", 20.0f), 0.25f)));
            s.set (w, "spread", num (o, "spread", 80.0f));
            s.set (w, "blend", num (o, "blend", 80.0f));
            s.set (w, "voices", (float) poly);
            if (glide > 0.0f)
                s.set (w, "glide", juce::jlimit (0.0f, 1000.0f, glide));

            if (o["A"].isArray())
            {
                s.setChoice (w, "wave", "Custom");
                s.set (w, "shape", 0.0f);
                if (auto* osc = dynamic_cast<WarpOscModule*> (s.dsp (w)))
                    osc->setCustomSpectra (decodeSpectrum (o["A"]), o["B"].isArray() ? decodeSpectrum (o["B"]) : decodeSpectrum (o["A"]));
            }
            else
                s.set (w, "shape", nudge.add (num (o, "shape", 50.0f), 6.0f, 0.0f, 100.0f));

            if (o["wm"].isString())
            {
                s.setChoice (w, "warpMode", str (o, "wm"));
                s.set (w, "warp", nudge.add (num (o, "wa"), 8.0f, 0.0f, 100.0f));
            }

            s.wire (ampEnv, "envOut", w, "envIn");
            L.outId = w;
            L.outSock = "audioOut";

            const float pan = num (o, "pan");
            if (std::abs (pan) > 5.0f)
                addPan (L, pan);
            return L;
        }

        void addPan (Layer& L, float panPct)
        {
            if (L.pan >= 0)
                return;
            L.pan = s.add ("util.panning", kColShape);
            s.set (L.pan, "pan", juce::jlimit (-100.0f, 100.0f, panPct));
            s.wire (L.outId, L.outSock, L.pan, "audioIn");
            L.outId = L.pan;
            L.outSock = "audioOut";
        }

        void addAmp (Layer& L)
        {
            if (L.amp >= 0)
                return;
            L.amp = s.add ("util.volume", kColShape);
            s.wire (L.outId, L.outSock, L.amp, "audioIn");
            L.outId = L.amp;
            L.outSock = "audioOut";
        }

        Layer* layerFor (int oscIndex)
        {
            for (auto& L : layers)
                if (L.index == oscIndex)
                    return &L;
            return nullptr;
        }

        //----------------------------------------------------------------------
        struct Filter { int id { -1 }; juce::String type; bool hasMod { false }; };
        Filter filters[2];

        void buildFilters()
        {
            auto* arr = recipe["flt"].getArray();
            for (int f = 0; f < 2 && arr != nullptr && f < arr->size(); ++f)
            {
                const auto& F = (*arr)[f];
                if (! F.isObject())
                    continue;
                const auto kind = str (F, "kind", "lp12");
                const float cut = juce::jlimit (20.0f, 20000.0f, nudge.mul (num (F, "cut", 1000.0f), 0.3f));
                const float res = nudge.add (num (F, "res", 0.3f), 0.06f, 0.0f, 1.0f);
                juce::String type;
                int id = -1;
                if (kind.startsWith ("lp") || kind.startsWith ("hp"))
                {
                    type = kind.startsWith ("lp") ? "filter.lowpass" : "filter.highpass";
                    id = s.add (type, kColFilter);
                    s.set (id, "cutoff", cut);
                    s.set (id, "resonance", res * 0.92f);
                    s.setChoice (id, "slope", kind.endsWith ("24") ? "24 dB/oct" : "12 dB/oct");
                }
                else if (kind == "bp")
                {
                    type = "filter.svf";
                    id = s.add (type, kColFilter);
                    s.set (id, "cutoff", cut);
                    s.set (id, "resonance", res * 0.9f);
                    s.setChoice (id, "mode", "Bandpass");
                }
                else if (kind == "notch")
                {
                    type = "filter.notch";
                    id = s.add (type, kColFilter);
                    s.set (id, "freq", cut);
                }
                else if (kind.startsWith ("ladder"))
                {
                    type = "filter.ladder";
                    id = s.add (type, kColFilter);
                    s.set (id, "cutoff", cut);
                    s.set (id, "resonance", res * 0.9f);
                    s.set (id, "drive", juce::jlimit (1.0f, 5.0f, 1.0f + num (F, "drive") / 20.0f * 3.0f));
                    const bool hp = kind.startsWith ("ladderhp");
                    s.setChoice (id, "mode", juce::String (kind.endsWith ("24") ? "24dB " : "12dB ") + (hp ? "HP" : "LP"));
                }
                else if (kind == "comb")
                {
                    type = "filter.combfilter";
                    id = s.add (type, kColFilter);
                    s.set (id, "freq", juce::jlimit (20.0f, 8000.0f, cut));
                    s.set (id, "feedback", juce::jlimit (-99.0f, 99.0f, res * 95.0f));
                    s.set (id, "pitchDepth", 0.0f);
                }
                else if (kind == "formant")
                {
                    type = "filter.formant";
                    id = s.add (type, kColFilter);
                    s.set (id, "vowel", num (F, "vowel", 50.0f));
                    s.set (id, "resonance", juce::jlimit (0.0f, 100.0f, 60.0f + res * 40.0f));
                }
                filters[f] = { id, type, false };

                // key tracking: KeyTrack is (note - 60) / 60, which the filters'
                // +-5 octave mod input turns into exactly 1:1 at 100%
                const float kt = num (F, "kt");
                if (id >= 0 && std::abs (kt) > 0.02f)
                {
                    const int key = s.add ("util.keytrack", kColMods);
                    if (type == "filter.combfilter")
                    {
                        s.set (id, "pitchDepth", juce::jlimit (0.0f, 100.0f, kt * 100.0f));
                        s.wire (key, "modOut", id, "pitchIn");
                    }
                    else if (type != "filter.formant")
                        connectScaled (key, "modOut", kt, id, cutoffSocket (type), true);
                }
            }
        }

        // a source scaled by `gain` into a filter's cutoff input (or anything),
        // through a DC Offset when the source itself cannot be scaled
        void connectScaled (int src, const juce::String& srcSock, float gain, int dst, const juce::String& dstSock, bool cutoff)
        {
            if (cutoff)
                markFilterMod (dst);
            if (std::abs (gain - 1.0f) < 1.0e-3f)
            {
                s.wire (src, srcSock, dst, dstSock);
                return;
            }
            const int dc = s.add ("util.dcoffset", kColMods);
            s.set (dc, "gain", juce::jlimit (-2.0f, 2.0f, gain));
            s.set (dc, "offset", 0.0f);
            s.wire (src, srcSock, dc, "modIn");
            s.wire (dc, "modOut", dst, dstSock);
        }

        void markFilterMod (int filterId)
        {
            for (auto& f : filters)
                if (f.id == filterId && ! f.hasMod)
                {
                    f.hasMod = true;
                    if (f.type != "filter.combfilter" && f.type != "filter.formant")
                        s.set (f.id, "modDepth", 100.0f);
                    if (f.type == "filter.formant")
                        s.set (f.id, "vowelMod", 100.0f);
                }
        }

        //----------------------------------------------------------------------
        void routeLayers()
        {
            const int serial = (int) num (recipe, "serial");
            const int f1 = filters[0].id, f2 = filters[1].id;

            for (auto& L : layers)
            {
                const bool to1 = (L.dest == "f1" || L.dest == "both") && f1 >= 0;
                const bool to2 = (L.dest == "f2" || L.dest == "both") && f2 >= 0;
                if (to1) s.wire (L.outId, L.outSock, f1, "audioIn");
                if (to2) s.wire (L.outId, L.outSock, f2, "audioIn");
                if (! to1 && ! to2)
                    finals.push_back ({ L.outId, L.outSock });
            }

            if (serial == 1 && f1 >= 0 && f2 >= 0)
            {
                s.wire (f1, "audioOut", f2, "audioIn");
                finals.push_back ({ f2, "audioOut" });
            }
            else if (serial == 2 && f1 >= 0 && f2 >= 0)
            {
                s.wire (f2, "audioOut", f1, "audioIn");
                finals.push_back ({ f1, "audioOut" });
            }
            else
            {
                if (f1 >= 0) finals.push_back ({ f1, "audioOut" });
                if (f2 >= 0) finals.push_back ({ f2, "audioOut" });
            }
        }

        //----------------------------------------------------------------------
        // one modulation: its own source module, scaled at the source, wired
        // into the destination's per-voice input
        void buildMods()
        {
            auto* mods = recipe["mods"].getArray();
            if (mods == nullptr)
                return;

            for (auto& m : *mods)
            {
                const auto dst = str (m, "dst");
                const bool bip = (bool) m["bip"];
                float amt = num (m, "amt");

                // the destination in "source units" (1.0 = what a full-scale
                // source should move it by)
                int target = -1;
                juce::String sock;
                bool cutoff = false;
                if (dst == "cut")
                {
                    const int f = (int) num (m, "f", 1) - 1;
                    if (f < 0 || f > 1 || filters[f].id < 0)
                        continue;
                    target = filters[f].id;
                    sock = cutoffSocket (filters[f].type);
                    if (filters[f].type == "filter.combfilter")
                        continue;
                    amt = amt / 5.0f;                       // octaves -> +-5 octave input
                    if (filters[f].type == "filter.formant")
                        amt *= 1.0f;
                    cutoff = true;
                }
                else if (dst == "pitch" || dst == "shape" || dst == "level" || dst == "pan")
                {
                    auto* L = layerFor ((int) num (m, "o", 1));
                    if (L == nullptr)
                        continue;
                    if (dst == "pitch")
                    {
                        if (L->index == 0) continue;
                        target = L->gen; sock = "pitchIn"; amt = amt / 60.0f;   // semitones -> semis/60
                    }
                    else if (dst == "shape")
                    {
                        if (L->index == 0) continue;
                        target = L->gen; sock = "shapeIn";
                        s.set (L->gen, "shapeMod", 100.0f);
                        amt = 1.0f;
                    }
                    else if (dst == "level")
                    {
                        // the layer's level moves by amt around its own setting:
                        // gain = 1 + (amt / level) * source - or, for a layer that
                        // sits at zero and is only brought in by the modulation,
                        // the layer is set to the top and the source is the gain
                        addAmp (*L);
                        const int dc = s.add ("util.dcoffset", kColMods);
                        const float base = std::sqrt (juce::jmax (0.0f, L->baseLevel));
                        float scaleIt = 1.0f;
                        if (base < 0.1f && amt > 0.0f)
                        {
                            s.set (L->gen, L->index != 0 ? "volume" : "level", juce::jlimit (0.0f, 1.0f, amt * amt));
                            s.set (dc, "offset", 0.0f);
                        }
                        else
                        {
                            scaleIt = juce::jlimit (-1.0f, 1.0f, amt / juce::jmax (0.1f, base));
                            s.set (dc, "offset", 1.0f);
                        }
                        s.set (dc, "gain", 1.0f);
                        const int src = makeSource (m["src"], scaleIt, bip);
                        if (src < 0) continue;
                        s.wire (src, sourceSocket (m["src"]), dc, "modIn");
                        s.wire (dc, "modOut", L->amp, "volMod");
                        continue;
                    }
                    else
                    {
                        addPan (*L, 0.0f);
                        target = L->pan; sock = "panMod";
                    }
                }
                if (target < 0)
                    continue;

                if (cutoff)
                    markFilterMod (target);
                const int src = makeSource (m["src"], amt, bip);
                if (src >= 0)
                    s.wire (src, sourceSocket (m["src"]), target, sock);
            }
        }

        static juce::String sourceSocket (const juce::var& src)
        {
            const auto kind = src["kind"].toString();
            if (kind == "env") return "envOut";
            if (kind == "lfo" && src["div"].isString()) return "modOut";   // via its DC Offset
            return "modOut";
        }

        // builds the source with its output already scaled to `scale` (bipolar
        // sources swing +-scale/2, unipolar ones 0..scale); returns the module
        // whose Mod Out / Env Out to wire
        int makeSource (const juce::var& src, float scale, bool bipolar)
        {
            const auto kind = src["kind"].toString();
            scale = juce::jlimit (-1.0f, 1.0f, scale);
            if (kind == "env")
                return makeEnv (src["env"], scale * 100.0f, kColMods);

            if (kind == "rnd")
            {
                const int r = s.add ("util.randomlfo", kColMods);
                s.setChoice (r, "mode", src["mode"].toString());
                if (src["div"].isString())
                {
                    s.setChoice (r, "sync", "Tempo");
                    s.setChoice (r, "division", src["div"].toString());
                }
                else
                    s.set (r, "rate", juce::jlimit (0.1f, 50.0f, nudge.mul (num (src, "hz", 1.0f), 0.2f)));
                s.setChoice (r, "polarity", bipolar ? "Bipolar" : "Unipolar");
                s.set (r, "amount", (bipolar ? 50.0f : 100.0f) * scale);
                return r;
            }

            // LFOs: free ones are the per-voice LFO, tempo ones the Multi LFO
            if (src["div"].isString())
            {
                const int l = s.add ("util.multilfo", kColMods);
                const auto shape = src["shape"].toString();
                s.setChoice (l, "shape", shape == "Sine" ? "Triangle" : shape);
                s.setChoice (l, "sync", "Tempo");
                s.setChoice (l, "division", src["div"].toString());
                s.set (l, "min", 0.0f);
                s.set (l, "max", 1.0f);
                const int dc = s.add ("util.dcoffset", kColMods);
                s.set (dc, "gain", scale);
                s.set (dc, "offset", bipolar ? -0.5f * scale : 0.0f);
                s.wire (l, "modOut", dc, "modIn");
                return dc;
            }

            const int l = s.add ("util.lfo", kColMods);
            s.setChoice (l, "waveform", src["shape"].toString());
            s.set (l, "rate", juce::jlimit (0.1f, 100.0f, nudge.mul (num (src, "hz", 1.0f), 0.15f)));
            s.set (l, "level", juce::jlimit (0.0f, 1.0f, std::abs (scale) * 0.5f));
            s.set (l, "offset", bipolar ? 0.0f : 0.5f * scale);
            if (scale < 0.0f)
            {
                // a negative depth: flip it through a DC Offset
                const int dc = s.add ("util.dcoffset", kColMods);
                s.set (dc, "gain", -1.0f);
                s.set (l, "offset", bipolar ? 0.0f : -0.5f * scale);
                s.wire (l, "modOut", dc, "modIn");
                return dc;
            }
            return l;
        }

        //----------------------------------------------------------------------
        int buildEffect (const juce::var& e)
        {
            const auto t = str (e, "t");
            const float mix = num (e, "mix", 50.0f);
            if (t == "dist")
            {
                const auto kind = str (e, "kind");
                const float drive = num (e, "drive");
                if (kind == "BitCrush")
                {
                    const int id = s.add ("fx.bitcrush", kColOut);
                    s.set (id, "bits", juce::jlimit (2.0f, 16.0f, 12.0f - drive / 3.0f));
                    s.set (id, "dryWet", mix);
                    return id;
                }
                if (kind == "DownSample")
                {
                    const int id = s.add ("fx.srreduce", kColOut);
                    s.set (id, "rate", juce::jlimit (200.0f, 44100.0f, 44100.0f / std::exp2 (juce::jmax (0.0f, drive) / 5.0f)));
                    s.set (id, "dryWet", mix);
                    return id;
                }
                const int id = s.add ("fx.waveshaper", kColOut);
                const float gain = juce::jlimit (1.0f, 50.0f, juce::Decibels::decibelsToGain (drive));
                s.set (id, "drive", gain);
                s.setChoice (id, "shape", kind == "HardClip" ? "Hard Clip" : (kind.contains ("Fold") ? "Sine Fold" : "Tanh"));
                s.set (id, "out", juce::jlimit (0.3f, 1.0f, 1.0f / std::pow (gain, 0.25f)));
                s.set (id, "dryWet", mix);
                return id;
            }
            if (t == "comp")
            {
                const int id = s.add ("fx.threebandcomp", kColOut);
                s.set (id, "depth", juce::jlimit (0.0f, 100.0f, num (e, "depth", 60.0f) * 0.7f));
                s.set (id, "time", num (e, "time", 50.0f));
                return id;
            }
            if (t == "eq")
            {
                const int id = s.add ("filter.shelfeq", kColOut);
                s.set (id, "lowMode", num (e, "lowMode"));
                s.set (id, "lowFreq", juce::jlimit (20.0f, 2000.0f, num (e, "lowF", 100.0f)));
                s.set (id, "lowGain", juce::jlimit (-24.0f, 24.0f, num (e, "lowG")));
                s.set (id, "lowQ", juce::jlimit (0.3f, 4.0f, num (e, "lowQ", 0.71f)));
                s.set (id, "midMode", num (e, "midMode"));
                s.set (id, "midFreq", juce::jlimit (40.0f, 16000.0f, num (e, "midF", 1000.0f)));
                s.set (id, "midGain", juce::jlimit (-24.0f, 24.0f, num (e, "midG")));
                s.set (id, "midQ", juce::jlimit (0.2f, 16.0f, num (e, "midQ", 0.8f)));
                s.set (id, "highMode", num (e, "highMode"));
                s.set (id, "highFreq", juce::jlimit (500.0f, 20000.0f, num (e, "highF", 6000.0f)));
                s.set (id, "highGain", juce::jlimit (-24.0f, 24.0f, num (e, "highG")));
                s.set (id, "highQ", juce::jlimit (0.3f, 4.0f, num (e, "highQ", 0.71f)));
                return id;
            }
            if (t == "chorus")
            {
                const int id = s.add ("fx.liquidchor", kColOut);
                s.set (id, "time1", juce::jlimit (0.5f, 25.0f, num (e, "t1", 2.0f) + 1.5f));
                s.set (id, "time2", juce::jlimit (0.5f, 25.0f, num (e, "t2", 8.0f) + 1.5f));
                s.set (id, "speed", juce::jlimit (0.01f, 10.0f, num (e, "rate", 0.5f)));
                s.set (id, "feedback", juce::jlimit (0.0f, 95.0f, num (e, "fb", 40.0f)));
                s.set (id, "dryWet", nudge.add (mix, 6.0f, 0.0f, 100.0f));
                return id;
            }
            if (t == "flanger")
            {
                const int id = s.add ("fx.flanger", kColOut);
                s.set (id, "rate", num (e, "rate", 0.25f));
                s.set (id, "depth", num (e, "depth", 50.0f));
                s.set (id, "manual", num (e, "manual", 2.0f));
                s.set (id, "feedback", juce::jlimit (-100.0f, 100.0f, num (e, "fb", 50.0f)));
                s.set (id, "dryWet", juce::jlimit (0.0f, 100.0f, mix));
                return id;
            }
            if (t == "phaser")
            {
                const int id = s.add ("fx.phaser", kColOut);
                s.set (id, "rate", num (e, "rate", 0.5f));
                s.set (id, "depth", num (e, "depth", 50.0f));
                s.set (id, "centre", num (e, "centre", 800.0f));
                s.set (id, "feedback", juce::jlimit (0.0f, 100.0f, num (e, "fb", 50.0f)));
                s.set (id, "dryWet", mix);
                s.setChoice (id, "stages", "6");
                return id;
            }
            if (t == "delay")
            {
                const int id = s.add ("fx.echo", kColOut);
                if (e["div"].isString())
                {
                    s.setChoice (id, "sync", "Tempo");
                    s.setChoice (id, "division", e["div"].toString());
                }
                else
                {
                    s.setChoice (id, "sync", "Free (ms)");
                    s.set (id, "time", juce::jlimit (1.0f, 3900.0f, num (e, "ms", 375.0f)));
                }
                s.set (id, "feedback", juce::jlimit (0.0f, 105.0f, nudge.add (num (e, "fb", 40.0f), 5.0f, 0.0f, 100.0f)));
                s.set (id, "tone", juce::jlimit (40.0f, 16000.0f, num (e, "tone", 1800.0f)));
                s.set (id, "width", juce::jlimit (0.5f, 10.0f, num (e, "width", 4.0f)));
                s.setChoice (id, "style", str (e, "style", "Stereo"));
                s.set (id, "mix", nudge.add (mix, 6.0f, 0.0f, 100.0f));
                return id;
            }
            if (t == "reverb")
            {
                const int id = s.add ("fx.reverb", kColOut);
                s.set (id, "roomSize", num (e, "size", 50.0f));
                s.set (id, "decay", juce::jlimit (0.1f, 20.0f, nudge.mul (num (e, "decay", 2.0f), 0.2f)));
                s.set (id, "damping", num (e, "damp", 50.0f));
                s.set (id, "preDelay", num (e, "pre", 0.0f));
                s.set (id, "dryWet", nudge.add (mix, 6.0f, 0.0f, 100.0f));
                return id;
            }
            return -1;
        }

        PatchSketch s;
        const juce::var recipe;     // a copy: juce::Array hands its elements out by value
        Nudge nudge;
        juce::uint32 seedValue { 0 };
        int poly { 8 };
        float glide { 0.0f };
        int ampEnv { -1 };
        std::vector<Layer> layers;
        std::vector<std::pair<int, juce::String>> finals;
    };
}

//==============================================================================
juce::StringArray genreNames() { return { "Ambient", "DnB", "Dub Techno", "Psychill" }; }

int numRecipes() { return recipes().size(); }

std::vector<Entry> recipesOf (int genre)
{
    static const juce::StringArray order { "Bass", "Pad", "Lead", "Pluck", "Chord", "Keys", "Bell", "Arp", "Synth", "Kick", "Perc", "FX" };
    std::vector<Entry> out;
    const auto& list = recipes();
    for (int i = 0; i < list.size(); ++i)
        if ((int) list[i]["genre"] == genre)
            out.push_back ({ i, list[i]["name"].toString(), list[i]["cat"].toString() });
    std::stable_sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b)
    {
        const int ca = order.indexOf (a.category), cb = order.indexOf (b.category);
        if (ca != cb) return ca < cb;
        return a.name.compareNatural (b.name) < 0;
    });
    return out;
}

juce::String buildRecipePatch (AquanodeModularAudioProcessor& p, int index, juce::uint32 seed)
{
    const auto& list = recipes();
    if (index < 0 || index >= list.size())
        return "Init";
    Builder b (p, list.getReference (index), seed);
    return b.build();
}

BuildFn randomBuilder (int genre, juce::uint32 seed, const juce::String& category)
{
    return [genre, seed, category] (AquanodeModularAudioProcessor& p)
    {
        auto entries = recipesOf (genre);
        if (category.isNotEmpty())
        {
            std::vector<Entry> only;
            for (auto& e : entries)
                if (e.category == category)
                    only.push_back (e);
            if (! only.empty())
                entries = only;
        }
        if (entries.empty())
            return juce::String ("Init");
        Dice d (seed);
        const auto& pick = entries[(size_t) d.index ((int) entries.size())];
        return buildRecipePatch (p, pick.index, seed * 2654435761u + 1u);
    };
}

} // namespace aquanode::presets::genre
