#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpringerAudioProcessorEditor::SpringerAudioProcessorEditor(SpringerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    auto& apvts = audioProcessor.apvts;

    // Standard Knob Setup
    setupKnob(widthSlider);
    setupKnob(resonanceSlider);
    setupKnob(couplingSlider);
    setupKnob(dampingSlider);
    setupKnob(lfoRateSlider);
    setupKnob(lfoDepthSlider);
    setupKnob(wetSlider);
    setupKnob(densityASlider);
    setupKnob(densityBSlider);

    addAndMakeVisible(widthSlider);
    addAndMakeVisible(resonanceSlider);
    addAndMakeVisible(couplingSlider);
    addAndMakeVisible(dampingSlider);
    addAndMakeVisible(lfoRateSlider);
    addAndMakeVisible(lfoDepthSlider);
    addAndMakeVisible(wetSlider);
    addAndMakeVisible(densityASlider);
    addAndMakeVisible(densityBSlider);

    // --- Dispersion Slider ---
    numStagesSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    numStagesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 15);
    addAndMakeVisible(numStagesSlider);

    // --- Pitch Slider (Now matching Dispersion) ---
    pitchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    pitchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 15); // Changed from Below to Right
    pitchSlider.setRange(-24.0, 12.0, 0.01);
    pitchSlider.setSkewFactorFromMidPoint(-12.0);
    pitchSlider.setColour(juce::Slider::thumbColourId, juce::Colours::aqua);
    addAndMakeVisible(pitchSlider);

    // Buttons
    randomizeButton.setButtonText("Random Allpass");
    addAndMakeVisible(randomizeButton);
    randomizeButton.onClick = [this]() { audioProcessor.randomizeSprings(); };

    muteDryButton.setButtonText("Wet Only");
    muteDryButton.setClickingTogglesState(true);
    muteDryButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    addAndMakeVisible(muteDryButton);

    // Attachments
    widthAttachment = std::make_unique<SliderAttachment>(apvts, "width", widthSlider);
    resonanceAttachment = std::make_unique<SliderAttachment>(apvts, "resonance", resonanceSlider);
    couplingAttachment = std::make_unique<SliderAttachment>(apvts, "coupling", couplingSlider);
    dampingAttachment = std::make_unique<SliderAttachment>(apvts, "damping", dampingSlider);
    lfoRateAttachment = std::make_unique<SliderAttachment>(apvts, "lfoRate", lfoRateSlider);
    lfoDepthAttachment = std::make_unique<SliderAttachment>(apvts, "lfoDepth", lfoDepthSlider);
    wetAttachment = std::make_unique<SliderAttachment>(apvts, "wet", wetSlider);
    muteDryAttachment = std::make_unique<ButtonAttachment>(apvts, "muteDry", muteDryButton);
    densityAAttachment = std::make_unique<SliderAttachment>(apvts, "densityA", densityASlider);
    densityBAttachment = std::make_unique<SliderAttachment>(apvts, "densityB", densityBSlider);
    numStagesAttachment = std::make_unique<SliderAttachment>(apvts, "numStages", numStagesSlider);
    pitchAttachment = std::make_unique<SliderAttachment>(apvts, "pitch", pitchSlider);

    setSize(620, 320);
}


SpringerAudioProcessorEditor::~SpringerAudioProcessorEditor() {}

