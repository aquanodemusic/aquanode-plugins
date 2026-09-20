// Generated from the Supernova II parameter specification. Do not hand-edit.
#pragma once
#include <JuceHeader.h>

namespace aquanova {

struct ParamDesc
{
    int         index;      // hardware parameter index
    const char* id;         // APVTS parameter id
    const char* label;      // human readable
    int         min, max, def;
    bool        bipolar;    // displays as -64..+63
    bool        discrete;   // present as a choice / dropdown
    const char* valueList;  // key into getValueList()
};

extern const ParamDesc kParams[];
extern const int       kNumParams;

// Returns the display strings for a named value list (empty if unknown).
const juce::StringArray& getValueList (const juce::String& key);

// Index of a parameter by hardware index, or -1.
int paramSlotForIndex (int hardwareIndex);

} // namespace aquanova
