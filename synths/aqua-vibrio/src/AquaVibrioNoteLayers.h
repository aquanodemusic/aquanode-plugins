#pragma once
//==============================================================================
//  AquaVibrioNoteLayers.h
//
//  What the engine needs to know about note layers, and nothing more.
//
//  A note layer is a numbered set of keys that play their own variation of
//  the loaded patch: only the knobs that were changed for them are stored,
//  everything else follows the base patch. The editor side of this (choosing
//  notes, numbering, saving) lives in AquaVibrioNoteLayerManager.h; the
//  engine only ever sees the compiled NoteLayerSnapshot below.
//
//  Every parameter falls into one of three scopes:
//
//      Voice   - the oscillators, filters, envelopes, LFOs, matrix, amp:
//                each voice uses the value of the layer its note belongs to.
//      Chain   - the effects rack: each layer whose effect settings differ
//                from the base patch's is summed into an effect chain of its
//                own; layers whose effects match the base share the base
//                chain, so they sound exactly as they always did.
//      Global  - the arpeggiator, key mode, tempo and the audio input: these
//                decide WHICH notes play, or happen once for the whole
//                instrument, so they always come from the base patch.
//==============================================================================

#include <JuceHeader.h>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include "AquaVibrioParameters.h"

namespace aquavibrio
{

namespace layers
{
    enum class Scope : juce::uint8 { Voice, Chain, Global };

    inline int numParams() { return (int) allParameters().size(); }

    // Parameter table index by ID; -1 when unknown.
    inline int indexOf (const juce::String& id)
    {
        static const std::unordered_map<std::string, int> map = []
        {
            std::unordered_map<std::string, int> m;
            int i = 0;
            for (const auto& p : allParameters())
                m[std::string (p.id)] = i++;
            return m;
        }();

        const auto it = map.find (id.toStdString());
        return it != map.end() ? it->second : -1;
    }

    inline const std::vector<Scope>& scopes()
    {
        static const std::vector<Scope> table = []
        {
            const juce::StringArray globalIds { "KeyMode", "ClockTempo", "InputMode", "InputSelect",
                                                "InputFollowerMode", "InputFollowerAttack",
                                                "InputFollowerRelease", "InputFollowerLevel",
                                                "ControlSmoothMode" };
            const juce::StringArray chainRegions { "distortion", "filterBank", "vocoder", "character", "eq",
                                                   "phaser", "chorus", "delay", "reverb", "granulator", "fx" };
            std::vector<Scope> v;
            for (const auto& p : allParameters())
            {
                const juce::String id (p.id), region (p.region);

                if (region == "arp" || globalIds.contains (id))
                    v.push_back (Scope::Global);
                else if (chainRegions.contains (region) || id == "InputRingmodulator" || id == "SecondOutputBalance")
                    v.push_back (Scope::Chain);
                else
                    v.push_back (Scope::Voice);
            }
            return v;
        }();
        return table;
    }

    inline Scope scopeOf (int index)
    {
        const auto& s = scopes();
        return index >= 0 && index < (int) s.size() ? s[(size_t) index] : Scope::Voice;
    }

    inline bool isGlobal (const juce::String& id) { return scopeOf (indexOf (id)) == Scope::Global; }

    inline const std::vector<int>& indicesWithScope (Scope scope)
    {
        static const std::array<std::vector<int>, 3> lists = []
        {
            std::array<std::vector<int>, 3> l;
            for (int i = 0; i < numParams(); ++i)
                l[(size_t) scopeOf (i)].push_back (i);
            return l;
        }();
        return lists[(size_t) scope];
    }

    // "E4", "C#7" - middle C (60) is C4, the same as the on-screen keyboards.
    inline juce::String noteName (int note)
    {
        return juce::MidiMessage::getMidiNoteName (note, true, true, 4);
    }
}

//==============================================================================
// The compiled layout handed to the audio thread. Immutable once published:
// the manager builds a new one for every change and the engine switches at
// the start of a block.
//==============================================================================
struct NoteLayerSnapshot
{
    static constexpr int kMaxSlots = 128 + 2;   // every key its own layer, plus base and an open selection

    juce::uint64 serial { 0 };

    // Which slot each key plays from. Slot 0 is always the base patch.
    std::array<juce::uint8, 128> slotForNote {};

    struct Slot
    {
        int uid { 0 };       // stable identity: survives renumbering, keeps an effect chain bound
        bool live { false }; // the selection being edited: plays the live knob values
        std::vector<std::pair<int, float>> overrides;   // parameter index -> raw value
    };

    std::vector<Slot> slots { Slot {} };

    // While a selection is being edited the knobs show (and the APVTS holds)
    // the selection's values, so the base patch is parked here.
    bool sessionOpen { false };
    std::vector<float> stash;
};

} // namespace aquavibrio
