#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpringerAudioProcessorEditor::SpringerAudioProcessorEditor(SpringerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    auto& apvts = audioProcessor.apvts;

    //============================================================================== 
    // Knob setup
    setupKnob(widthSlider);
    setupKnob(resonanceSlider);
    setupKnob(couplingSlider);
    setupKnob(dampingSlider);
    setupKnob(lfoRateSlider);
    setupKnob(lfoDepthSlider);
    setupKnob(wetSlider);
    setupKnob(densityASlider); // Setup density knobs
    setupKnob(densityBSlider);

    // Add visible
    addAndMakeVisible(widthSlider);
    addAndMakeVisible(resonanceSlider);
    addAndMakeVisible(couplingSlider);
    addAndMakeVisible(dampingSlider);
    addAndMakeVisible(lfoRateSlider);
    addAndMakeVisible(lfoDepthSlider);
    addAndMakeVisible(wetSlider);
    addAndMakeVisible(densityASlider);
    addAndMakeVisible(densityBSlider);

    // --- New horizontal slider ---
    numStagesSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    numStagesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 15);
    addAndMakeVisible(numStagesSlider);


    //============================================================================== 
    // Buttons
    randomizeButton.setButtonText("Random Allpass");
    addAndMakeVisible(randomizeButton);
    randomizeButton.onClick = [this]() {
        audioProcessor.randomizeSprings();
        };

    muteDryButton.setButtonText("Wet Only");
    muteDryButton.setClickingTogglesState(true); // makes it toggle
    muteDryButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan); // lights up cyan when active
    addAndMakeVisible(muteDryButton);

    //============================================================================== 
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

    // --- Attachment ---
    numStagesAttachment = std::make_unique<SliderAttachment>(apvts, "numStages", numStagesSlider);

    setSize(620, 320);
}


SpringerAudioProcessorEditor::~SpringerAudioProcessorEditor() {}

//==============================================================================
void SpringerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(15);

    // Reserve top row for title + numStages slider
    auto topRow = area.removeFromTop(50);

    // numStagesSlider on top-right
    auto sliderWidth = 200;
    numStagesSlider.setBounds(topRow.removeFromRight(sliderWidth).reduced(5));

    // Top controls row below title
    int rowHeight = area.getHeight() / 2;
    auto topControls = area.removeFromTop(rowHeight);
    auto bottomControls = area;

    int colWidth = topControls.getWidth() / 5;

    auto layoutSlot = [&](juce::Rectangle<int> slot, juce::Slider& slider) {
        slot.removeFromTop(20);
        slider.setBounds(slot.reduced(2));
        };

    // Top row: 4 main knobs
    layoutSlot(topControls.removeFromLeft(colWidth), widthSlider);
    layoutSlot(topControls.removeFromLeft(colWidth), resonanceSlider);
    layoutSlot(topControls.removeFromLeft(colWidth), couplingSlider);
    layoutSlot(topControls.removeFromLeft(colWidth), dampingSlider);

    // Buttons column
    auto buttonColumn = topControls.removeFromLeft(colWidth);
    muteDryButton.setBounds(buttonColumn.removeFromTop(40));
    buttonColumn.removeFromTop(10);
    randomizeButton.setBounds(buttonColumn.removeFromTop(40));

    // Bottom row: 3 main sliders + 2 density knobs
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

    // Title area: leave space so it's vertically centered with numStages slider
    auto titleArea = juce::Rectangle<int>(50, 10, 300, 50); // x=10, y=10 for some padding

    juce::String title = "Springer by aquanode";

    g.setFont(juce::Font(28.0f).boldened());

    // Draw glow (shadow) multiple times
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    for (int dx = -2; dx <= 2; ++dx)
    {
        for (int dy = -2; dy <= 2; ++dy)
        {
            if (dx != 0 || dy != 0)
                g.drawFittedText(title, titleArea.translated(dx, dy),
                    juce::Justification::centredLeft, 1);
        }
    }

    // Draw main title text in white
    g.setColour(juce::Colours::white);
    g.drawFittedText(title, titleArea, juce::Justification::centredLeft, 1);

    // --- Slider labels ---
    g.setFont(12.0f);
    auto drawLabel = [&](juce::String text, juce::Rectangle<int> s) {
        auto labelArea = s.withY(s.getY() - 18).withHeight(18);
        g.setColour(juce::Colours::black);
        g.drawFittedText(text, labelArea, juce::Justification::centredBottom, 1);
        };

    // Row 1
    drawLabel("Width/Time", widthSlider.getBounds());
    drawLabel("Resonance", resonanceSlider.getBounds());
    drawLabel("Coupling", couplingSlider.getBounds());
    drawLabel("Damping", dampingSlider.getBounds());

    // Row 2
    drawLabel("Mod Rate", lfoRateSlider.getBounds());
    drawLabel("Mod Depth", lfoDepthSlider.getBounds());
    drawLabel("Wet Gain", wetSlider.getBounds());
    drawLabel("Delay 1", densityASlider.getBounds());
    drawLabel("Delay 2", densityBSlider.getBounds());

    // New slider label
    drawLabel("Dispersion Stages", numStagesSlider.getBounds());
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