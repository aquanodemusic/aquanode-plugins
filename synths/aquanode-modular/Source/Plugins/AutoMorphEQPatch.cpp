//==============================================================================
// AutoMorphEQ - Patch version
//
// AutoMorphEQ is seven bell filters in series, each morphing on its own from a
// start frequency and level to an end frequency and level - round and round
// over Speed seconds in one of seven shapes (or held at a manual Position) -
// then, optionally, only what the bells changed (Wet Only), hard-limited at
// 0 dB. The patch version is that, module by module:
//
//   Audio In --> Band 1 --> Band 2 --> ... --> Band 7 --> Output
//       \___________________________________________/ (Dry In, for Wet Only)
//
// Each Band runs AutoMorphEQ's own band: the morph advanced every 32 samples
// (the plugin moves its bells once per block), the seven shapes - Random is
// its sample and hold with the 5 % crossfade - and JUCE's peak filter as in
// the plugin. Morph Out is where the band is between start (0) and end (1).
//==============================================================================
#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "automorpheq" };
    constexpr int kBands = 7;
    constexpr int kControlBlock = 32;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.automorpheq." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    const char* const kBandIds[] = { "start_freq", "end_freq", "start_vol", "end_vol", "q", "speed", "pos", "waveform" };
}

//==============================================================================
// AutoMorphEQ - Band
//==============================================================================
class AutoMorphEQBandModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("band", "Band", ModuleSection::Filter, 0);
        d.description = "One AutoMorphEQ band: a bell filter that morphs from Start to End (frequency and level, "
                        "1 = unchanged) and back, once every Speed seconds in the chosen shape - or sits at "
                        "Position when Speed is all the way down (below 1 ms). Random holds a new spot every cycle. Morph Out is the band's "
                        "place between start (0) and end (1).";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"), modOut ("morphOut", "Morph Out") };
        d.params = {
            makeRotary ("start_freq", "Start Freq", 20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
            makeRotary ("end_freq", "End Freq", 20.0f, 20000.0f, 2000.0f, 0, "Hz", true),
            makeRotary ("start_vol", "Start Level", 0.01f, 10.0f, 1.0f, 0, "x", true),
            makeRotary ("end_vol", "End Level", 0.01f, 10.0f, 1.0f, 0, "x", true),
            makeRotary ("q", "Q", 0.5f, 100.0f, 1.0f, 0, {}, true),

            makeRotary ("speed", "Speed", 0.00001f, 10.0f, 0.00001f, 1, "s", true),
            makeRotary ("pos", "Position", 0.0f, 100.0f, 0.0f, 1, "%"),
            makeCombo ("waveform", "Shape", { "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Tanh", "Random" }, 0, 1, 2)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 1000.0f, 1.0f, 1.0f);
        for (auto& f : filter)
        {
            f.coefficients = new juce::dsp::IIR::Coefficients<float> (*coeffs);
            f.reset();
        }
        phase = 0.0;
        randomCurrent = 0.0f;
        randomTarget = juce::Random::getSystemRandom().nextFloat();
        randomLastPhase = 0.0;
        counter = 0;
    }

    void reset() override
    {
        for (auto& f : filter) f.reset();
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (counter-- <= 0)
        {
            counter = kControlBlock - 1;
            for (auto& f : filter) f.snapToZero();
            updateBell();
        }
        out[0] = { filter[0].processSample (in[0][0]), filter[1].processSample (in[0][1]) };
        out[1] = { morph, morph };
    }

