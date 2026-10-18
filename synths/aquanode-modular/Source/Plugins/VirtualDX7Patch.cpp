//==============================================================================
// VirtualDX7 - Patch version
//
// VirtualDX7 has two engines. The bit-accurate one runs Yamaha's own firmware
// on an emulated HD6303 CPU - one program driving the EGS and OPS chips, which
// cannot be taken apart into modules (it plays in the GUI version, once you
// load your ROM). The patch version is VirtualDX7's native engine, taken apart
// along the DX7's own lines:
//
//   VirtualDX7 - Chord (MIDI: the Chord section's six added intervals and its
//   sustain hold) -> six DX Operators wired to the voice's algorithm (they
//   run the native engine's operator code), DX Pitch EG, DX LFO,
//   VirtualDX7 - Function (the FUNCTION page: bend range and steps, master
//   tune, portamento with its hold and glissando, and the four controllers -
//   wheel, foot, breath, aftertouch - on pitch, amp and EG bias)
//   -> Chorus -> Delay -> Phaser -> Reverb (VirtualDX7's FX) ->
//   master volume.
//
// The DX7 SysEx loading works in both modes: a .syx bank loads into the
// plugin first, and the selected voice is what gets built.
//==============================================================================
#include "PatchKit.h"
#include "ChorusVerbPatch.h"
#include "../Modules/DXNativeFM.h"
#include "../Modules/DXOperatorModule.h"

namespace aqp_vdx7patch
{
    #include "../PluginSources/virtual-dx7/Source/FxChain.h"
}

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "virtualdx7" };
    namespace fx = aqp_vdx7patch::vdx7fx;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.virtualdx7." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    juce::StringArray offOn() { return { "Off", "On" }; }

    // the native engine's operators run at kOpFullScale where a DX Operator
    // runs at its own kOutScale: the summed carriers are brought to the
    // native engine's level by their ratio (checked against the GUI version)
    constexpr float kOutputTrim = dxnative::kOpFullScale / DXOperatorModule::kOutScale;
}

