//==============================================================================
// TapElectric - Patch version
//
// The plugin's three parts as modules:
//
//     TapElectric - Hum ---------+
//                                +--> Tape In's "Hum / Noise In"
//     TapElectric - Tape Noise --+
//     Audio In --> TapElectric - Tape In --> Audio Out
//
// Hum is the 50/60 Hz hum with six harmonics, each with its own slow level
// fluctuation, and a slow pitch drift. Tape Noise is white noise through the
// three bell EQ bands. Tape In is the input side: drift and wobble on a short
// modulated delay, saturation, the tape mix - and the final sum with the hum
// and noise, scaled by Hum/Noise Mult exactly where the plugin does it.
//
// The DSP is the plugin's own, line for line (PluginProcessor.cpp); only the
// harmonics' level fluctuation is updated every 32 samples instead of once
// per host block, which keeps its speed but makes it smoother.
//==============================================================================
#include "PluginTapElectricConfig.h"
#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "tapelectric" };
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    // the plugin's knobs are NormalisableRanges with this skew
    float fromSkewed (float norm, float lo, float hi, float skew)
    {
        return lo + (hi - lo) * std::exp (std::log (juce::jmax (1.0e-6f, norm)) / skew);
    }

    float pan (float panValue, bool right)
    {
        return std::sqrt (0.5f * (right ? 1.0f + panValue : 1.0f - panValue));
    }
}

//==============================================================================
// TapElectric - Hum
//==============================================================================
class TapElectricHumModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.tapelectric.hum";
        d.displayName = kit::moduleName (kPlugin, "Hum");
        d.description = "Mains hum: 50 or 60 Hz and five harmonics above it, each harmonic's level wandering "
                        "slowly by its own Rand amount and speed, the whole hum drifting in pitch. Rand Tone "
                        "rolls new harmonic levels (and back again on the next press), as on the plugin.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo  ("freqMode", "Base", { "50 Hz", "60 Hz" }, 0, 0, 1),
            makeRotary ("humVol",    "Volume", 0.0f, 2.0f, 0.5f, 0),
            makeRotary ("humPan",    "Pan",    -1.0f, 1.0f, 0.0f, 0),
            makeRotary ("humStereo", "Stereo", 0.0f, 1.0f, 0.0f, 0),
            makeButton ("randTone",  "Rand Tone", 0, 1),

            makeRotary ("humDrift",      "Drift",       0.0f, 10.0f, 1.0f, 1),
            makeRotary ("humDriftRate",  "Drift Rate",  0.01f, 2.0f, 0.07f, 1, "Hz", true),
            makeRotary ("humDriftDepth", "Drift Depth", 0.0f, 0.05f, 0.003f, 1),
        };
        static const float defaults[6] = { 0.569f, 0.091f, 0.411f, 0.131f, 0.084f, 0.020f };
        for (int h = 0; h < 6; ++h)
            d.params.push_back (makeRotary ("h" + juce::String (h + 1), juce::String (h + 1) + "x",
                                            0.0f, 1.0f, defaults[h], 2 + h / 3));
        for (int h = 0; h < 6; ++h)
        {
            const auto n = juce::String (h + 1);
            d.params.push_back (makeRotary ("h" + n + "RandAmt", "H" + n + " Rand", 0.0f, 1.0f, 0.0f, 4 + h / 3));
        }
        for (int h = 0; h < 6; ++h)
        {
            const auto n = juce::String (h + 1);
            d.params.push_back (makeRotary ("h" + n + "RandSpd", "H" + n + " Speed", 0.01f, 100.0f, 0.5f,
                                            6 + h / 3, {}, true));
        }
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
        idx.freqMode = paramIndex ("freqMode");
        idx.vol = paramIndex ("humVol");
        idx.pan = paramIndex ("humPan");
        idx.stereo = paramIndex ("humStereo");
        idx.drift = paramIndex ("humDrift");
        idx.driftRate = paramIndex ("humDriftRate");
        idx.driftDepth = paramIndex ("humDriftDepth");
        for (int h = 0; h < 6; ++h)
        {
            const auto n = juce::String (h + 1);
            idx.level[h] = paramIndex ("h" + n);
            idx.randAmt[h] = paramIndex ("h" + n + "RandAmt");
            idx.randSpd[h] = paramIndex ("h" + n + "RandSpd");
        }
    }

    void reset() override
    {
        phases.fill (0.0);
        modPhases.fill (0.0);
        driftPhase = 0.0;
        counter = 0;
    }

    void processSample (const StereoFrame*, StereoFrame* out) override
    {
        // the harmonics' level fluctuation, at control rate (TapElectric does
        // this once per host block)
        if (counter-- <= 0)
        {
            counter = kControlBlock - 1;
            for (int i = 0; i < 6; ++i)
            {
                gains[i] = param (idx.level[i]);
                const float amt = param (idx.randAmt[i]);
                if (amt > 0.001f)
                {
                    const double lfoFreq = param (idx.randSpd[i]) * 0.1 * kModRates[i];
                    modPhases[i] = std::fmod (modPhases[i] + twoPi * lfoFreq * kControlBlock / sampleRate, twoPi);
                    gains[i] *= std::pow (10.0f, (float) (std::sin (modPhases[i]) * amt * 6.0f) / 20.0f);
                }
            }
        }

        const float baseFreq = param (idx.freqMode) < 0.5f ? 50.0f : 60.0f;
        const double drift = std::sin (driftPhase) * param (idx.drift) * param (idx.driftDepth);
        driftPhase = std::fmod (driftPhase + twoPi * param (idx.driftRate) / sampleRate, twoPi);

        float hum = 0.0f;
        for (int h = 0; h < 6; ++h)
        {
            const double freq = baseFreq * (h + 1) * (1.0 + drift);
            phases[(size_t) h] = std::fmod (phases[(size_t) h] + twoPi * freq / sampleRate, twoPi);
            hum += (float) std::sin (phases[(size_t) h]) * gains[h];
        }
        hum *= param (idx.vol) * 0.4f;   // the plugin's built-in headroom

        const float p = param (idx.pan), st = param (idx.stereo);
        out[0] = { hum * pan (p, false) * (1.0f + st * 0.5f),
                   hum * pan (p, true)  * (1.0f - st * 0.5f) };
    }

    // Rand Tone: random harmonic levels, or back to the defaults when they are
    // random already - the plugin's toggle, for the hum's half of it
    void uiButtonClicked (const juce::String& id) override
    {
        if (id != "randTone")
            return;

        const auto& d = getDescriptor();
        const auto* h1 = d.findParam ("h1");
        const bool isRandom = std::abs (getParameter ("h1") - h1->defaultValue) > 0.001f;
        auto& rng = juce::Random::getSystemRandom();
        for (int h = 0; h < 6; ++h)
        {
            const auto pid = "h" + juce::String (h + 1);
            const auto* spec = d.findParam (pid);
            setParameter (pid, isRandom ? spec->defaultValue : fromSkewed (rng.nextFloat(), 0.0f, 1.0f, 0.3f));
        }
    }

