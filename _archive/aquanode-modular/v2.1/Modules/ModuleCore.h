#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aquanode
{

//==============================================================================
// Enums / small types
//==============================================================================
enum class ParamType { Rotary, RotarySteppedList, Combo, Button };
enum class SocketKind { Audio, Modulation, Midi };
enum class SocketDirection { Input, Output };
enum class ModuleSection { InputOutput = 0, Oscillator, Filter, Effect, Utility };

inline const char* sectionName (ModuleSection s)
{
    switch (s)
    {
        case ModuleSection::InputOutput: return "Input / Output";
        case ModuleSection::Oscillator:  return "Sound Generation";
        case ModuleSection::Filter:      return "Filter";
        case ModuleSection::Effect:      return "Effect";
        case ModuleSection::Utility:     return "Utility";
    }
    return "";
}

inline juce::Colour sectionColour (ModuleSection s)
{
    switch (s)
    {
        case ModuleSection::InputOutput: return juce::Colour (0xffd9c34a); // yellow
        case ModuleSection::Oscillator:  return juce::Colour (0xff4a86d9); // blue
        case ModuleSection::Filter:      return juce::Colour (0xff53b06a); // green
        case ModuleSection::Effect:      return juce::Colour (0xff53b06a); // green (shared with Filter)
        case ModuleSection::Utility:     return juce::Colour (0xffd970b0); // pink
    }
    return juce::Colours::grey;
}

// Fixed sidebar section order
inline const std::array<ModuleSection, 5>& allSections()
{
    static const std::array<ModuleSection, 5> s {
        ModuleSection::InputOutput, ModuleSection::Oscillator,
        ModuleSection::Filter, ModuleSection::Effect, ModuleSection::Utility };
    return s;
}

//==============================================================================
// ParamSpec / SocketSpec / ModuleDescriptor
//==============================================================================
struct ParamSpec
{
    juce::String id;
    juce::String label;
    ParamType type { ParamType::Rotary };
    float minValue { 0.0f }, maxValue { 1.0f }, defaultValue { 0.0f };
    bool logarithmic { false };
    float interval { 0.0f };            // slider step (0 = continuous)
    juce::StringArray choices;          // for Combo / RotarySteppedList
    int widthUnits { 1 };               // 1 = one knob slot, 2 = wide (combo)
    int row { 0 };                      // explicit row index inside the module
    juce::String unitSuffix;            // "Hz", "%", "ms", "s"
    juce::String visibleWhenParamId;    // empty = always visible
    float visibleWhenEquals { 0.0f };
    bool hidden { false };              // stored/saved normally, but NOT rendered
                                        // as a generic control (custom UI owns it)

    ParamSpec& visibleWhen (const juce::String& paramId, float equals)
    {
        visibleWhenParamId = paramId;
        visibleWhenEquals = equals;
        return *this;
    }

    ParamSpec& hide() { hidden = true; return *this; }

    bool modulatable { true };          // false = knob refuses param-mod cables
                                        // (e.g. params that trigger cache rebuilds)
    ParamSpec& noMod() { modulatable = false; return *this; }

    // a hidden parameter that still accepts param-mod cables through the
    // module's custom extra-content UI (e.g. Pitch Lock Filter's note keys).
    // The extra-content component must implement CustomParamCableTargets so
    // the editor knows where cables should attach.
    bool hiddenCableTarget { false };
    ParamSpec& cableTargetInCustomUI() { hiddenCableTarget = true; return *this; }
};

inline ParamSpec makeRotary (juce::String id, juce::String label,
                             float mn, float mx, float def, int row = 0,
                             juce::String suffix = {}, bool log = false, float interval = 0.0f)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::Rotary;
    p.minValue = mn; p.maxValue = mx; p.defaultValue = def;
    p.logarithmic = log; p.interval = interval;
    p.row = row; p.unitSuffix = std::move (suffix);
    return p;
}

inline ParamSpec makeCombo (juce::String id, juce::String label,
                            juce::StringArray choices, int defaultIndex,
                            int row = 0, int widthUnits = 2)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::Combo;
    p.choices = std::move (choices);
    p.minValue = 0.0f; p.maxValue = (float) (p.choices.size() - 1);
    p.defaultValue = (float) defaultIndex;
    p.widthUnits = widthUnits; p.row = row;
    return p;
}

