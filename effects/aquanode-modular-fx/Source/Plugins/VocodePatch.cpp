//==============================================================================
// Vocode - Patch version
//
// Vocode is one algorithm, so its patch version is one module: "Vocode -
// Vocoder", the plugin's filter-bank vocoder (VocodeAudioProcessor::
// processBlock) sample by sample - up to 256 bands of cascaded biquad band
// passes, attack / release followers, the per-band level compensation,
// Normalize, the compressor, Chromatic Lock at 133 bands, and the two drawn
// curves (band width and band volume) on its card. Carrier In is the plugin's
// main input, Mod In its side chain; with nothing in Mod In it vocodes the
// carrier with itself, as the plugin's auto-vocode does.
//
// The plugin updates its normalisation once per host block; the module does it
// every 32 samples (the plugin at a 32-sample buffer).
//==============================================================================
#include "PluginVocodeConfig.h"

namespace aqp_vocode
{
    #include AQP_VOCODE_SRC(PluginProcessor.h)
}

#include "PatchKit.h"
#include "PatchWidgets.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "vocode" };
    constexpr int kMaxBands = 256;
    constexpr int kMaxOrder = 4;
    constexpr int kControlBlock = 32;
}

class VocodeVocoderModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.vocode.vocoder";
        d.displayName = kit::moduleName (kPlugin, "Vocoder");
        d.description = "Vocode whole: up to 256 bands between Freq Start and End, Mod In's band envelopes "
                        "gating the same bands of Carrier In. Band Order steepens every band, the drawn curves "
                        "set each band's width and volume. With Mod In empty it vocodes the carrier itself.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { audioIn ("carrierIn", "Carrier In"), audioIn ("modIn", "Mod In"),
                      audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("numBands",  "Bands",   1.0f, 256.0f, 16.0f, 0, {}, false, 1.0f).noMod(),
            makeRotary ("attack",    "Attack",  1.0f, 500.0f, 10.0f, 0, "ms", true),
            makeRotary ("release",   "Release", 10.0f, 2000.0f, 120.0f, 0, "ms", true),
            makeRotary ("bandOrder", "Order",   1.0f, 4.0f, 1.0f, 0, {}, false, 1.0f),
            makeRotary ("compression", "Comp",  0.0f, 1.0f, 0.0f, 0),

            makeRotary ("freqStart", "Freq Start", 20.0f, 2000.0f, 80.0f, 1, "Hz", true).noMod(),
            makeRotary ("freqEnd",   "Freq End",   1000.0f, 24000.0f, 12000.0f, 1, "Hz", true).noMod(),
            makeRotary ("normalize", "Normalize",  0.0f, 1.0f, 0.0f, 1),
            makeRotary ("dryWet",    "Dry/Wet",    0.0f, 1.0f, 1.0f, 1),
            makeRotary ("outputGain", "Gain",      0.0f, 24.0f, 0.0f, 1, "dB"),
            makeCombo  ("chromaticLock", "Chromatic Lock", { "Off", "On" }, 0, 2, 2),
            makeButton ("resetCurves", "Reset Curves", 2, 2)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        iBands = paramIndex ("numBands"); iAtt = paramIndex ("attack"); iRel = paramIndex ("release");
        iOrder = paramIndex ("bandOrder"); iComp = paramIndex ("compression"); iStart = paramIndex ("freqStart");
        iEnd = paramIndex ("freqEnd"); iNorm = paramIndex ("normalize"); iWet = paramIndex ("dryWet");
        iGain = paramIndex ("outputGain"); iLock = paramIndex ("chromaticLock");
        currentBands = -1;
        compEnv = 0.0f;
        for (auto& n : normAvg) n = 0.0f;
        counter = 0;
    }

    void reset() override
    {
        for (auto& b : bands)
        {
            b.env = 0.0f;
            for (auto* f : { b.mod, b.carL, b.carR })
                for (int o = 0; o < kMaxOrder; ++o)
                    f[o].reset();
        }
        compEnv = 0.0f;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (counter-- <= 0)
        {
            counter = kControlBlock - 1;
            control();
        }

        const float carL = in[0][0], carR = in[0][1];
        const bool autoVocode = ! isInputConnected (1);
        const float modSamp = autoVocode ? 0.5f * (carL + carR) : 0.5f * (in[1][0] + in[1][1]);

        float sumL = 0.0f, sumR = 0.0f;
        constexpr float kEnvSens = 15.0f;

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[(size_t) b];
            float filtered = modSamp;
            for (int o = 0; o < order; ++o)
                filtered = band.mod[o].process (filtered);

            const float rect = std::abs (filtered);
            band.env = rect > band.env ? attackCoeff * band.env + (1.0f - attackCoeff) * rect
                                       : releaseCoeff * band.env + (1.0f - releaseCoeff) * rect;
            levelAccum[(size_t) b] += band.env;

            const float envMod = juce::jmin (1.0f, band.env * kEnvSens * levelComp[(size_t) b] * normGain[(size_t) b]);

            float cL = carL, cR = carR;
            for (int o = 0; o < order; ++o)
            {
                cL = band.carL[o].process (cL);
                cR = band.carR[o].process (cR);
            }
            const float vol = juce::jlimit (0.0f, 1.2f, volumeCurve.get (b));
            sumL += cL * envMod * vol;
            sumR += cR * envMod * vol;
        }
        ++accumulated;

        if (compAmt > 0.001f)
        {
            const float peak = juce::jmax (std::abs (sumL), std::abs (sumR));
            compEnv = peak > compEnv ? cAtt * compEnv + (1.0f - cAtt) * peak
                                     : cRel * compEnv + (1.0f - cRel) * peak;
            float reduction = 1.0f;
            if (compEnv > 0.25f)
            {
                const float overDb = 20.0f * std::log10 (compEnv / 0.25f);
                reduction = std::pow (10.0f, -overDb * (1.0f - 1.0f / compRatio) / 20.0f);
            }
            sumL *= reduction * compMakeup;
            sumR *= reduction * compMakeup;
        }

        out[0] = { ((1.0f - dryWet) * carL + dryWet * sumL) * outGain,
                   ((1.0f - dryWet) * carR + dryWet * sumR) * outGain };
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id == "resetCurves")
        {
            widthCurve.fill (0.5f);
            volumeCurve.fill (1.0f);
            curvesChanged = true;
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        std::vector<CurveDisplay::Layer> layers {
            { &widthCurve,  "Band Width",  juce::Colour (0xff9fe0ff), 0.5f },
            { &volumeCurve, "Band Volume", juce::Colour (0xffe0c27f), 1.0f }
        };
        auto c = std::make_unique<CurveDisplay> (*this, std::move (layers));
        juce::WeakReference<SynthModule> weak (this);
        c->onEdited = [weak]
        {
            if (auto* m = dynamic_cast<VocodeVocoderModule*> (weak.get()))
                m->curvesChanged = true;
        };
        return c;
    }
    int extraContentHeight() const override { return 120; }

    juce::String saveCustomState() const override { return widthCurve.toBase64() + "|" + volumeCurve.toBase64(); }
    void loadCustomState (const juce::String& s) override
    {
        widthCurve.fromBase64 (s.upToFirstOccurrenceOf ("|", false, false));
        volumeCurve.fromBase64 (s.fromFirstOccurrenceOf ("|", false, false));
        curvesChanged = true;
    }

    PointCurve& widths() { return widthCurve; }
    PointCurve& volumes() { return volumeCurve; }