private:
    static constexpr int kControlBlock = 32;
    static constexpr double kModRates[6] = { 1.0, 1.13, 0.87, 1.21, 0.93, 1.07 };

    struct Idx { int freqMode, vol, pan, stereo, drift, driftRate, driftDepth, level[6], randAmt[6], randSpd[6]; } idx {};
    std::array<double, 6> phases {}, modPhases {};
    float gains[6] {};
    double driftPhase { 0.0 };
    int counter { 0 };
};

//==============================================================================
// TapElectric - Tape Noise
//==============================================================================
class TapElectricNoiseModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.tapelectric.noise";
        d.displayName = kit::moduleName (kPlugin, "Tape Noise");
        d.description = "Tape hiss: white noise, independent left and right, shaped by three bell EQ bands "
                        "(the plugin's Noise EQ next to its title), with its own level, pan and stereo.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 401;
        d.sockets = { audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("noiseVol",    "Volume", 0.0f, 1.0f, 0.08f, 0),
            makeRotary ("noisePan",    "Pan",    -1.0f, 1.0f, 0.57f, 0),
            makeRotary ("noiseStereo", "Stereo", 0.0f, 1.0f, 0.64f, 0),
            makeButton ("randEq",      "Rand EQ", 0, 1),
        };
        static const float f[3] = { 20.0f, 6200.0f, 8000.0f }, g[3] = { 17.4f, -4.0f, 0.0f }, q[3] = { 0.1f, 0.3f, 1.2f };
        for (int b = 0; b < 3; ++b)
        {
            const auto n = juce::String (b + 1);
            d.params.push_back (makeRotary ("eqFreq" + n, "EQ" + n + " Freq", 20.0f, 20000.0f, f[b], 1 + b, "Hz", true));
            d.params.push_back (makeRotary ("eqGain" + n, "EQ" + n + " Gain", -18.0f, 18.0f, g[b], 1 + b, "dB"));
            d.params.push_back (makeRotary ("eqQ" + n,    "EQ" + n + " Q",    0.1f, 10.0f, q[b], 1 + b));
        }
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (int b = 0; b < 3; ++b)
        {
            const auto n = juce::String (b + 1);
            idx.f[b] = paramIndex ("eqFreq" + n);
            idx.g[b] = paramIndex ("eqGain" + n);
            idx.q[b] = paramIndex ("eqQ" + n);
            last[b][0] = -1.0f;
        }
        idx.vol = paramIndex ("noiseVol");
        idx.pan = paramIndex ("noisePan");
        idx.stereo = paramIndex ("noiseStereo");
        reset();
    }

    void reset() override
    {
        for (auto& ch : filters)
            for (auto& f : ch)
                f.reset();
    }

    void processSample (const StereoFrame*, StereoFrame* out) override
    {
        if (counter-- <= 0)
        {
            counter = 31;
            for (int b = 0; b < 3; ++b)
            {
                const float fq = param (idx.f[b]), gn = param (idx.g[b]), qq = param (idx.q[b]);
                if (fq != last[b][0] || gn != last[b][1] || qq != last[b][2])
                {
                    last[b][0] = fq; last[b][1] = gn; last[b][2] = qq;
                    auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                        sampleRate, juce::jlimit (20.0f, (float) sampleRate * 0.49f, fq), qq,
                        juce::Decibels::decibelsToGain (gn));
                    for (auto& ch : filters)
                        ch[(size_t) b].coefficients = c;
                }
            }
        }

        float l = randomL.nextFloat() * 2.0f - 1.0f;
        float r = randomR.nextFloat() * 2.0f - 1.0f;
        for (size_t b = 0; b < 3; ++b)
        {
            l = filters[0][b].processSample (l);
            r = filters[1][b].processSample (r);
        }

        const float vol = param (idx.vol), p = param (idx.pan), st = param (idx.stereo);
        out[0] = { l * vol * pan (p, false) * (1.0f + st * 0.5f),
                   r * vol * pan (p, true)  * (1.0f - st * 0.5f) };
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id != "randEq")
            return;

        // the plugin's Rand Tone, for the EQ's half: frequency and gain of
        // each band, random or back to the defaults
        const auto& d = getDescriptor();
        const bool isRandom = std::abs (getParameter ("eqFreq1") - d.findParam ("eqFreq1")->defaultValue) > 0.01f;
        auto& rng = juce::Random::getSystemRandom();
        for (int b = 0; b < 3; ++b)
        {
            const auto n = juce::String (b + 1);
            setParameter ("eqFreq" + n, isRandom ? d.findParam ("eqFreq" + n)->defaultValue
                                                 : fromSkewed (rng.nextFloat(), 20.0f, 20000.0f, 0.3f));
            setParameter ("eqGain" + n, isRandom ? d.findParam ("eqGain" + n)->defaultValue
                                                 : -18.0f + 36.0f * rng.nextFloat());
        }
    }