inline ParamSpec makeSteppedList (juce::String id, juce::String label,
                                  juce::StringArray choices, int defaultIndex, int row = 0)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::RotarySteppedList;
    p.choices = std::move (choices);
    p.minValue = 0.0f; p.maxValue = (float) (p.choices.size() - 1);
    p.defaultValue = (float) defaultIndex;
    p.interval = 1.0f; p.row = row;
    return p;
}

inline ParamSpec makeButton (juce::String id, juce::String label, int row = 0, int widthUnits = 1)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::Button;
    p.widthUnits = widthUnits; p.row = row;
    return p;
}

//==============================================================================
// Implemented by extra-content components (createExtraContentComponent) that
// expose their OWN drop targets for knob-modulation cables - e.g. the Pitch
// Lock Filter's keyboard, where each key is a hidden nc0..nc11 parameter.
// All coordinates are local to the extra-content component.
//==============================================================================
struct CustomParamCableTargets
{
    virtual ~CustomParamCableTargets() = default;

    // paramId of the target under localPos, or {} if none
    virtual juce::String paramTargetAt (juce::Point<int> localPos) const = 0;

    // where a cable to this param should visually attach (local coords)
    virtual juce::Point<int> paramTargetCentre (const juce::String& paramId) const = 0;
};

struct SocketSpec
{
    juce::String id;
    juce::String label;
    SocketKind kind { SocketKind::Audio };
    SocketDirection direction { SocketDirection::Input };
};

inline SocketSpec audioIn  (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Audio,      SocketDirection::Input  }; }
inline SocketSpec audioOut (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Audio,      SocketDirection::Output }; }
inline SocketSpec modIn    (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Modulation, SocketDirection::Input  }; }
inline SocketSpec midiIn   (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Midi,       SocketDirection::Input  }; }
inline SocketSpec midiOut  (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Midi,       SocketDirection::Output }; }
inline SocketSpec modOut   (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Modulation, SocketDirection::Output }; }

struct ModuleDescriptor
{
    juce::String typeId;        // e.g. "osc.basic"
    juce::String displayName;
    ModuleSection section { ModuleSection::Utility };
    int sidebarOrder { 0 };     // explicit position inside its section
    std::vector<SocketSpec> sockets;
    std::vector<ParamSpec> params;

    std::vector<const SocketSpec*> inputs() const
    {
        std::vector<const SocketSpec*> v;
        for (auto& s : sockets) if (s.direction == SocketDirection::Input) v.push_back (&s);
        return v;
    }

    std::vector<const SocketSpec*> outputs() const
    {
        std::vector<const SocketSpec*> v;
        for (auto& s : sockets) if (s.direction == SocketDirection::Output) v.push_back (&s);
        return v;
    }

    int numInputs()  const { return (int) inputs().size(); }
    int numOutputs() const { return (int) outputs().size(); }

    int inputIndexOf (const juce::String& socketId) const
    {
        auto v = inputs();
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i]->id == socketId) return i;
        return -1;
    }

    int outputIndexOf (const juce::String& socketId) const
    {
        auto v = outputs();
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i]->id == socketId) return i;
        return -1;
    }

    const ParamSpec* findParam (const juce::String& id) const
    {
        for (auto& p : params) if (p.id == id) return &p;
        return nullptr;
    }
};

//==============================================================================
// Misc DSP helpers
//==============================================================================
inline double midiNoteToHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }
inline double midiNoteToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

// "C-1" ... "G9" for MIDI 0..127 (middle C = C4 = 60)
inline juce::String midiNoteName (int note)
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    note = juce::jlimit (0, 127, note);
    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}

inline const juce::StringArray& midiNoteNameChoices()
{
    static const juce::StringArray choices = []
    {
        juce::StringArray a;
        for (int n = 0; n < 128; ++n)
            a.add (midiNoteName (n));
        return a;
    }();
    return choices;
}


static constexpr int kMaxVoices = 24;