//==============================================================================
// VirtualDX7 - Function
//==============================================================================
class VDX7FunctionModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("function", "Function", ModuleSection::Utility, 1);
        d.description = "VirtualDX7's FUNCTION page for the DX modules: pitch bend (range, or steps), master "
                        "tune and portamento (DX7 time law, retain/follow or fingered/full time, glissando) on "
                        "Pitch Out, for every operator's Pitch In; and the wheel, foot (CC 4), breath (CC 2) "
                        "and aftertouch, each with a range and its PITCH / AMP / EG BIAS assignments - into "
                        "the DX LFO's Wheel, Amp and Hold inputs. Bend comes from here: the patch's Midi "
                        "Controls has its own bend range at 0.";
        d.sockets = { midiIn ("addMidiIn", "Add Midi In"),
                      modOut ("pitchOut", "Pitch Out"), modOut ("pitchDepth", "LFO Depth"),
                      modOut ("ampBias", "Amp Bias"), modOut ("egHold", "EG Hold") };
        d.params = {
            makeRotary ("fnPbRange", "Bend", 0.0f, 12.0f, 2.0f, 0, "st", false, 1.0f),
            makeRotary ("fnPbStep", "Bend Step", 0.0f, 12.0f, 0.0f, 0, {}, false, 1.0f),
            makeRotary ("fnMasterTune", "Tune", -256.0f, 255.0f, 0.0f, 0, {}, false, 1.0f),
            makeCombo ("fnPolyMono", "Poly/Mono", { "Poly", "Mono" }, 0, 1, 2).noMod(),
            makeCombo ("fnPortaMode", "Porta", { "Retain/Fingered", "Follow/Full" }, 0, 1, 2),
            makeRotary ("fnPortaTime", "Porta Time", 0.0f, 99.0f, 0.0f, 1, {}, false, 1.0f),
            makeCombo ("fnPortaGliss", "Gliss", offOn(), 0, 2, 1)
        };
        const char* names[4] = { "Wheel", "Foot", "Breath", "Touch" };
        const char* ids[4] = { "fnMw", "fnFc", "fnBc", "fnAt" };
        for (int c = 0; c < 4; ++c)
        {
            const juce::String id (ids[c]);
            d.params.push_back (makeRotary (id + "Range", juce::String (names[c]) + " Rng", 0.0f, 99.0f, 99.0f, 3 + c, {}, false, 1.0f));
            d.params.push_back (makeCombo (id + "Pitch", "Pitch", offOn(), c == 0 ? 1 : 0, 3 + c, 1));
            d.params.push_back (makeCombo (id + "Amp", "Amp", offOn(), 0, 3 + c, 1));
            d.params.push_back (makeCombo (id + "EgBias", "EG Bias", offOn(), 0, 3 + c, 1));
        }
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& vs = voices[(size_t) v];
        float tau = 0.0f, delay = 0.0f;
        dxnative::portamentoTimeToGlide ((int) std::lround (param (5)), tau, delay);
        const bool glide = tau > 0.0f;
        const bool mono = param (3) >= 0.5f, follow = param (4) >= 0.5f;

        // where the glide starts: poly glides from the last note; mono glides
        // between held keys (fingered) or always from the last note (full time)
        float from = (float) note;
        if (glide && lastNote >= 0.0f)
        {
            if (! mono || follow || held > 0)
                from = lastNote;
        }
        vs.note = (float) note;
        vs.glideNote = from;
        vs.hold = -1.0f;
        lastNote = (float) note;
        ++held;
    }
    void voiceNoteOff (int) override { held = juce::jmax (0, held - 1); }
    void reset() override { held = 0; lastNote = -1.0f; }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        auto& vs = voices[(size_t) v];
        const float dt = (float) (1.0 / sampleRate);

        float tau = 0.0f, delay = 0.0f;
        dxnative::portamentoTimeToGlide ((int) std::lround (param (5)), tau, delay);
        if (tau > 0.0f && vs.glideNote != vs.note)
        {
            if (vs.hold < 0.0f) vs.hold = delay;
            if (vs.hold > 0.0f) vs.hold = juce::jmax (0.0f, vs.hold - dt);
            else
            {
                vs.glideNote += (vs.note - vs.glideNote) * (1.0f - std::exp (-dt / tau));
                if (std::abs (vs.note - vs.glideNote) < 0.001f) vs.glideNote = vs.note;
            }
        }
        else
            vs.glideNote = vs.note;

        const float heard = param (6) >= 0.5f ? std::round (vs.glideNote) : vs.glideNote;

        // bend: smooth over the range, or in steps (the range is then an octave)
        dxnative::Functions fn;
        fn.bendRange = std::round (param (0));
        fn.bendStep = (int) std::lround (param (1));
        const float bend = midiControls != nullptr ? midiControls->bend : 0.0f;
        const float bendSemis = dxnative::bendSemitones (bend, fn);
        const float tuneSemis = std::round (param (2)) * (1200.0f / 4096.0f) / 100.0f;

        out[0] = { (heard - vs.note + bendSemis + tuneSemis) / 60.0f, 0.0f };   // DX Pitch In units

        // the controllers, as the native engine sums them
        float ctl[4] = {};
        if (midiControls != nullptr)
        {
            ctl[0] = midiControls->modWheel;
            ctl[1] = midiControls->cc[4];
            ctl[2] = midiControls->cc[2];
            ctl[3] = midiControls->aftertouch;
        }
        float pitchCtl = 0.0f, ampCtl = 0.0f, holdCtl = 0.0f;
        for (int c = 0; c < 4; ++c)
        {
            const float range = param (7 + c * 4) / 99.0f;
            if (param (8 + c * 4) >= 0.5f)  pitchCtl += ctl[c] * range;
            if (param (9 + c * 4) >= 0.5f)  ampCtl   += ctl[c] * range;
            if (param (10 + c * 4) >= 0.5f) holdCtl  += range * (1.0f - ctl[c]);
        }
        out[1] = { juce::jlimit (0.0f, 1.0f, pitchCtl), 0.0f };
        out[2] = { juce::jlimit (0.0f, 1.0f, ampCtl), 0.0f };
        out[3] = { juce::jlimit (0.0f, 1.0f, holdCtl), 0.0f };
    }

private:
    struct Voice { float note { 60.0f }, glideNote { 60.0f }, hold { -1.0f }; };
    std::array<Voice, kMaxVoices> voices {};
    float lastNote { -1.0f };
    int held { 0 };
};