private:
    struct Idx { int f[3], g[3], q[3], vol, pan, stereo; } idx {};
    std::array<std::array<juce::dsp::IIR::Filter<float>, 3>, 2> filters;
    float last[3][3] {};
    int counter { 0 };
    juce::Random randomL { juce::Time::currentTimeMillis() };
    juce::Random randomR { juce::Time::currentTimeMillis() + 12345 };
};

//==============================================================================
// TapElectric - Tape In
//==============================================================================
class TapElectricTapeInModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.tapelectric.tapein";
        d.displayName = kit::moduleName (kPlugin, "Tape In");
        d.description = "The tape machine the input goes through: slow drift and faster wobble on a short "
                        "modulated delay, tanh saturation, the tape mix - then the Hum / Noise In signal added "
                        "on top, both scaled by Hum/Noise Mult the way TapElectric sums them.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 402;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("humIn", "Hum/Noise In"),
                      audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("mix",        "Hum/Noise Mult", 0.0f, 0.5f, 0.033f, 0),
            makeRotary ("inputVol",   "Tape Mix",   0.0f, 2.0f, 1.0f, 0),
            makeRotary ("saturation", "Saturation", 1.0f, 10.0f, 1.0f, 0),
            makeRotary ("inputDelayBase", "Delay Base", 1.0f, 50.0f, 10.0f, 0),
            makeRotary ("inputDelayMod",  "Delay Mod",  0.0f, 2000.0f, 1000.0f, 0),

            makeRotary ("inputDrift",      "Drift",       0.0f, 10.0f, 0.0f, 1),
            makeRotary ("inputDriftRate",  "Drift Rate",  0.01f, 5.0f, 0.1f, 1, "Hz", true),
            makeRotary ("inputDriftDepth", "Drift Depth", 0.0f, 0.2f, 0.02f, 1),

            makeRotary ("inputWobble",      "Wobble",       0.0f, 10.0f, 0.0f, 2),
            makeRotary ("inputWobbleRate",  "Wobble Rate",  0.01f, 10.0f, 0.5f, 2, "Hz", true),
            makeRotary ("inputWobbleDepth", "Wobble Depth", 0.0f, 0.3f, 0.05f, 2),
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int size = (int) (sr * 0.05);
        for (auto& b : delay)
            b.assign ((size_t) juce::jmax (8, size), 0.0f);
        reset();

        i.mix = paramIndex ("mix"); i.tape = paramIndex ("inputVol"); i.sat = paramIndex ("saturation");
        i.base = paramIndex ("inputDelayBase"); i.dmod = paramIndex ("inputDelayMod");
        i.drift = paramIndex ("inputDrift"); i.driftRate = paramIndex ("inputDriftRate");
        i.driftDepth = paramIndex ("inputDriftDepth"); i.wob = paramIndex ("inputWobble");
        i.wobRate = paramIndex ("inputWobbleRate"); i.wobDepth = paramIndex ("inputWobbleDepth");
    }

    void reset() override
    {
        for (auto& b : delay)
            std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0;
        driftPhase = wobblePhase = 0.0;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float globalMix = param (i.mix);
        const float tapeMix = param (i.tape);
        const float saturation = param (i.sat);
        const float inputDrift = param (i.drift), inputWobble = param (i.wob);

        const double driftLfo = std::sin (driftPhase) * inputDrift * param (i.driftDepth);
        driftPhase = std::fmod (driftPhase + twoPi * param (i.driftRate) / sampleRate, twoPi);
        const double wobbleLfo = std::sin (wobblePhase) * inputWobble * param (i.wobDepth);
        wobblePhase = std::fmod (wobblePhase + twoPi * param (i.wobRate) / sampleRate, twoPi);
        const double combined = driftLfo + wobbleLfo;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = in[0][(size_t) ch];
            float wet = dry;
            auto& buf = delay[(size_t) ch];

            if (inputDrift > 0.001f || inputWobble > 0.001f)
            {
                buf[(size_t) writePos] = wet;
                float t = param (i.base) + (float) (combined * param (i.dmod));
                t = juce::jlimit (2.0f, (float) buf.size() - 2.0f, t);
                const int dInt = (int) std::floor (t);
                const float frac = t - (float) dInt;
                const int size = (int) buf.size();
                const int rp1 = (writePos - dInt + size) % size;
                const int rp2 = (rp1 - 1 + size) % size;
                wet = buf[(size_t) rp1] * (1.0f - frac) + buf[(size_t) rp2] * frac;
            }

            if (saturation > 1.001f)
                wet = std::tanh (wet * saturation) / std::sqrt (saturation);

            const float tape = dry * (1.0f - tapeMix) + wet * tapeMix;
            out[0][(size_t) ch] = tape * (1.0f - globalMix) + in[1][(size_t) ch] * globalMix;
        }

        writePos = (writePos + 1) % (int) delay[0].size();
    }