//==============================================================================
// Standard amplitude handling for a pitched generator, matching Oscillator's
// convention exactly: when Env In is patched the envelope IS the amplitude;
// when it is not, a simple on/off gate with a ~3 ms anti-click ramp stands in.
// Note velocity always multiplies on top.
//==============================================================================
class ModuleVoiceGate
{
public:
    static constexpr double kRampSeconds = 0.003;

    ModuleVoiceGate() { resetAll(); }

    void resetAll()
    {
        for (int v = 0; v < kMaxVoices; ++v)
            resetVoice (v);
    }

    void resetVoice (int v) { level[v] = 0.0f; on[v] = false; vel[v] = 1.0f; }

    void noteOn (int v)  { on[v] = true; }
    void noteOff (int v) { on[v] = false; }
    void setVelocity (int v, float velocity01) { vel[v] = velocity01; }

    float next (int v, bool envConnected, float envIn, double sampleRate)
    {
        if (envConnected)
            return envIn * vel[v];

        const float step = (float) (1.0 / (kRampSeconds * sampleRate));
        const float target = on[v] ? 1.0f : 0.0f;
        if (level[v] < target)      level[v] = juce::jmin (target, level[v] + step);
        else if (level[v] > target) level[v] = juce::jmax (target, level[v] - step);
        return level[v] * vel[v];
    }

    bool isOn (int v) const { return on[v]; }

private:
    float level[kMaxVoices] {};
    bool on[kMaxVoices] {};
    float vel[kMaxVoices] {};
};

//==============================================================================
// Mono glide (portamento). Only meaningful when a generator's voice limit is
// 1: it tracks the pitch that is actually sounding, so a new note starts from
// wherever the previous one left off and slides to its own target. Works in
// semitone space, which makes the slide musically linear rather than linear
// in Hz. With Glide at 0 the pitch jumps immediately and ModuleVoicePool's
// crossfade between the old and new voice hides the discontinuity.
//==============================================================================
class ModuleGlide
{
public:
    ModuleGlide() { resetAll(); }

    void resetAll()
    {
        for (int v = 0; v < kMaxVoices; ++v)
            current[v] = target[v] = 60.0f;
        liveSemi = 60.0f;
        hasLive = false;
    }

    void resetVoice (int v) { current[v] = target[v]; }

    // from voiceNoteOn: in mono the new voice picks up the sounding pitch
    void noteOn (int v, float semitones, bool mono)
    {
        target[v] = semitones;
        current[v] = (mono && hasLive) ? liveSemi : semitones;
    }

    // once per sample per voice; `sounding` is false for voices fading out
    // (they hold their pitch and must not steer the shared live pitch)
    float next (int v, float glideMs, bool mono, bool sounding, double sampleRate)
    {
        if (! sounding)
            return current[v];

        if (! mono || glideMs < 0.5f)
            current[v] = target[v];
        else
        {
            const float coeff = 1.0f - std::exp ((float) (-1.0 / (glideMs * 0.001 * sampleRate)));
            current[v] += coeff * (target[v] - current[v]);
        }

        if (mono)
        {
            liveSemi = current[v];
            hasLive = true;
        }
        return current[v];
    }

private:
    float current[kMaxVoices] {}, target[kMaxVoices] {};
    float liveSemi { 60.0f };
    bool hasLive { false };
};

//==============================================================================
// Per-generator polyphony limit with round-robin voice stealing: each sound
// generator can cap how many of the engine's global voices it actually sounds
// on. New notes always sound; when the cap is exceeded, the generator's
// OLDEST sounding voice is muted (its lane keeps running, output gated).
//==============================================================================
class ModuleVoicePool
{
public:
    void noteOn (int v, int limit)
    {
        order[v] = ++counter;
        sounding[v] = true;
        recompute (limit);
    }

    // A released voice stops competing for the limit immediately, so a held
    // note that was muted to make room comes back as soon as the newer notes
    // are let go (classic mono/poly note-priority behaviour). Its own release
    // tail keeps ringing meanwhile.
    void noteOff (int v, int limit)
    {
        sounding[v] = false;
        recompute (limit);
    }

    void resetVoice (int v)
    {
        sounding[v] = false;
        muted[v] = false;
        gain[v] = 1.0f;     // a fresh voice starts audible; its envelope shapes the attack
        recompute (lastLimit);
    }

