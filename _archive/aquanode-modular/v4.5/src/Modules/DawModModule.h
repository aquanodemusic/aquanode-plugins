#pragma once

#include "ModuleCore.h"

// DAW Mod - a modulation source the host automates. The plugin exposes twelve
// fixed automation parameters ("DAW Mod 1".."DAW Mod 12"); each DAW Mod module
// you add claims the next free one, in the order the modules were created, and
// shows its number in its title bar. A thirteenth instance finds no free slot,
// shows "off", and silently outputs nothing rather than fighting over a slot.
//
// Output is the host parameter's raw 0..1 value. Patch DC Offset after it
// (Gain 2, Offset -1) if a bipolar -1..+1 sweep is wanted instead.
// Output: 0 = Mod Out.
class DawModModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxSlots = 12;

    // assigned by the processor whenever the graph is rebuilt; -1 = no slot
    void setSlot (int s) { slot = s; }
    int getSlot() const { return slot; }

    juce::String titleSuffix() const override
    {
        return slot >= 0 ? " " + juce::String (slot + 1) : " (off)";
    }

    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        const float v = (slot >= 0 && hostModValues != nullptr) ? hostModValues[slot] : 0.0f;
        outputs[0][0] = v;
        outputs[0][1] = v;
    }

private:
    int slot { -1 };
};
