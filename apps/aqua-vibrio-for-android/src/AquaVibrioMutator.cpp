#include "AquaVibrioMutator.h"
#include "AquaVibrioParameters.h"
#include "AquaVibrioNoteLayers.h"
#include "AquaVibrioRelevance.h"

namespace aquavibrio
{

namespace
{
    // The six axes a mutation moves along, in this order.
    enum Axis { Brightness, Snap, Movement, Width, Grit, Space, NumAxes };

    struct Gene
    {
        const char* id;
        float weight;      // how far this knob moves relative to the others
        const char* axes;  // e.g. "B+S-": brighter raises it, snappier lowers it
        bool gated;        // switched off at its rest value: leave it there
    };

    // The whole personality of the mutator lives in this table.
    const Gene kGenes[] =
    {
        //== oscillators ======================================================
        { "Osc1Shape", 0.8f, "B+", false },               { "Osc2Shape", 0.8f, "B+", false },
        { "Osc1Pulsewidth", 0.6f, "", false },            { "Osc2Pulsewidth", 0.6f, "", false },
        { "Osc1WavetableWavetableindex", 0.7f, "B+", false }, { "Osc2WavetableWavetableindex", 0.7f, "B+", false },
        { "Osc1WavetableInterpolation", 0.5f, "", false },    { "Osc2WavetableInterpolation", 0.5f, "", false },
        { "Osc1WavetableSync", 0.5f, "B+", true },        { "Osc2WavetableSync", 0.5f, "B+", true },
        { "Osc1HypersawDetunespread", 0.7f, "W+", false },{ "Osc2HypersawDetunespread", 0.7f, "W+", false },
        { "Osc1HypersawDensity", 0.5f, "W+", false },     { "Osc2HypersawDensity", 0.5f, "W+", false },
        { "Osc2Detune", 0.35f, "W+", false },
        { "Osc2FmAmount", 0.6f, "G+B+", true },
        { "Osc2HypersawCrossoscsyncfreq", 0.5f, "B+G+", true },
        { "OscBalance", 0.4f, "", false },
        { "SuboscillatorVolume", 0.5f, "", true },
        { "NoiseVolume", 0.4f, "G+", true },
        { "NoiseColor", 0.5f, "B+", false },
        { "RingmodulatorVolume", 0.5f, "G+", true },
        { "Osc3Volume", 0.4f, "", true },
        { "OscMainvolume", 0.3f, "G+", false },
        { "Osc1ShapeVelocity", 0.4f, "", true },          { "Osc2ShapeVelocity", 0.4f, "", true },
        { "PulsewidthVelocity", 0.4f, "", true },
        { "FmFiltEnvAmt", 0.5f, "S+", true },
        { "FmAmountVelocity", 0.4f, "", true },
        { "Osc2HsawFiltEnvSyncFreq", 0.5f, "S+", true },

        //== unison ===========================================================
        { "UnisonDetune", 0.6f, "W+", false },
        { "UnisonPanSpread", 0.6f, "W+", false },
        { "UnisonLfoPhase", 0.4f, "M+", false },

        //== filters ==========================================================
        { "Cutoff", 1.0f, "B+", false },                  { "Cutoff2", 0.8f, "B+", false },
        { "Filter1Resonance", 0.7f, "G+", false },        { "Filter2Resonance", 0.6f, "G+", false },
        { "Filter1EnvAmt", 0.8f, "B+S+", false },         { "Filter2EnvAmt", 0.6f, "B+S+", false },
        { "Filter1Keyfollow", 0.4f, "", false },          { "Filter2Keyfollow", 0.4f, "", false },
        { "OffsetForFilterlink", 0.4f, "B+", false },
        { "FilterBalance", 0.4f, "", false },
        { "Flt1EnvamtVelocity", 0.4f, "", true },         { "Flt2EnvamtVelocity", 0.4f, "", true },
        { "Resonance1Velocity", 0.3f, "", true },         { "Resonance2Velocity", 0.3f, "", true },

        //== envelopes ========================================================
        { "AmpEnvAttack", 0.6f, "S-", false },            { "AmpEnvDecay", 0.6f, "S-", false },
        { "AmpEnvSustain", 0.5f, "", false },             { "AmpEnvSustainTime", 0.4f, "", false },
        { "AmpEnvRelease", 0.6f, "S-P+", false },
        { "FilterEnvAttack", 0.7f, "S-", false },         { "FilterEnvDecay", 0.8f, "S-", false },
        { "FilterEnvSustain", 0.6f, "", false },          { "FilterEnvSustainTime", 0.4f, "", false },
        { "FilterEnvRelease", 0.6f, "S-", false },
        { "Envelope3Attack", 0.5f, "S-", false },         { "Envelope3Decay", 0.5f, "S-", false },
        { "Envelope3Sustain", 0.4f, "", false },          { "Envelope3Release", 0.4f, "S-", false },
        { "Envelope4Attack", 0.5f, "S-", false },         { "Envelope4Decay", 0.5f, "S-", false },
        { "Envelope4Sustain", 0.4f, "", false },          { "Envelope4Release", 0.4f, "S-", false },

        //== LFOs =============================================================
        { "Lfo1Rate", 0.7f, "M+", false },                { "Lfo2Rate", 0.7f, "M+", false },
        { "Lfo3Rate", 0.5f, "M+", false },
        { "Lfo1Symmetry", 0.4f, "", false },              { "Lfo2Symmetry", 0.4f, "", false },
        { "Osc1Lfo1Amount", 0.25f, "M+", true },          { "Osc2Lfo1Amount", 0.25f, "M+", true },
        { "PwLfo1Amount", 0.6f, "M+", true },             { "ResoLfo1Amount", 0.5f, "M+", true },
        { "FiltgainLfo1Amount", 0.5f, "M+", true },       { "Lfo1AssignAmount", 0.5f, "M+", true },
        { "ShapeLfo2Amount", 0.6f, "M+", true },          { "FmLfo2Amount", 0.5f, "M+", true },
        { "Cutoff1Lfo2Amount", 0.6f, "M+", true },        { "Cutoff2Lfo2Amount", 0.5f, "M+", true },
        { "PanLfo2Amount", 0.6f, "M+W+", true },          { "Lfo2AssignAmount", 0.5f, "M+", true },
        { "OscLfo3Amount", 0.3f, "M+", true },
        { "Lfo3FadeInTime", 0.4f, "", true },

        //== modulation matrix amounts (never the routings) ===================
        { "Assign1Amount", 0.4f, "M+", true },  { "Assign1Amount2", 0.4f, "M+", true },  { "Assign1Amount3", 0.4f, "M+", true },
        { "Assign2Amount1", 0.4f, "M+", true }, { "Assign2Amount2", 0.4f, "M+", true },  { "Assign2Amount3", 0.4f, "M+", true },
        { "Assign3Amount1", 0.4f, "M+", true }, { "Assign3Amount2", 0.4f, "M+", true },  { "Assign3Amount3", 0.4f, "M+", true },
        { "Assign4Amount", 0.4f, "M+", true },  { "Assign4Amount2", 0.4f, "M+", true },  { "Assign4Amount3", 0.4f, "M+", true },
        { "Assign5Amount", 0.4f, "M+", true },  { "Assign5Amount2", 0.4f, "M+", true },  { "Assign5Amount3", 0.4f, "M+", true },
        { "Assign6Amount", 0.4f, "M+", true },  { "Assign6Amount2", 0.4f, "M+", true },  { "Assign6Amount3", 0.4f, "M+", true },

        //== amp ==============================================================
        { "PunchIntensity", 0.6f, "S+", true },
        { "AmpVelocity", 0.4f, "", true },
        { "PanoramaVelocity", 0.3f, "W+", true },

        //== effects (amounts, tones and times - never the types) =============
        { "DistortionIntensity", 0.6f, "G+", false },     { "PatchDistortionTone127", 0.5f, "B+", false },
        { "PatchDistortionMix", 0.4f, "G+", false },
        { "FilterBankFrequency", 0.7f, "B+", false },     { "FilterBankResonance", 0.5f, "G+", false },
        { "FilterBankMix", 0.4f, "", false },
        { "VocoderBalance", 0.3f, "", false },
        { "BassIntensity", 0.5f, "G+", true },
        { "LoweqGain", 0.4f, "B-", true },                { "MideqGain", 0.4f, "", true },
        { "HigheqGain", 0.4f, "B+", true },
        { "PhaserMix", 0.5f, "M+", true },                { "PhaserRate", 0.5f, "M+", false },
        { "PhaserFeedback", 0.4f, "G+", false },          { "PhaserDepth", 0.4f, "M+", false },
        { "ChorusMix", 0.5f, "W+", true },                { "ChorusRate", 0.5f, "M+", false },
        { "ChorusDepth", 0.5f, "W+", false },
        { "DelaySend", 0.5f, "P+", true },                { "DelayFeedback", 0.5f, "P+", false },
        { "DelayTime", 0.25f, "", false },                { "DelayColor", 0.4f, "B+", false },
        { "ReverbSend", 0.5f, "P+", true },               { "ReverbTime", 0.5f, "P+", false },
        { "ReverbDamping", 0.4f, "B-", false },           { "ReverbColor", 0.3f, "B+", false },
        { "ReverbPredelay", 0.3f, "", false },
        { "Atomizer", 0.4f, "P+", true },                 { "GranulatorSpray", 0.5f, "M+", false },
        { "GranulatorSize", 0.4f, "", false },            { "GranulatorGrains", 0.4f, "", false },
        { "GranulatorPosition", 0.3f, "", false },        { "GranulatorStereo", 0.5f, "W+", false },
    };