//==============================================================================
// VirtualDX7 - Chord
//==============================================================================
class VDX7ChordModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("chord", "Chord", ModuleSection::Utility, 0);
        d.description = "VirtualDX7's Chord section on the MIDI before the operators: up to six intervals added "
                        "to every note (moving one re-strikes it on the held notes), and Sustain, which holds "
                        "every note until it is switched off.";
        d.sockets = { midiIn ("midiIn", "Midi In"), midiOut ("midiOut", "Midi Out") };
        d.params = { makeCombo ("fxChordOn", "Chord", offOn(), 0, 0, 1),
                     makeCombo ("fxChordSustain", "Sustain", offOn(), 0, 0, 1) };
        for (int i = 1; i <= 6; ++i)
            d.params.push_back (makeRotary ("fxChordNote" + juce::String (i), "Note " + juce::String (i),
                                            -36.0f, 36.0f, 0.0f, 1 + (i - 1) / 3, "st", false, 1.0f));
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiProcessor() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }

    void prepare (double sr) override { SynthModule::prepare (sr); pending.ensureSize (1024); }
    void reset() override { chord.reset(); sustain.reset(); pending.clear(); }

    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent* out, int maxOut) override
    {
        updateSettings();
        const auto msg = in.isOn ? juce::MidiMessage::noteOn (1, in.note, juce::jlimit (0.0f, 1.0f, in.velocity))
                                 : juce::MidiMessage::noteOff (1, in.note);
        int n = 0;
        if (! sustain.filterMessage (msg))
            return 0;
        if (n < maxOut) out[n++] = in;
        juce::MidiBuffer extra;
        chord.processMessage (msg, extra, 0);
        for (const auto meta : extra)
            if (n < maxOut) n += toEvent (meta.getMessage(), out[n]);
        return n;
    }

    void midiAllNotesOff() override { chord.reset(); sustain.reset(); }

    int midiDriverAdvanceEvents (double, MidiDriverEvent* out, int maxOut) override
    {
        if (++counter < 32)
            return 0;
        counter = 0;
        updateSettings();
        int n = 0;
        for (const auto meta : pending)
            if (n < maxOut) n += toEvent (meta.getMessage(), out[n]);
        pending.clear();
        return n;
    }

private:
    static int toEvent (const juce::MidiMessage& m, MidiDriverEvent& e)
    {
        if (! m.isNoteOnOrOff()) return 0;
        e.note = m.getNoteNumber();
        e.isOn = m.isNoteOn();
        e.velocity = m.getFloatVelocity();
        return 1;
    }

    // switch / knob changes re-strike or release notes, as in the plugin
    void updateSettings()
    {
        sustain.setEnabled (param (1) >= 0.5f, pending, 0);
        int offsets[fx::Harmonizer::kNumSlots];
        for (int i = 0; i < fx::Harmonizer::kNumSlots; ++i)
            offsets[i] = (int) std::lround (param (2 + i));
        chord.setEnabled (param (0) >= 0.5f, pending, 0);
        chord.updateOffsets (offsets, pending, 0);
    }

    fx::Harmonizer chord;
    fx::SustainHold sustain;
    juce::MidiBuffer pending;
    int counter { 0 };
};

AQUANODE_REGISTER_MODULE (VDX7FunctionModule, VDX7FunctionModule::descriptor)
AQUANODE_REGISTER_MODULE (VDX7ChordModule,    VDX7ChordModule::descriptor)