    void resetAll()
    {
        for (int v = 0; v < kMaxVoices; ++v)
            resetVoice (v);
        counter = 0;
    }

    ModuleVoicePool() { resetAll(); }

    bool isMuted (int v) const { return muted[v]; }

    // Per-voice fade, advanced once per sample by the generator. Voice
    // stealing used to cut the stolen voice dead, which clicks; the muted
    // voice now ramps out over kFadeSeconds instead (and a re-used voice
    // ramps back in), so exceeding the voice limit is silent.
    float nextGain (int v, double sampleRate)
    {
        const float target = muted[v] ? 0.0f : 1.0f;
        const float step = (float) (1.0 / (kFadeSeconds * sampleRate));
        if (gain[v] < target)      gain[v] = juce::jmin (target, gain[v] + step);
        else if (gain[v] > target) gain[v] = juce::jmax (target, gain[v] - step);
        return gain[v];
    }

    // true once a muted voice has fully faded: the generator can skip its DSP
    bool isSilent (int v) const { return muted[v] && gain[v] <= 0.0f; }

private:
    void recompute (int limit)
    {
        limit = juce::jlimit (1, kMaxVoices, limit);
        lastLimit = limit;

        int active = 0;
        for (int v = 0; v < kMaxVoices; ++v)
            if (sounding[v])
                ++active;

        // mute the oldest until at most `limit` are audible
        for (int v = 0; v < kMaxVoices; ++v)
            muted[v] = false;

        while (active > limit)
        {
            int oldest = -1;
            juce::uint64 oldestOrder = ~ (juce::uint64) 0;
            for (int v = 0; v < kMaxVoices; ++v)
                if (sounding[v] && ! muted[v] && order[v] < oldestOrder)
                {
                    oldestOrder = order[v];
                    oldest = v;
                }
            if (oldest < 0)
                break;
            muted[oldest] = true;
            --active;
        }
    }

    static constexpr double kFadeSeconds = 0.004;   // 4 ms de-click fade

    juce::uint64 order[kMaxVoices] {};
    juce::uint64 counter { 0 };
    int lastLimit { kMaxVoices };
    bool sounding[kMaxVoices] {};
    bool muted[kMaxVoices] {};
    float gain[kMaxVoices] { };
};

// shared tempo-division table (Clock / Step Seq / Euclid)
inline const juce::StringArray& seqDivisionChoices()
{
    static const juce::StringArray choices
        { "1/1", "1/2", "1/4", "1/4T", "1/4.", "1/8", "1/8T", "1/8.",
          "1/16", "1/16T", "1/16.", "1/32" };
    return choices;
}

inline double seqDivisionBeats (int choice)
{
    static const double beats[12] = { 4.0, 2.0, 1.0, 2.0 / 3.0, 1.5, 0.5, 1.0 / 3.0, 0.75,
                                      0.25, 1.0 / 6.0, 0.375, 0.125 };
    return beats[juce::jlimit (0, 11, choice)];
}

using StereoFrame = std::array<float, 2>;

//==============================================================================
// Global polyphony (Nord Modular G2 style): the WHOLE patch is evaluated once
// per voice. The engine owns a single global voice pool; modules hold
// per-voice state indexed by the engine's voice number.
//==============================================================================

enum class VoiceMode
{
    Global,     // one instance of state, voices already summed at its inputs (effects, IO)
    PerVoice,   // inherently per-voice (oscillators, envelopes, LFOs)
    Flexible    // per-voice when fed by a per-voice source, global otherwise (filters, volume...)
};

//==============================================================================
// SynthModule - per-instance DSP object base class
//==============================================================================
class SynthModule
{
public:
    SynthModule() = default;
    virtual ~SynthModule() = default;

