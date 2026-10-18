//==============================================================================
// FlowEQ - Patch version
//
// FlowEQ's three bands are three "FlowEQ - Bell" modules in series, and each
// of the curves that moves them is a "FlowEQ - Flow Curve" module: a drawn,
// cyclic 128-point curve (or a 2048-sample wavetable frame) with its own
// cycle time or tempo sync. A curve patched into a Bell's Freq / Gain / Q
// Curve input moves that value exactly as in the plugin:
//
//     Freq  absolute, 20 Hz - 20 kHz on a log scale (the curve IS the frequency)
//     Gain  added to the knob, centre = no change, +-24 dB at the edges
//     Q     multiplied, centre = no change, +-2 octaves at the edges
//
// with FlowEQ's 12 ms smoothing on the result. A Bell input with nothing
// patched leaves the knob alone - the plugin's "undrawn curve". The builder
// therefore only makes Flow Curves for the curves that are drawn (or use a
// wavetable). The plugin's gesture recording stays in its GUI version.
//==============================================================================
#include "PluginFlowEQConfig.h"

namespace aqp_floweq
{
    #include AQP_FLOWEQ_SRC(PluginProcessor.h)
}

#include "PatchKit.h"
#include "PatchWidgets.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "floweq" };
    constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f, kMaxGainDb = 24.0f;
    constexpr float kMinQ = 0.1f, kMaxQ = 10.0f;
    constexpr int kControlBlock = 32;

    juce::StringArray syncChoices()
    {
        juce::StringArray a;
        for (auto& d : aqp_floweq::getSyncDivisions())
            a.add (d.label);
        return a;
    }
}