private:
    // the band's setting for this block, as AutoMorphEQ's processBlock works it out
    void updateBell()
    {
        const float startFreq = param (0), endFreq = param (1), startVol = param (2), endVol = param (3);
        float bandQ = param (4);
        const float speed = param (5), manualPos = param (6);
        const int waveform = (int) std::lround (param (7));

        float m = 0.0f;
        if (speed <= 0.001f)
        {
            m = manualPos / 100.0f;
        }
        else
        {
            phase += ((double) kControlBlock / sampleRate) / speed;
            if (phase > 1.0) phase -= 1.0;
            m = shape ((float) phase, waveform);
        }
        if (! std::isfinite (m))
            m = 0.0f;
        morph = m = juce::jlimit (0.0f, 1.0f, m);

        float freq = startFreq + (endFreq - startFreq) * m;
        float vol = startVol + (endVol - startVol) * m;
        if (! std::isfinite (freq) || freq <= 0.0f)
            freq = 1000.0f;
        freq = juce::jlimit (20.0f, juce::jmin (20000.0f, (float) (sampleRate * 0.49)), freq);
        if (! std::isfinite (vol) || vol <= 0.0f)
            vol = 1.0f;
        vol = juce::jlimit (0.0001f, 10.0f, vol);
        if (! std::isfinite (bandQ) || bandQ <= 0.0f)
            bandQ = 1.0f;
        bandQ = juce::jlimit (0.1f, 100.0f, bandQ);

        coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, freq, bandQ, vol);
        for (auto& f : filter)
            *f.coefficients = *coeffs;
    }

    // AutoMorphEQ's applyWaveformShape
    float shape (float p, int waveform)
    {
        switch (waveform)
        {
            case 1: return p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
            case 2: return p;
            case 3: return 1.0f - p;
            case 4: return p < 0.5f ? 0.0f : 1.0f;
            case 5: return (std::tanh (1.5f * std::sin (juce::MathConstants<float>::twoPi * p)) + 1.0f) * 0.5f;
            case 6:
            {
                if (p < randomLastPhase)
                {
                    randomCurrent = randomTarget;
                    randomTarget = juce::Random::getSystemRandom().nextFloat();
                }
                randomLastPhase = p;
                constexpr float crossfade = 0.05f;
                if (p < crossfade)
                {
                    const float s = (1.0f - std::cos ((p / crossfade) * juce::MathConstants<float>::pi)) * 0.5f;
                    return randomCurrent + (randomTarget - randomCurrent) * s;
                }
                return randomTarget;
            }
            default: return (float) (0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi * p)));
        }
    }

    std::array<juce::dsp::IIR::Filter<float>, 2> filter;
    juce::dsp::IIR::Coefficients<float>::Ptr coeffs;
    double phase { 0.0 }, randomLastPhase { 0.0 };
    float randomCurrent { 0.0f }, randomTarget { 0.5f }, morph { 0.0f };
    int counter { 0 };
};

//==============================================================================
// AutoMorphEQ - Output
//==============================================================================
class AutoMorphEQOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("output", "Output", ModuleSection::Utility, 1);
        d.description = "AutoMorphEQ's last stage: Wet Only takes the dry signal (Dry In) away, leaving only "
                        "what the bells changed; then the hard limit at 0 dB.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("dryIn", "Dry In"), audioOut ("audioOut", "Audio Out") };
        d.params = { makeCombo ("wet_only", "Wet Only", { "Off", "On" }, 0, 0, 2) };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const bool wetOnly = param (0) >= 0.5f;
        float l = in[0][0], r = in[0][1];
        if (wetOnly)
        {
            l -= in[1][0];
            r -= in[1][1];
        }
        out[0] = { juce::jlimit (-1.0f, 1.0f, l), juce::jlimit (-1.0f, 1.0f, r) };
    }
};

AQUANODE_REGISTER_MODULE (AutoMorphEQBandModule,   AutoMorphEQBandModule::descriptor)
AQUANODE_REGISTER_MODULE (AutoMorphEQOutputModule, AutoMorphEQOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::automorpheq
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        PatchPorts ports;
        ports.name = "AutoMorphEQ";

        int previous = -1;
        for (int band = 0; band < kBands; ++band)
        {
            const int m = b.add ("plugin.automorpheq.band", (band % 4) * 300, (band / 4) * 300);
            const juce::String suffix = "_" + juce::String (band);
            for (auto* id : kBandIds)
                b.set (m, id, kit::param (original, id + suffix));

            if (previous < 0) ports.audioIn.push_back ({ m, "audioIn" });
            else              b.cable (previous, "audioOut", m, "audioIn");
            previous = m;
        }

        const int output = b.add ("plugin.automorpheq.output", 1200, 300);
        b.copy (output, original);
        b.cable (previous, "audioOut", output, "audioIn");
        ports.audioIn.push_back ({ output, "dryIn" });
        ports.audioOut = { { output, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