    // called by the factory right after construction
    void initialiseFromDescriptor (const ModuleDescriptor& d)
    {
        descriptor = &d;
        const auto n = d.params.size();
        paramValues.reset (n > 0 ? new std::atomic<float>[n] : nullptr);
        for (size_t i = 0; i < n; ++i)
        {
            paramValues[i].store (d.params[i].defaultValue, std::memory_order_relaxed);
            paramIndexById[d.params[i].id.toStdString()] = (int) i;
        }
        voicesParamIndex = paramIndexById.count ("voices") ? paramIndexById["voices"] : -1;
        glideParamIndex  = paramIndexById.count ("glide")  ? paramIndexById["glide"]  : -1;
        paramMod.assign (n, 0.0f);
        paramMin.resize (n);
        paramMax.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            paramMin[i] = d.params[i].minValue;
            paramMax[i] = d.params[i].maxValue;
        }
        for (auto& f : inputConnectedFlags) f.store (false, std::memory_order_relaxed);
    }

    const ModuleDescriptor& getDescriptor() const { return *descriptor; }

    //=== lifecycle ============================================================
    virtual void prepare (double newSampleRate) { sampleRate = newSampleRate; }
    virtual void reset() {}                            // wipe transient state (voices/filters/delays), no reallocation
    virtual void blockStart() {}                       // once per processBlock (latch buffers etc.)
    virtual void setTempo (double bpm) { tempoBpm = bpm; }

    // Snapshot of the plugin's host-automatable modulation parameters, handed
    // to every module once per block. Only DAW Mod reads it.
    void setHostModValues (const float* values) { hostModValues = values; }

    // Appended to the module's name in its title bar (DAW Mod uses it to show
    // which host parameter slot it owns). Empty for everything else.
    virtual juce::String titleSuffix() const { return {}; }

    // For modules with a Midi Out: false means the notes are ADDED to what the
    // player is holding (Midi Add), true means they REPLACE them for any
    // generator listening (Arp, Always Midi). Everything else is irrelevant.
    virtual bool midiSourceReplacesInput() const { return false; }

    //=== MIDI note drivers ===================================================
    // A module that emits notes on its OWN clock rather than at the moment a
    // key is pressed (Arp, Always Midi). The engine ticks these once per
    // sample and turns what they report into real voices.
    virtual bool isMidiNoteDriver() const { return false; }

    // One sample of the driver's own time. Report at most one note-off and one
    // note-on (-1 for "nothing"); both may land on the same sample.
    virtual void midiDriverAdvance (double sr, int& onNote, int& offNote)
    {
        juce::ignoreUnused (sr);
        onNote = -1;
        offNote = -1;
    }

    // The keys the player is currently holding, for drivers that care (the Arp
    // arpeggiates them; Always Midi ignores them entirely).
    virtual void midiDriverHeldNoteOn (int note)  { juce::ignoreUnused (note); }
    virtual void midiDriverHeldNoteOff (int note) { juce::ignoreUnused (note); }
    virtual void midiDriverAllHeldOff() {}

    //=== voice architecture ===================================================
    // The engine evaluates the whole patch once per active voice (G2-style).
    // Global modules see voice-summed inputs; PerVoice/Flexible modules are
    // called once per voice with that voice's own signals.
    virtual VoiceMode voiceMode() const { return VoiceMode::Global; }

    // Global lane. inputs/outputs indexed by descriptor input/output order.
    // Audio sockets use both channels; Modulation sockets use channel 0 only.
    virtual void processSample (const StereoFrame* inputs, StereoFrame* outputs)
    {
        juce::ignoreUnused (inputs);
        zeroOutputs (outputs);
    }

    // Per-voice lane. Same socket conventions; 'voice' is the engine's global
    // voice index (0..kMaxVoices-1).
    virtual void processVoiceSample (int voice, const StereoFrame* inputs, StereoFrame* outputs)
    {
        juce::ignoreUnused (voice, inputs);
        zeroOutputs (outputs);
    }

    // engine-driven voice lifecycle (PerVoice/Flexible modules)
    virtual void voiceNoteOn (int voice, int note, bool retrigger) { juce::ignoreUnused (voice, note, retrigger); }
    virtual void voiceVelocity (int voice, float velocity01) { juce::ignoreUnused (voice, velocity01); }   // called right after voiceNoteOn
    virtual void voiceNoteOff (int voice) { juce::ignoreUnused (voice); }
    virtual void voiceReset (int voice) { juce::ignoreUnused (voice); }

    // how long this module keeps a voice audible after note-off (e.g. an
    // ADSR's release). The engine keeps the voice alive for the patch's max.
    virtual double voiceTailSeconds() const { return 0.0; }

    //=== MIDI (non-note messages; note events are routed via voiceNoteOn/Off) =
    virtual void handleMidiEvent (const juce::MidiMessage&) {}

    //=== parameters ===========================================================
    float param (int index) const
    {
        const float base = paramValues[(size_t) index].load (std::memory_order_relaxed);
        const float mod = paramMod[(size_t) index];
        return mod == 0.0f ? base
                           : juce::jlimit (paramMin[(size_t) index], paramMax[(size_t) index], base + mod);
    }

    // polyphony limit for sound generators that expose a "voices" knob;
    // index resolved once at init so the audio thread never does a string lookup
    int voiceLimit() const
    {
        return voicesParamIndex >= 0 ? (int) param (voicesParamIndex) : kMaxVoices;
    }

    // glide is a mono-only behaviour: with more than one voice there is no
    // single "previous note" to slide from, so the knob is hidden and ignored
    bool isMonoVoice() const { return voiceLimit() == 1; }

    float glideMillis() const { return glideParamIndex >= 0 ? param (glideParamIndex) : 0.0f; }

    //=== parameter modulation (audio thread only) =============================
    void setParamMod (int index, float value) { paramMod[(size_t) index] = value; }
    void addParamMod (int index, float value) { paramMod[(size_t) index] += value; }
    void clearParamMods() { std::fill (paramMod.begin(), paramMod.end(), 0.0f); }

    int paramIndex (const juce::String& id) const
    {
        auto it = paramIndexById.find (id.toStdString());
        return it == paramIndexById.end() ? -1 : it->second;
    }

    virtual void setParameter (const juce::String& id, float value)
    {
        const int i = paramIndex (id);
        if (i >= 0)
            paramValues[(size_t) i].store (value, std::memory_order_relaxed);
    }

    // NOTE: returns the BASE value, deliberately WITHOUT parameter modulation.
    // The UI, patch serialization, cloning and the mutator all read through
    // here, so a modulated knob must never leak its live offset into them.
    // DSP code wants param(int) instead, which includes modulation.
    float getParameter (const juce::String& id) const
    {
        const int i = paramIndex (id);
        return i >= 0 ? paramValues[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
    }

    float getParameterBase (int index) const
    {
        return paramValues[(size_t) index].load (std::memory_order_relaxed);
    }

    //=== connection info (set by the processor when topology changes) ========
    void setInputConnected (int inputIndex, bool connected)
    {
        if (inputIndex >= 0 && inputIndex < (int) inputConnectedFlags.size())
            inputConnectedFlags[(size_t) inputIndex].store (connected, std::memory_order_relaxed);
    }

    bool isInputConnected (int inputIndex) const
    {
        if (inputIndex >= 0 && inputIndex < (int) inputConnectedFlags.size())
            return inputConnectedFlags[(size_t) inputIndex].load (std::memory_order_relaxed);
        return false;
    }

    //=== host audio IO hooks (Audio In / Audio Out modules only) ==============
    virtual bool wantsHostInput() const { return false; }
    virtual void setHostInput (float, float) {}
    virtual bool providesHostOutput() const { return false; }
    virtual void getHostOutput (float& l, float& r) { l = 0.0f; r = 0.0f; }

    //=== loaded sample support (Oscillator "Sample" mode / Sampler) ===========
    virtual bool usesLoadedSample() const { return false; }

    void setLoadedSample (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double sourceRate)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sampleLock);
            loadedSample = std::move (buffer);
            loadedSampleRate = sourceRate;
        }
        sampleChangeCounter.fetch_add (1, std::memory_order_release);
    }

    std::shared_ptr<const juce::AudioBuffer<float>> getLoadedSample (double* sourceRate = nullptr) const
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        if (sourceRate != nullptr)
            *sourceRate = loadedSampleRate;
        return loadedSample;
    }

    int getSampleChangeCounter() const { return sampleChangeCounter.load (std::memory_order_acquire); }

    // loads any supported audio file into the shared sample buffer (message thread)
    void loadSampleFromFile (const juce::File& file);

    //=== generic UI hooks =====================================================
    // Button params call this from the generic module UI
    virtual void uiButtonClicked (const juce::String& paramId)
    {
        juce::ignoreUnused (paramId);
    }

    // the one non-declarative UI case: Sampler's waveform display
    virtual std::unique_ptr<juce::Component> createExtraContentComponent() { return nullptr; }
    virtual int extraContentHeight() const { return 0; }