//==============================================================================
void SpringerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(15);

    //==================== TOP BAR ====================
    auto topRow = area.removeFromTop(60);

    // Left: title space
    const int titleWidth = 350;
    auto titleArea = topRow.removeFromLeft(titleWidth);

    // Right: top-right sliders column
    const int sliderColumnWidth = 180;
    auto topRightArea = topRow.removeFromRight(sliderColumnWidth);

    // Split into two identical rows
    auto pitchArea = topRightArea.removeFromTop(28);
    auto stagesArea = topRightArea.removeFromTop(28);

    // Set bounds (reduced(2) ensures they don't touch edges)
    pitchSlider.setBounds(pitchArea.reduced(2));
    numStagesSlider.setBounds(stagesArea.reduced(2));

    //==================== MAIN CONTROLS ====================
    int rowHeight = area.getHeight() / 2;
    auto topControls = area.removeFromTop(rowHeight);
    auto bottomControls = area;

    int colWidth = topControls.getWidth() / 5;

    auto layoutSlot = [&](juce::Rectangle<int> slot, juce::Slider& slider)
        {
            slot.removeFromTop(20);
            slider.setBounds(slot.reduced(2));
        };

    // Top row knobs
    layoutSlot(topControls.removeFromLeft(colWidth), widthSlider);
    layoutSlot(topControls.removeFromLeft(colWidth), resonanceSlider);
    layoutSlot(topControls.removeFromLeft(colWidth), couplingSlider);
    layoutSlot(topControls.removeFromLeft(colWidth), dampingSlider);

    // Buttons column
    auto buttonColumn = topControls.removeFromLeft(colWidth);
    muteDryButton.setBounds(buttonColumn.removeFromTop(40));
    buttonColumn.removeFromTop(10);
    randomizeButton.setBounds(buttonColumn.removeFromTop(40));

    // Bottom row knobs
    colWidth = bottomControls.getWidth() / 5;
    layoutSlot(bottomControls.removeFromLeft(colWidth), lfoRateSlider);
    layoutSlot(bottomControls.removeFromLeft(colWidth), lfoDepthSlider);
    layoutSlot(bottomControls.removeFromLeft(colWidth), wetSlider);
    layoutSlot(bottomControls.removeFromLeft(colWidth), densityASlider);
    layoutSlot(bottomControls.removeFromLeft(colWidth), densityBSlider);
}

void SpringerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::cyan);

    //==================== TITLE ====================
    juce::Rectangle<int> titleArea(50, 10, 300, 40);
    juce::String title = "Springer by aquanode";
    g.setFont(juce::Font(28.0f).boldened());

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 2; ++dy) {
            if (dx || dy) g.drawFittedText(title, titleArea.translated(dx, dy), juce::Justification::centredLeft, 1);
        }
    }

    g.setColour(juce::Colours::white);
    g.drawFittedText(title, titleArea, juce::Justification::centredLeft, 1);

    //==================== LABELS ====================
    g.setFont(12.0f);
    auto drawLabel = [&](const juce::String& text, juce::Rectangle<int> bounds)
        {
            auto labelArea = bounds.withY(bounds.getY() - 18).withHeight(18);
            g.setColour(juce::Colours::black);
            g.drawFittedText(text, labelArea, juce::Justification::centredBottom, 1);
        };

    drawLabel("Width / Time", widthSlider.getBounds());
    drawLabel("Resonance", resonanceSlider.getBounds());
    drawLabel("Coupling", couplingSlider.getBounds());
    drawLabel("Damping", dampingSlider.getBounds());
    drawLabel("Mod Rate", lfoRateSlider.getBounds());
    drawLabel("Mod Depth", lfoDepthSlider.getBounds());
    drawLabel("Wet Gain", wetSlider.getBounds());
    drawLabel("Delay 1", densityASlider.getBounds());
    drawLabel("Delay 2", densityBSlider.getBounds());

    //==================== TOP-RIGHT SLIDERS ====================
    auto drawLabelLeft = [&](juce::Rectangle<int> sliderBounds, const juce::String& label)
        {
            int labelWidth = 80;
            juce::Rectangle<int> labelArea(sliderBounds.getX() - labelWidth - 5,
                sliderBounds.getY(),
                labelWidth,
                sliderBounds.getHeight());
            g.setColour(juce::Colours::black);
            g.drawFittedText(label, labelArea, juce::Justification::centredRight, 1);
        };

    drawLabelLeft(pitchSlider.getBounds(), "Pitch");
    drawLabelLeft(numStagesSlider.getBounds(), "Dispersion");
}

//==============================================================================
void SpringerAudioProcessorEditor::setupKnob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);

    // Width 65 ensures we see "600.00" without dots.
    // Height 15 is slim enough to stay away from the row below.
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 65, 15);

    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::black);
    slider.setMouseDragSensitivity(150);
}