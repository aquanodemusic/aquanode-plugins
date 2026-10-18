#include "Parameters.h"

namespace aquanova {

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = kParams[i];
        const juce::ParameterID pid { d.id, 1 };
        const juce::String label (d.label);
        const juce::StringArray list = getValueList (juce::String (d.valueList));

        if (d.discrete && list.size() >= (d.max - d.min + 1) && (d.max - d.min) < 64)
        {
            // Small enumerated set -> a choice parameter, so the editor can use a combo box.
            juce::StringArray choices;
            for (int v = d.min; v <= d.max; ++v)
                choices.add (list[v]);

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                            pid, label, choices, juce::jlimit (0, choices.size() - 1, d.def - d.min)));
        }
        else
        {
            // Everything else stays an integer in the hardware's own units, so a
            // knob position here means exactly what it means on the front panel.
            auto textFor = [list, min = d.min] (int v, int) -> juce::String
            {
                const int idx = v - min;
                if (juce::isPositiveAndBelow (idx, list.size()))
                    return list[idx];
                return juce::String (v);
            };

            auto valueFor = [list, min = d.min] (const juce::String& t) -> int
            {
                const int idx = list.indexOf (t.trim());
                return idx >= 0 ? idx + min : t.getIntValue();
            };

            layout.add (std::make_unique<juce::AudioParameterInt> (
                            pid, label, d.min, d.max, d.def,
                            juce::AudioParameterIntAttributes()
                                .withStringFromValueFunction (textFor)
                                .withValueFromStringFunction (valueFor)));
        }
    }

    // Ours, not the Supernova's: a defeatable leaky-integrator DC blocker on
    // the output. On by default.
    layout.add (std::make_unique<juce::AudioParameterBool> (
                    juce::ParameterID { kDcBlockId, 1 }, "DC Block", true));

    // How fast it cancels: slow (1 Hz) is click-free, fast (5 Hz) is quicker
    // to reclaim headroom but can clack on a sudden DC-heavy onset. Defaults
    // to the slow, click-free end.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
                    juce::ParameterID { kDcSpeedId, 1 }, "DC Cancel Speed",
                    juce::NormalisableRange<float> (kDcSpeedMinHz, kDcSpeedMaxHz), kDcSpeedMinHz,
                    juce::AudioParameterFloatAttributes()
                        .withLabel ("Hz")
                        .withStringFromValueFunction (
                            [] (float v, int) { return juce::String (v, 1) + " Hz"; })));

    return layout;
}

} // namespace aquanova