protected:
    // opens an async file chooser and loads the chosen file (used by Oscillator/Sampler)
    void openSampleChooser();

    void zeroOutputs (StereoFrame* outputs) const
    {
        const int n = descriptor != nullptr ? juce::jmax (1, descriptor->numOutputs()) : 1;
        for (int i = 0; i < n; ++i)
            outputs[i] = { 0.0f, 0.0f };
    }

    double sampleRate { 44100.0 };
    double tempoBpm { 120.0 };
    const float* hostModValues { nullptr };

private:
    const ModuleDescriptor* descriptor { nullptr };
    std::unique_ptr<std::atomic<float>[]> paramValues;
    std::vector<float> paramMod, paramMin, paramMax;   // paramMod: audio thread only
    int voicesParamIndex { -1 };
    int glideParamIndex { -1 };
    std::unordered_map<std::string, int> paramIndexById;
    std::array<std::atomic<bool>, 8> inputConnectedFlags;

    mutable juce::SpinLock sampleLock;
    std::shared_ptr<const juce::AudioBuffer<float>> loadedSample;
    double loadedSampleRate { 44100.0 };
    std::atomic<int> sampleChangeCounter { 0 };

    std::unique_ptr<juce::FileChooser> activeChooser;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SynthModule)
    JUCE_DECLARE_NON_COPYABLE (SynthModule)
};

