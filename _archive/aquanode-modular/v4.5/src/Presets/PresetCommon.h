#pragma once

#include <JuceHeader.h>
#include "../Modules/ModuleCore.h"

class AquanodeModularAudioProcessor;
struct ModuleInstance;

// Shared plumbing for the patch generators in this folder (FM, the genre
// generators). The Colorbass generator predates it and keeps its own small
// builder; everything newer goes through PatchSketch.
namespace aquanode::presets
{
    // A patch builder: fills an EMPTY processor patch and returns a short
    // display name for it. The processor clears the patch before calling and
    // rebuilds the graph afterwards (AquanodeModularAudioProcessor::generatePatch).
    using BuildFn = std::function<juce::String (AquanodeModularAudioProcessor&)>;

    // The size a module's card will have in the patch area, worked out from its
    // descriptor the same way the editor lays a card out (header from the
    // socket count, one row per knob row, the custom content below). Used to
    // stack cards without overlaps when there is no editor to measure.
    int estimateCardWidth (const ModuleInstance& inst);
    int estimateCardHeight (const ModuleInstance& inst);

    // Builds a patch by columns. Every module is added to a column; columns are
    // laid out left to right and their cards stacked top to bottom, so the
    // signal reads from left to right. A column taller than `maxColumnHeight`
    // continues in a new column to its right.
    class PatchSketch
    {
    public:
        explicit PatchSketch (AquanodeModularAudioProcessor& processor) : p (processor) {}

        int add (const juce::String& typeId, int column);

        void set (int id, const juce::String& paramId, float value);
        // sets a Combo / stepped knob by the text of its choice ("LP24", "1/8")
        bool setChoice (int id, const juce::String& paramId, const juce::String& choice);
        float get (int id, const juce::String& paramId) const;

        bool wire (int from, const juce::String& out, int to, const juce::String& in);
        bool mod (int from, const juce::String& out, int to, const juce::String& paramId, float depth);

        void collapse (int id, bool shouldCollapse = true);
        SynthModule* dsp (int id) const;

        // places every card; call once at the end
        void layout (int maxColumnHeight = 1500, juce::Point<int> origin = { 40, 40 });

        // explicit position (for hand-made layouts such as the DX algorithm
        // chart); a module placed this way is skipped by layout()
        void place (int id, juce::Point<int> pos);

        AquanodeModularAudioProcessor& processor() { return p; }

    private:
        AquanodeModularAudioProcessor& p;
        struct Entry { int id; int column; bool placed; };
        std::vector<Entry> entries;
    };

    // consecutive seeds must not give correlated patches: juce::Random's
    // first outputs for nearby seeds are close, so scramble the seed first
    juce::int64 scrambleSeed (juce::uint32 seed);

    // small random helpers shared by the generators
    struct Dice
    {
        explicit Dice (juce::uint32 seed) : r (scrambleSeed (seed)) {}
        juce::Random r;

        float range (float lo, float hi)   { return juce::jmap (r.nextFloat(), lo, hi); }
        bool chance (float probability)    { return r.nextFloat() < probability; }
        int  index (int n)                 { return n > 0 ? r.nextInt (n) : 0; }
        // multiplies by a factor between 1/(1+amount) and (1+amount), evenly in log
        float jitterMul (float value, float amount)
        {
            const float k = std::log1p (juce::jmax (0.0f, amount));
            return value * std::exp (range (-k, k));
        }
        float jitterAdd (float value, float amount, float lo, float hi)
        {
            return juce::jlimit (lo, hi, value + range (-amount, amount));
        }
    };
}