    struct Resolved
    {
        int index;
        const ParamInfo* info;
        float weight;
        std::array<float, NumAxes> axis {};
        bool gated;
        float restValue;
    };

    const std::vector<Resolved>& resolved()
    {
        static const std::vector<Resolved> list = []
        {
            std::vector<Resolved> v;
            for (const auto& g : kGenes)
            {
                const int idx = layers::indexOf (g.id);
                jassert (idx >= 0);   // a typo in the table above
                if (idx < 0)
                    continue;

                Resolved r;
                r.index = idx;
                r.info = &allParameters()[(size_t) idx];
                r.weight = g.weight;
                r.gated = g.gated;
                r.restValue = (r.info->scale == ValueScale::Bipolar || r.info->scale == ValueScale::BipolarPercent)
                                ? 64.0f : (float) r.info->minValue;

                for (const char* a = g.axes; a[0] != 0 && a[1] != 0; a += 2)
                {
                    const float sign = a[1] == '-' ? -1.0f : 1.0f;
                    switch (a[0])
                    {
                        case 'B': r.axis[Brightness] = sign; break;
                        case 'S': r.axis[Snap] = sign; break;
                        case 'M': r.axis[Movement] = sign; break;
                        case 'W': r.axis[Width] = sign; break;
                        case 'G': r.axis[Grit] = sign; break;
                        case 'P': r.axis[Space] = sign; break;
                        default: jassertfalse; break;
                    }
                }
                v.push_back (r);
            }
            return v;
        }();
        return list;
    }