//==============================================================================
namespace aquanode::plugins::virtualdx7
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        auto V = [&] (int offset) { return (int) std::lround (kit::param (original, "p" + juce::String (offset), 0.0f)); };
        auto P = [&] (const char* id, float def) { return kit::param (original, id, def); };

        const int algorithm = juce::jlimit (0, 31, V (134));
        const auto& alg = dxnative::kAlgorithms[algorithm];
        const int fb = juce::jlimit (0, 7, V (135));
        const bool mono = P ("fnPolyMono", 0.0f) >= 0.5f;

        // --- the MIDI front: the Chord section, when the voice uses it (it
        // then plays the operators and the Function module; without it they
        // hear the keys themselves)
        bool useChord = P ("fxChordOn", 0.0f) >= 0.5f || P ("fxChordSustain", 0.0f) >= 0.5f;
        const int chord = useChord ? b.add ("plugin.virtualdx7.chord", 0, 0) : -1;
        if (chord >= 0)
            b.copy (chord, original);
        useChord = chord >= 0;

        // --- the six operators, laid out like the DX7's algorithm chart ------
        std::array<int, 6> op {};
        for (int i = 0; i < 6; ++i)
        {
            const int id = b.add ("osc.dxop", 600 + (i % 3) * 290, (i / 3) * 420);
            op[(size_t) i] = id;
            const int o = (5 - i) * 21;                 // VCED runs OP6 .. OP1
            for (int k = 0; k < 4; ++k)
            {
                b.set (id, "r" + juce::String (k + 1), (float) V (o + k));
                b.set (id, "l" + juce::String (k + 1), (float) V (o + 4 + k));
            }
            b.set (id, "breakPoint", (float) V (o + 8));
            b.set (id, "leftDepth",  (float) V (o + 9));
            b.set (id, "rightDepth", (float) V (o + 10));
            b.set (id, "leftCurve",  (float) (V (o + 11) & 3));
            b.set (id, "rightCurve", (float) (V (o + 12) & 3));
            b.set (id, "rateScale",  (float) (V (o + 13) & 7));
            b.set (id, "ams",        (float) (V (o + 14) & 3));
            b.set (id, "velSens",    (float) (V (o + 15) & 7));
            b.set (id, "level",      (float) V (o + 16));
            b.set (id, "mode",       (float) (V (o + 17) & 1));
            b.set (id, "coarse",     (float) (V (o + 18) & 31));
            b.set (id, "fine",       (float) V (o + 19));
            b.set (id, "detune",     (float) juce::jlimit (0, 14, V (o + 20)));
            b.set (id, "keySync",    V (136) != 0 ? 1.0f : 0.0f);
            b.set (id, "opNum",      (float) (i + 1));
            b.set (id, "voices",     mono ? 1.0f : (float) kMaxVoices);
            const bool carrier = (alg.carrierMask & (1u << i)) != 0;
            b.set (id, "com", carrier ? (float) (alg.numCarriers - 1) : 0.0f);
            if (useChord)
                b.cable (chord, "midiOut", id, "addMidiIn");
        }
        for (int dst = 0; dst < 6; ++dst)
            for (int src = 0; src < 6; ++src)
                if (alg.modMask[dst] & (1u << src))
                    b.cable (op[(size_t) src], "audioOut", op[(size_t) dst], "fmIn");
        if (fb > 0)
        {
            b.set (op[(size_t) alg.feedbackDst], "feedback", (float) fb);
            if (alg.feedbackSrc != alg.feedbackDst)
                b.cable (op[(size_t) alg.feedbackSrc], "audioOut", op[(size_t) alg.feedbackDst], "fbIn");
        }

        // --- pitch EG, LFO, FUNCTION ------------------------------------------
        const int peg = b.add ("util.dxpitcheg", 300, 0);
        for (int k = 0; k < 4; ++k)
        {
            b.set (peg, "r" + juce::String (k + 1), (float) V (126 + k));
            b.set (peg, "l" + juce::String (k + 1), (float) V (130 + k));
        }
        b.set (peg, "transpose", (float) (V (144) - 24));

        const int lfo = b.add ("util.dxlfo", 300, 330);
        b.set (lfo, "speed", (float) V (137));
        b.set (lfo, "delay", (float) V (138));
        b.set (lfo, "pmd",   (float) V (139));
        b.set (lfo, "amd",   (float) V (140));
        b.set (lfo, "keySync", V (141) != 0 ? 1.0f : 0.0f);
        b.set (lfo, "wave",  (float) juce::jlimit (0, 5, V (142)));
        b.set (lfo, "pms",   (float) juce::jlimit (0, 7, V (143)));

        const int fn = b.add ("plugin.virtualdx7.function", 0, 330);
        b.copy (fn, original);
        b.cable (fn, "pitchDepth", lfo, "wheelIn");
        b.cable (fn, "ampBias", lfo, "ampIn");
        b.cable (fn, "egHold", lfo, "holdIn");
        if (useChord)
            b.cable (chord, "midiOut", fn, "addMidiIn");

        // bend comes from the Function module
        const int controls = b.add ("io.midicontrols", 0, 800);
        b.set (controls, "bendRange", 0.0f);

        for (int i = 0; i < 6; ++i)
        {
            b.cable (peg, "pitchOut", op[(size_t) i], "pitchIn");
            b.cable (lfo, "pitchOut", op[(size_t) i], "pitchIn");
            b.cable (fn,  "pitchOut", op[(size_t) i], "pitchIn");
            b.cable (lfo, "amOut",    op[(size_t) i], "amIn");
        }

        // --- the carriers summed (plainly, as the native engine sums its
        // notes - no chord compensation), VirtualDX7's effects, master volume
        const auto chain = chorusverb::buildChain (b, original, 1500, 500, [] (const juce::String& id)
        {
            return "fx" + id.substring (0, 1).toUpperCase() + id.substring (1);
        });
        for (int i = 0; i < 6; ++i)
            if (alg.carrierMask & (1u << i))
                b.cable (op[(size_t) i], "audioOut", chain.chorus, "audioIn");

        const int vol = b.add ("util.volume", 2700, 500);
        b.set (vol, "level", juce::Decibels::decibelsToGain (P ("fnMasterVolume", 0.0f)) * kOutputTrim);
        b.cable (chain.reverb, "audioOut", vol, "audioIn");

        PatchPorts ports;
        ports.name = "VirtualDX7";
        ports.audioOut = { { vol, "audioOut" } };
        if (useChord)
            ports.midiIn = { { chord, "midiIn" } };
        return ports;
    }
}

namespace aquanode::plugins::virtualdx7
{
    bool loadSysex (juce::AudioProcessor& p, const juce::File& file);   // PluginVirtualDX7Access.cpp
    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadSysex);
}