//==============================================================================
// FlowEQ - Flow Curve
//==============================================================================
class FlowEQCurveModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.floweq.curve";
        d.displayName = kit::moduleName (kPlugin, "Flow Curve");
        d.description = "One of FlowEQ's drawn curves: 128 points played round and round over Time (or a "
                        "synced note value), out as 0..1 on Curve Out. Patch it into a FlowEQ Bell's Freq, "
                        "Gain or Q Curve input - or into any knob. Load WT plays a 2048-sample wavetable frame "
                        "instead of the drawing.";
        d.section = ModuleSection::Utility;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 401;
        d.sockets = { modOut ("curveOut", "Curve Out") };
        d.params = {
            makeRotary ("time", "Time", 0.01f, 120.0f, 2.0f, 0, "s", true),
            makeCombo ("sync", "Sync", { "Off", "On" }, 0, 0, 1),
            makeSteppedList ("syncdiv", "Sync Div", syncChoices(), 6, 0),
            makeButton ("smooth", "Smooth", 1, 1),
            makeButton ("flat",   "Flat",   1, 1),
            makeButton ("loadWT", "Load WT", 1, 1),
            makeButton ("drawn",  "Drawn",  1, 1),
            makeRotary ("frame", "Frame", 0.0f, 255.0f, 0.0f, 1, {}, false, 1.0f)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        iTime = paramIndex ("time"); iSync = paramIndex ("sync"); iDiv = paramIndex ("syncdiv");
        iFrame = paramIndex ("frame");
    }

    void reset() override { phase = 0.0; }

    void processSample (const StereoFrame*, StereoFrame* out) override
    {
        double seconds = param (iSync) >= 0.5f ? aqp_floweq::syncDivisionToSeconds ((int) param (iDiv), tempoBpm)
                                               : (double) param (iTime);
        seconds = juce::jmax (0.0001, seconds);
        phase += 1.0 / (sampleRate * seconds);
        phase -= std::floor (phase);
        livePhase.store ((float) phase, std::memory_order_relaxed);

        out[0] = { valueAt ((float) phase), 0.0f };
    }

    float valueAt (float ph) const
    {
        if (useTable.load (std::memory_order_relaxed))
        {
            std::shared_ptr<const juce::AudioBuffer<float>> t;
            {
                const juce::SpinLock::ScopedLockType sl (tableLock);
                t = table;
            }
            if (t != nullptr && t->getNumSamples() >= 2048)
            {
                const int frames = t->getNumSamples() / 2048;
                const int f = juce::jlimit (0, frames - 1, (int) param (iFrame));
                const float* frame = t->getReadPointer (0, f * 2048);
                return aqp_floweq::catmullRomPeriodic (2048, ph, [frame] (int i) { return frame[i] * 0.5f + 0.5f; });
            }
        }
        return curve.interpolatedPeriodic (ph);
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id == "smooth")  curve.smooth (true);
        if (id == "flat")    curve.fill (0.5f);
        if (id == "drawn")   useTable = false;
        if (id == "loadWT")  chooseWavetable();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        CurveDisplay::Layer layer { &curve, "Curve", kit::accentOf (kPlugin).brighter (0.4f), 0.5f };
        return std::make_unique<CurveDisplay> (*this, std::vector<CurveDisplay::Layer> { layer },
                                               [this] { return livePhase.load (std::memory_order_relaxed); });
    }
    int extraContentHeight() const override { return 110; }

    //=== state ================================================================
    PointCurve& getCurve() { return curve; }

    bool setWavetable (const juce::File& file)
    {
        aqp_floweq::WavetableSet set;
        if (! set.loadFromFile (file))
            return false;
        setWavetableData (std::make_shared<juce::AudioBuffer<float>> (set.data), file.getFullPathName());
        return true;
    }

    void setWavetableData (std::shared_ptr<const juce::AudioBuffer<float>> data, const juce::String& path)
    {
        {
            const juce::SpinLock::ScopedLockType sl (tableLock);
            table = std::move (data);
        }
        tablePath = path;
        useTable = table != nullptr;
    }

    // The curve, and the wavetable as audio stored with the patch - FlowEQ
    // itself keeps only the file's path, which a patch moved to another
    // machine would lose.
    juce::String saveCustomState() const override
    {
        return curve.toBase64() + "|" + (useTable.load() ? "1" : "0") + "|" + tablePath;
    }

    void loadCustomState (const juce::String& s) override
    {
        juce::StringArray parts;
        parts.addTokens (s, "|", {});
        if (parts.size() > 0)
            curve.fromBase64 (parts[0]);
        const bool use = parts.size() > 1 && parts[1] == "1";
        if (parts.size() > 2 && parts[2].isNotEmpty() && table == nullptr)
        {
            tablePath = parts[2];
            const juce::File f (tablePath);
            if (f.existsAsFile())
                setWavetable (f);
        }
        useTable = use && table != nullptr;
    }

    bool hasAudioToSave() const override { return table != nullptr; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        rate = 48000.0;
        const juce::SpinLock::ScopedLockType sl (tableLock);
        return table;
    }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double) override
    {
        const bool use = useTable.load();
        setWavetableData (std::move (audio), tablePath);
        useTable = use || table != nullptr;
    }

private:
    void chooseWavetable()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a wavetable (N x 2048 samples)", juce::File(), "*.wav");
        juce::WeakReference<SynthModule> weak (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [weak] (const juce::FileChooser& fc)
            {
                auto* self = dynamic_cast<FlowEQCurveModule*> (weak.get());
                if (self == nullptr)
                    return;
               #if JUCE_ANDROID
                const auto url = fc.getURLResult();
                if (url.isEmpty())
                    return;
                auto doc = juce::AndroidDocument::fromDocument (url);
                if (! doc.hasValue())
                    return;
                const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (doc.getInfo().getName());
                {
                    auto in = doc.createInputStream();
                    juce::FileOutputStream o (tmp);
                    if (in == nullptr || ! o.openedOk())
                        return;
                    o.writeFromInputStream (*in, -1);
                }
                self->setWavetable (tmp);
               #else
                const auto f = fc.getResult();
                if (f.existsAsFile())
                    self->setWavetable (f);
               #endif
            });
    }

    PointCurve curve { 128, 0.0f, 1.0f, 0.5f };
    double phase { 0.0 };
    std::atomic<float> livePhase { 0.0f };
    int iTime { 0 }, iSync { 0 }, iDiv { 0 }, iFrame { 0 };

    mutable juce::SpinLock tableLock;
    std::shared_ptr<const juce::AudioBuffer<float>> table;
    std::atomic<bool> useTable { false };
    juce::String tablePath;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// FlowEQ - Bell