    float gauss (juce::Random& rng)
    {
        const double u1 = juce::jmax (1.0e-12, rng.nextDouble());
        const double u2 = rng.nextDouble();
        return (float) (std::sqrt (-2.0 * std::log (u1)) * std::cos (juce::MathConstants<double>::twoPi * u2));
    }

    // Beyond the panel's own relevance rules: effects whose level is at zero
    // are inaudible, and changing them would only give a layer an effects
    // rack of its own for nothing.
    juce::String silencedBy (const juce::String& id, const std::function<int (const char*)>& get)
    {
        if (id.startsWith ("Delay") && id != "DelaySend" && get ("DelaySend") == 0)        return "delay send is closed";
        if (id.startsWith ("Reverb") && id != "ReverbSend" && get ("ReverbSend") == 0)     return "reverb send is closed";
        if (id.startsWith ("Phaser") && id != "PhaserMix" && get ("PhaserMix") == 0)       return "phaser is off";
        if (id.startsWith ("Chorus") && id != "ChorusMix" && get ("ChorusMix") == 0)       return "chorus is off";
        if (id.startsWith ("Granulator") && get ("Atomizer") == 0)                         return "granulator is off";
        if (id.startsWith ("FilterBank") && id != "FilterBankMix" && get ("FilterBankMix") == 0) return "filter bank is off";
        if (id == "NoiseColor" && get ("NoiseVolume") == 0)                                return "noise is off";

        if (id.startsWith ("Assign"))
        {
            // the slot's source, and the destination this amount belongs to
            const juce::String slot = id.substring (0, 7);       // "Assign3"
            if (get ((slot + "Source").toRawUTF8()) == 0)
                return "matrix slot has no source";
        }
        return {};
    }
}

bool Mutator::isMutable (int parameterIndex)
{
    for (const auto& r : resolved())
        if (r.index == parameterIndex)
            return true;
    return false;
}

int Mutator::mutate (std::vector<float>& values, Strength strength, juce::Random& rng)
{
    if ((int) values.size() < layers::numParams())
        return 0;

    static const float sigmas[] = { 0.035f, 0.08f, 0.18f };
    const float sigma = sigmas[juce::jlimit (0, 2, (int) strength)];

    // One direction in feel-space for the whole patch ...
    std::array<float, NumAxes> direction {};
    for (auto& d : direction)
        d = gauss (rng);

    // ... read against the patch as it was, so the order of the table does
    // not change which knobs count as active.
    const std::vector<float> before = values;
    const auto get = [&before] (const char* id) -> int
    {
        const int idx = layers::indexOf (id);
        return idx >= 0 ? juce::roundToInt (before[(size_t) idx]) : 0;
    };

    int moved = 0;

    for (const auto& r : resolved())
    {
        const juce::String id (r.info->id);
        const float v = before[(size_t) r.index];

        if (r.gated && v == r.restValue)
            continue;
        if (inactiveReason (id, get).isNotEmpty() || silencedBy (id, get).isNotEmpty())
            continue;

        float along = 0.0f;
        for (int a = 0; a < NumAxes; ++a)
            along += r.axis[(size_t) a] * direction[(size_t) a];

        const float span = (float) (r.info->maxValue - r.info->minValue);
        const float delta = (along * 0.8f + gauss (rng) * 0.6f) * sigma * r.weight * span;
        float nv = (float) juce::roundToInt (juce::jlimit ((float) r.info->minValue, (float) r.info->maxValue, v + delta));

        // Guard rails: a mutation must not silence or scream.
        if (id == "Cutoff" || id == "Cutoff2")                nv = juce::jmax (nv, juce::jmin (v, 24.0f));
        if (id == "AmpEnvAttack")                             nv = juce::jmin (nv, juce::jmax (v, v + 18.0f));
        if (id == "AmpEnvSustain" && v >= 40.0f)              nv = juce::jmax (nv, 30.0f);
        if (id.endsWith ("Resonance") && id.startsWith ("Filter") && id != "FilterBankResonance")
                                                              nv = juce::jmin (nv, juce::jmax (v, 118.0f));
        if (id == "DelayFeedback")                            nv = juce::jmin (nv, juce::jmax (v, 110.0f));
        if (id == "OscMainvolume")                            nv = juce::jlimit (v - 10.0f, v + 10.0f, nv);

        if (nv != v)
        {
            values[(size_t) r.index] = nv;
            ++moved;
        }
    }

    return moved;
}

} // namespace aquavibrio