private:
    struct Biquad
    {
        float b0 {}, b1 {}, b2 {}, a1 {}, a2 {}, z1 {}, z2 {};
        float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void reset() { z1 = z2 = 0.0f; }
        void setBandpass (float fc, float Q, float sr)
        {
            fc = juce::jlimit (20.0f, sr * 0.499f, fc);
            Q = juce::jmax (0.05f, Q);
            const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
            const float alpha = std::sin (w0) / (2.0f * Q);
            const float inv = 1.0f / (1.0f + alpha);
            b0 = alpha * inv; b1 = 0.0f; b2 = -alpha * inv;
            a1 = -2.0f * std::cos (w0) * inv; a2 = (1.0f - alpha) * inv;
        }
    };

    struct Band { Biquad mod[kMaxOrder], carL[kMaxOrder], carR[kMaxOrder]; float env { 0 }, centre { 440 }; };

    // the per-block part of VocodeAudioProcessor::processBlock
    void control()
    {
        const int nb = juce::jlimit (1, kMaxBands, (int) param (iBands));
        const bool lock = param (iLock) >= 0.5f;
        const float start = param (iStart), end = param (iEnd);
        const float sr = (float) sampleRate;
        order = juce::jlimit (1, kMaxOrder, (int) param (iOrder));

        const bool topology = nb != currentBands || lock != currentLock || start != currentStart || end != currentEnd;
        if (topology)
        {
            currentBands = nb; currentLock = lock; currentStart = start; currentEnd = end;
            // rebuildFilters(): centre frequencies, every band's state cleared
            const float lo = juce::jmax (20.0f, start), hi = juce::jmax (lo + 1.0f, end);
            const float logRatio = std::log (hi / lo);
            const bool chromatic = nb == 133 && lock;
            for (int b = 0; b < kMaxBands; ++b)
            {
                auto& band = bands[(size_t) b];
                band.env = 0.0f;
                if (chromatic && b < 133)
                    band.centre = 440.0f * std::pow (2.0f, (float) (b - 69) / 12.0f);
                else
                    band.centre = lo * std::exp (logRatio * (nb > 1 ? juce::jlimit (0.0f, 1.0f, (float) b / (float) (nb - 1)) : 0.0f));
                for (auto* f : { band.mod, band.carL, band.carR })
                    for (int o = 0; o < kMaxOrder; ++o)
                        f[o].reset();
            }
            for (auto& l : levelAccum) l = 0.0f;
            accumulated = 0;
        }

        // band filters and level compensation (the plugin recomputes these
        // every block; here only when something they depend on moved)
        if (topology || order != lastOrder || curvesChanged.exchange (false))
        {
            lastOrder = order;
            numBands = nb;
            const float globalQ = juce::jmax (1.0f, (float) nb * 0.35f) * 2.0f;
            const float orderComp = std::sqrt ((float) order);
            for (int b = 0; b < nb; ++b)
            {
                auto& band = bands[(size_t) b];
                const float qMult = juce::jmap (juce::jlimit (0.0f, 1.0f, widthCurve.get (b)), 0.0f, 1.0f, 2.0f, 0.5f);
                const float q = juce::jmax (0.1f, globalQ * qMult);
                levelComp[(size_t) b] = std::sqrt (qMult) * orderComp * std::pow (band.centre / 1000.0f, 0.30f);
                for (int o = 0; o < kMaxOrder; ++o)
                {
                    band.mod[o].setBandpass (band.centre, q, sr);
                    band.carL[o].setBandpass (band.centre, q, sr);
                    band.carR[o].setBandpass (band.centre, q, sr);
                }
            }
        }

        attackCoeff = std::exp (-1.0f / juce::jmax (1.0f, param (iAtt) * 0.001f * sr));
        releaseCoeff = std::exp (-1.0f / juce::jmax (1.0f, param (iRel) * 0.001f * sr));

        // Normalize: the previous block's band levels into slow averages
        const float normAmt = param (iNorm);
        const float normSmooth = std::exp (-(float) kControlBlock / (0.4f * sr));
        float total = 0.0f; int active = 0;
        for (int b = 0; b < numBands; ++b)
        {
            const float lvl = accumulated > 0 ? levelAccum[(size_t) b] / (float) accumulated : 0.0f;
            levelAccum[(size_t) b] = 0.0f;
            normAvg[(size_t) b] = normSmooth * normAvg[(size_t) b] + (1.0f - normSmooth) * lvl;
            if (normAvg[(size_t) b] > 1e-7f) { total += normAvg[(size_t) b]; ++active; }
        }
        accumulated = 0;
        const float mean = active > 0 ? total / (float) active : 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            if (mean < 1e-8f || normAmt < 0.001f)
                normGain[(size_t) b] = 1.0f;
            else
                normGain[(size_t) b] = 1.0f + normAmt * (juce::jlimit (0.1f, 10.0f, mean / juce::jmax (1e-7f, normAvg[(size_t) b])) - 1.0f);
        }

        compAmt = param (iComp);
        compRatio = juce::jmap (compAmt, 0.0f, 1.0f, 1.0f, 20.0f);
        compMakeup = std::pow (10.0f, juce::jmap (compAmt, 0.0f, 1.0f, 0.0f, 10.0f) / 20.0f);
        cAtt = std::exp (-1.0f / (0.005f * sr));
        cRel = std::exp (-1.0f / (0.100f * sr));
        dryWet = param (iWet);
        outGain = std::pow (10.0f, param (iGain) / 20.0f);
    }

    std::array<Band, kMaxBands> bands;
    std::array<float, kMaxBands> levelComp {}, normGain {}, normAvg {}, levelAccum {};
    PointCurve widthCurve { kMaxBands, 0.0f, 1.0f, 0.5f };
    PointCurve volumeCurve { kMaxBands, 0.0f, 1.2f, 1.0f };
    std::atomic<bool> curvesChanged { true };

    int iBands {}, iAtt {}, iRel {}, iOrder {}, iComp {}, iStart {}, iEnd {}, iNorm {}, iWet {}, iGain {}, iLock {};
    int currentBands { -1 }, numBands { 16 }, order { 1 }, lastOrder { -1 }, counter { 0 }, accumulated { 0 };
    bool currentLock { false };
    float currentStart { 0 }, currentEnd { 0 };
    float attackCoeff { 0 }, releaseCoeff { 0 }, compEnv { 0 }, compAmt { 0 }, compRatio { 1 }, compMakeup { 1 };
    float cAtt { 0 }, cRel { 0 }, dryWet { 1 }, outGain { 1 };
};

AQUANODE_REGISTER_MODULE (VocodeVocoderModule, VocodeVocoderModule::descriptor)

//==============================================================================
namespace aquanode::plugins::vocode
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int voc = b.add ("plugin.vocode.vocoder", 0, 0);
        b.copy (voc, original);

        // the drawn curves live in the plugin itself, not in parameters
        if (auto* v = dynamic_cast<aqp_vocode::VocodeAudioProcessor*> (&original))
            if (auto* m = dynamic_cast<VocodeVocoderModule*> (b.dsp (voc)))
            {
                for (int i = 0; i < kMaxBands; ++i)
                {
                    m->widths().set (i, v->bandwidthCurve[i]);
                    m->volumes().set (i, v->bandVolumeCurve[i]);
                }
                m->loadCustomState (m->saveCustomState());   // flags the bands for a rebuild
            }

        PatchPorts ports;
        ports.name = "Vocode";
        ports.audioIn = { { voc, "carrierIn" } };
        ports.sideIn = { { voc, "modIn" } };
        ports.audioOut = { { voc, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