//==============================================================================
class FlowEQBellModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.floweq.bell";
        d.displayName = kit::moduleName (kPlugin, "Bell");
        d.description = "One FlowEQ band: a bell filter whose Freq, Gain and Q Curve inputs take FlowEQ Flow "
                        "Curves (or anything 0..1). Freq Curve sets the frequency outright (20 Hz - 20 kHz, log), "
                        "Gain Curve adds +-24 dB to Gain, Q Curve multiplies Q by up to 4 either way.";
        d.section = ModuleSection::Filter;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = {
            audioIn ("audioIn", "Audio In"),
            modIn ("freqCurve", "Freq Curve"),
            modIn ("gainCurve", "Gain Curve"),
            modIn ("qCurve", "Q Curve"),
            audioOut ("audioOut", "Audio Out")
        };
        d.params = {
            makeRotary ("freq", "Freq", kMinFreq, kMaxFreq, 1000.0f, 0, "Hz", true),
            makeRotary ("gain", "Gain", -kMaxGainDb, kMaxGainDb, 0.0f, 0, "dB"),
            makeRotary ("q",    "Q",    kMinQ, kMaxQ, 0.707f, 0, {}, true)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (auto& s : st) s = {};
        for (auto& s : smoothers) s.initialised = false;
        counter = 0;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (counter-- <= 0)
        {
            counter = kControlBlock - 1;

            const float baseFreq = param (0), baseGain = param (1), baseQ = param (2);
            const float rawFreq = isInputConnected (1)
                ? kMinFreq * std::pow (kMaxFreq / kMinFreq, juce::jlimit (0.0f, 1.0f, in[1][0]))
                : baseFreq;
            const float rawGain = juce::jlimit (-kMaxGainDb, kMaxGainDb,
                baseGain + (isInputConnected (2) ? (juce::jlimit (0.0f, 1.0f, in[2][0]) - 0.5f) * 2.0f * kMaxGainDb : 0.0f));
            const float rawQ = juce::jlimit (kMinQ, kMaxQ,
                baseQ * (isInputConnected (3) ? std::pow (2.0f, (juce::jlimit (0.0f, 1.0f, in[3][0]) - 0.5f) * 4.0f) : 1.0f));

            const float alpha = 1.0f - std::exp (-((float) kControlBlock / (float) sampleRate) / 0.012f);
            const float f = juce::jlimit (kMinFreq, (float) (sampleRate * 0.45), smoothers[0].process (rawFreq, alpha));
            const float g = smoothers[1].process (rawGain, alpha);
            const float q = juce::jlimit (kMinQ, kMaxQ, smoothers[2].process (rawQ, alpha));

            // juce::dsp::IIR::Coefficients::makePeakFilter, as the plugin
            const double A = std::sqrt ((double) juce::Decibels::decibelsToGain (g));
            const double omega = juce::MathConstants<double>::twoPi * f / sampleRate;
            const double a = std::sin (omega) / (q * 2.0);
            const double c2 = -2.0 * std::cos (omega);
            const double a0 = 1.0 + a / A;
            b0 = (float) ((1.0 + a * A) / a0); b1 = (float) (c2 / a0); b2 = (float) ((1.0 - a * A) / a0);
            a1 = (float) (c2 / a0); a2 = (float) ((1.0 - a / A) / a0);
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& s = st[(size_t) ch];
            const float x = in[0][(size_t) ch];
            const float y = b0 * x + s.s1;
            s.s1 = b1 * x - a1 * y + s.s2;
            s.s2 = b2 * x - a2 * y;
            out[0][(size_t) ch] = y;
        }
    }