//==============================================================================
// ModuleFactory - Meyer's singleton, self-registration target
//==============================================================================
struct RegisteredModule
{
    ModuleDescriptor descriptor;
    std::function<std::unique_ptr<SynthModule>()> create;
};

class ModuleFactory
{
public:
    static ModuleFactory& instance();   // Meyer's singleton (defined in ModuleCore.cpp)

    bool registerModule (ModuleDescriptor descriptor, std::function<std::unique_ptr<SynthModule>()> create)
    {
        registered.push_back ({ std::move (descriptor), std::move (create) });
        return true;
    }

    const std::vector<RegisteredModule>& all() const { return registered; }

    const RegisteredModule* find (const juce::String& typeId) const
    {
        for (auto& r : registered)
            if (r.descriptor.typeId == typeId)
                return &r;
        return nullptr;
    }

    std::unique_ptr<SynthModule> createInstance (const juce::String& typeId) const
    {
        if (auto* r = find (typeId))
        {
            auto m = r->create();
            m->initialiseFromDescriptor (r->descriptor);
            return m;
        }
        return nullptr;
    }

private:
    ModuleFactory() = default;
    std::vector<RegisteredModule> registered;   // vector, NOT map - descriptor sidebarOrder rules ordering
};

} // namespace aquanode

//==============================================================================
// Self-registration macro - place at file scope in the module's .cpp
//==============================================================================
// The registration variable is only "used" for its side effect (inserting the
// module into the factory during static init). Nothing else references it, so
// a linker is free to discard it - and MSVC in particular does, which makes
// every self-registered module silently vanish. We therefore give the symbol
// external "C" linkage (so its name is predictable) and force the linker to
// keep it: /include: on MSVC, __attribute__((used)) on GCC/Clang.
#if defined(_MSC_VER)
  #if defined(_WIN64)
    #define AQUANODE_KEEP_SYMBOL(sym) __pragma (comment (linker, "/include:" #sym))
  #else
    #define AQUANODE_KEEP_SYMBOL(sym) __pragma (comment (linker, "/include:_" #sym))
  #endif
  #define AQUANODE_USED_ATTR
#else
  #define AQUANODE_KEEP_SYMBOL(sym)
  #define AQUANODE_USED_ATTR __attribute__ ((used))
#endif

#define AQUANODE_REGISTER_MODULE(ClassName, descriptorFn) \
    extern "C" AQUANODE_USED_ATTR bool ClassName##_aqRegistered = \
        aquanode::ModuleFactory::instance().registerModule ( \
            descriptorFn(), [] { return std::make_unique<ClassName>(); }); \
    AQUANODE_KEEP_SYMBOL (ClassName##_aqRegistered)