private:
    struct { int mix, tape, sat, base, dmod, drift, driftRate, driftDepth, wob, wobRate, wobDepth; } i {};
    std::array<std::vector<float>, 2> delay;
    int writePos { 0 };
    double driftPhase { 0.0 }, wobblePhase { 0.0 };
};

AQUANODE_REGISTER_MODULE (TapElectricHumModule,    TapElectricHumModule::descriptor)
AQUANODE_REGISTER_MODULE (TapElectricNoiseModule,  TapElectricNoiseModule::descriptor)
AQUANODE_REGISTER_MODULE (TapElectricTapeInModule, TapElectricTapeInModule::descriptor)

//==============================================================================
namespace aquanode::plugins::tapelectric
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int hum   = b.add ("plugin.tapelectric.hum",    0, 0);
        const int noise = b.add ("plugin.tapelectric.noise",  0, 760);
        const int tape  = b.add ("plugin.tapelectric.tapein", 320, 300);

        for (int m : { hum, noise, tape })
            b.copy (m, original);

        b.cable (hum,   "audioOut", tape, "humIn");
        b.cable (noise, "audioOut", tape, "humIn");

        PatchPorts ports;
        ports.name = "TapElectric";
        ports.audioIn = { { tape, "audioIn" } };
        ports.audioOut = { { tape, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