private:
    struct State { float s1 { 0 }, s2 { 0 }; };
    struct Smoother
    {
        float value { 0 }; bool initialised { false };
        float process (float target, float alpha)
        {
            if (! initialised) { value = target; initialised = true; return value; }
            value += (target - value) * alpha;
            return value;
        }
    };

    std::array<State, 2> st {};
    std::array<Smoother, 3> smoothers {};
    float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 };
    int counter { 0 };
};

AQUANODE_REGISTER_MODULE (FlowEQBellModule,  FlowEQBellModule::descriptor)
AQUANODE_REGISTER_MODULE (FlowEQCurveModule, FlowEQCurveModule::descriptor)

//==============================================================================
namespace aquanode::plugins::floweq
{
    // FlowEQ's drawn curves and wavetables are not parameters: read them from
    // its state, the same XML its own setStateInformation reads
    struct CurveInfo
    {
        juce::String points;      // base64, 128 floats
        bool useWavetable { false };
        int frame { 0 };
        juce::String path;
    };

    static std::map<juce::String, CurveInfo> readCurves (juce::AudioProcessor& original)
    {
        std::map<juce::String, CurveInfo> out;
        juce::MemoryBlock mb;
        original.getStateInformation (mb);
        std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()));
        if (xml == nullptr)
            return out;

        if (auto* c = xml->getChildByName ("CURVES"))
            for (int i = 0; i < c->getNumAttributes(); ++i)
                out[c->getAttributeName (i)].points = c->getAttributeValue (i);

        if (auto* w = xml->getChildByName ("WAVETABLES"))
            for (auto& kv : out)
            {
                kv.second.useWavetable = w->getBoolAttribute (kv.first + "_use", false);
                kv.second.frame = w->getIntAttribute (kv.first + "_frame", 0);
                kv.second.path = w->getStringAttribute (kv.first + "_path");
            }
        return out;
    }

    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const auto curves = readCurves (original);
        static const char* targets[3] = { "freq", "gain", "q" };
        static const char* inputs[3] = { "freqCurve", "gainCurve", "qCurve" };

        PatchPorts ports;
        ports.name = "FlowEQ";
        int previous = -1;
        int curveRow = 0;

        for (int band = 0; band < 3; ++band)
        {
            const auto bandKey = "f" + juce::String (band);
            const int bell = b.add ("plugin.floweq.bell", 320 + band * 300, 0);
            b.set (bell, "freq", kit::param (original, bandKey + "_freq", 1000.0f));
            b.set (bell, "gain", kit::param (original, bandKey + "_gain", 0.0f));
            b.set (bell, "q",    kit::param (original, bandKey + "_q", 0.707f));

            if (previous < 0) ports.audioIn = { { bell, "audioIn" } };
            else              b.cable (previous, "audioOut", bell, "audioIn");
            previous = bell;

            for (int t = 0; t < 3; ++t)
            {
                const auto key = bandKey + "_" + targets[t];
                auto it = curves.find (key);
                if (it == curves.end())
                    continue;

                PointCurve probe (128, 0.0f, 1.0f, 0.5f);
                probe.fromBase64 (it->second.points);
                const bool drawn = ! probe.isFlatAt (0.5f);
                const bool wavetable = it->second.useWavetable && it->second.path.isNotEmpty();
                if (! drawn && ! wavetable)
                    continue;   // FlowEQ ignores an undrawn curve: so does the patch

                const int c = b.add ("plugin.floweq.curve", 320 + band * 300, 260 + (curveRow++ % 3) * 260);
                b.set (c, "time", kit::param (original, key + "_time", 2.0f));
                b.set (c, "sync", kit::param (original, key + "_sync", 0.0f));
                b.set (c, "syncdiv", kit::param (original, key + "_syncdiv", 6.0f));
                b.set (c, "frame", (float) it->second.frame);
                b.setCustomState (c, it->second.points + "|" + (wavetable ? "1" : "0") + "|" + it->second.path);
                b.cable (c, "curveOut", bell, inputs[t]);
            }
            curveRow = 0;
        }

        ports.audioOut = { { previous, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
