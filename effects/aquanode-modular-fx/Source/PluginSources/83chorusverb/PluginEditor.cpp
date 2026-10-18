/*
    PluginEditor.cpp  -  83ChorusVerb

    GPLv3.
*/
#include "PluginEditor.h"

namespace
{
    // Whether the visualizers are shown is a matter of taste, not sound, so it
    // is not a parameter: it is kept as a property on the plugin's state tree,
    // which getStateInformation() saves with everything else. Off by default.
    const juce::Identifier kShowVisualizersId { "showVisualizers" };
}

ChorusVerbAudioProcessorEditor::ChorusVerbAudioProcessorEditor (ChorusVerbAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    // `content` is laid out once, at the design size, and scaled as a whole
    // to whatever size the window is (see resized()).
    addAndMakeVisible (content);

    fxPanel = std::make_unique<cv83ui::FxPanel> (processor.apvts);
    content.addAndMakeVisible (*fxPanel);
    fxPanel->scopesButton.onClick = [this] { setVisualizersShown (! showScopes_); };

    showScopes_ = (bool) processor.apvts.state.getProperty (kShowVisualizersId, false);
    fxPanel->setScopesVisible (showScopes_);
    content.setSize (kDesignW, designH());
    fxPanel->setBounds (content.getLocalBounds().reduced (kMargin));

    setResizable (true, true);
    setResizeLimits (760, 100, 2 * kDesignW, 2 * kDesignHFull);   // installs the constrainer
    applyConstraints();                                              // ... and fits it to this height
    setSize (kDesignW, designH());
}

ChorusVerbAudioProcessorEditor::~ChorusVerbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

// The window keeps the design's proportions; those change with the
// visualizers, so the limits are worked out from the current height. Width
// runs from 760 px up to twice the design size. The constrainer is changed
// directly rather than through setResizeLimits(), which would at once squeeze
// the window into the new limits while the old proportions still applied.
void ChorusVerbAudioProcessorEditor::applyConstraints()
{
    const double aspect = (double) kDesignW / (double) designH();
    if (auto* c = getConstrainer()) {
        c->setFixedAspectRatio (aspect);
        c->setSizeLimits (760, juce::roundToInt (760.0 / aspect),
                          2 * kDesignW, juce::roundToInt (2.0 * kDesignW / aspect));
    }
}

// Shows or hides the displays and makes the window taller or shorter to
// match, keeping its current width (and so its current scale).
void ChorusVerbAudioProcessorEditor::setVisualizersShown (bool shown)
{
    const double scale = (double) getWidth() / (double) kDesignW;   // before anything changes

    showScopes_ = shown;
    processor.apvts.state.setProperty (kShowVisualizersId, shown, nullptr);

    fxPanel->setScopesVisible (shown);
    content.setSize (kDesignW, designH());
    fxPanel->setBounds (content.getLocalBounds().reduced (kMargin));

    applyConstraints();
    setSize (juce::roundToInt (kDesignW * scale), juce::roundToInt (designH() * scale));
    resized();   // the transform, even if the host kept the old size
}

void ChorusVerbAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (cv83ui::col::bg);
}

void ChorusVerbAudioProcessorEditor::resized()
{
    const auto avail = getLocalBounds();
    if (avail.isEmpty())
        return;

    // Fit the design inside the window, keeping its proportions, centred. The
    // constrainer already holds the window to the design's aspect ratio, so in
    // a host that honours it there is no margin at all.
    const float h = (float) designH();
    const float scale = juce::jmin ((float) avail.getWidth() / (float) kDesignW,
                                    (float) avail.getHeight() / h);
    const float dx = ((float) avail.getWidth()  - (float) kDesignW * scale) * 0.5f;
    const float dy = ((float) avail.getHeight() - h * scale) * 0.5f;
    content.setTransform (juce::AffineTransform::scale (scale).translated (dx, dy));
}
