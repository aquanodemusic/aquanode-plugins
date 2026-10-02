#include "DiscardMidiModule.h"

using namespace aquanode;

namespace
{
    const char* kClassNames[DiscardMidiModule::kNumClasses] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    constexpr int kCols = 4;
    constexpr int kRows = 3;   // C C# D D# / E F F# G / G# A A# B
}

//==============================================================================
// twelve toggle boxes: highlighted = that note class is discarded
//==============================================================================
class DiscardMidiGrid : public juce::Component,
                        public aquanode::CustomParamCableTargets,
                        private juce::Timer
{
public:
    explicit DiscardMidiGrid (DiscardMidiModule& m) : module (&m)
    {
        startTimerHz (12);   // reflect patch loads, mutations, cable-driven boxes
    }

    void paint (juce::Graphics& g) override
    {
        auto* dm = dynamic_cast<DiscardMidiModule*> (module.get());
        if (dm == nullptr)
            return;

        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        for (int nc = 0; nc < DiscardMidiModule::kNumClasses; ++nc)
        {
            const auto box = boxRect (nc).toFloat();
            const bool byHand = dm->isClassSetByHand (nc);
            const bool live = dm->isClassLiveDiscarded (nc);

            // discarded = lit; the live state (cables folded in) is what the
            // engine actually acts on, so that is what the fill shows
            g.setColour (live ? juce::Colour (0xffd9705a) : juce::Colour (0xff262626));
            g.fillRoundedRectangle (box, 3.0f);

            g.setColour (live ? juce::Colours::white.withAlpha (0.75f)
                              : juce::Colours::white.withAlpha (0.18f));
            g.drawRoundedRectangle (box, 3.0f, 1.0f);

            g.setColour (live ? juce::Colours::white : juce::Colours::white.withAlpha (0.55f));
            g.drawText (kClassNames[nc], box, juce::Justification::centred, false);

            // a box a cable is currently overriding gets a marker dot
            if (live != byHand)
            {
                g.setColour (juce::Colour (0xffd970b0));
                g.fillEllipse (box.getRight() - 6.0f, box.getY() + 2.0f, 4.0f, 4.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // right-click belongs to the module's knob-modulation menu
        if (e.mods.isPopupMenu())
            return;

        auto* m = module.get();
        if (m == nullptr)
            return;

        const int nc = classAt (e.getPosition());
        if (nc < 0)
            return;

        const auto id = paramIdFor (nc);
        m->setParameter (id, m->getParameter (id) > 0.5f ? 0.0f : 1.0f);
        repaint();
    }

    //=== CustomParamCableTargets: each box accepts a knob-modulation cable ===
    juce::String paramTargetAt (juce::Point<int> localPos) const override
    {
        const int nc = classAt (localPos);
        return nc < 0 ? juce::String() : paramIdFor (nc);
    }

    juce::Point<int> paramTargetCentre (const juce::String& paramId) const override
    {
        for (int nc = 0; nc < DiscardMidiModule::kNumClasses; ++nc)
            if (paramIdFor (nc) == paramId)
                return boxRect (nc).getCentre();
        return { -1, -1 };
    }

private:
    static juce::String paramIdFor (int nc) { return "nc" + juce::String (nc); }

    juce::Rectangle<int> boxRect (int nc) const
    {
        const int col = nc % kCols;
        const int row = nc / kCols;
        const int w = getWidth() / kCols;
        const int h = getHeight() / kRows;
        return juce::Rectangle<int> (col * w, row * h, w, h).reduced (2);
    }

    int classAt (juce::Point<int> pos) const
    {
        for (int nc = 0; nc < DiscardMidiModule::kNumClasses; ++nc)
            if (boxRect (nc).expanded (2).contains (pos))
                return nc;
        return -1;
    }

    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> DiscardMidiModule::createExtraContentComponent()
{
    return std::make_unique<DiscardMidiGrid> (*this);
}

//==============================================================================
static ModuleDescriptor discardMidiDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.discardmidi";
    d.displayName = "Discard Midi";
    d.description =
        "Twelve boxes, one per note class - every one switched on is discarded, in any octave, so "
        "selected notes simply never play. Its Midi Out goes into a generator's Add Midi In, and "
        "like Always Midi it REPLACES the played keys for that generator, so a filtered voice and "
        "an unfiltered one can run off the same keys. The boxes take mod cables too: an LFO on a "
        "box gates that note class in and out while you play.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 24;
    d.sockets = {
        midiOut ("midiOut", "Midi Out")
    };

    // one hidden toggle per note class, rendered by the grid above; flagged as
    // cable targets so an LFO can gate a class while playing
    for (int i = 0; i < DiscardMidiModule::kNumClasses; ++i)
        d.params.push_back (makeRotary ("nc" + juce::String (i),
                                        "NC" + juce::String (i),
                                        0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f)
                                .hide()
                                .cableTargetInCustomUI());
    return d;
}

AQUANODE_REGISTER_MODULE (DiscardMidiModule, discardMidiDescriptor)
