#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AliasSynthAudioProcessorEditor::AliasSynthAudioProcessorEditor(AliasSynthAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // --- 1. The Setup Lambda ---
    auto setupSlider = [this](juce::Slider& s, juce::String paramID, std::unique_ptr<SliderAttachment>& attach)
        {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
            addAndMakeVisible(s);
            attach = std::make_unique<SliderAttachment>(audioProcessor.apvts, paramID, s);
        };

    // --- 2. Row 1: ADSR + Volume ---
    setupSlider(attackSlider, "attack", attackAttach);
    setupSlider(decaySlider, "decay", decayAttach);
    setupSlider(sustainSlider, "sustain", sustainAttach);
    setupSlider(releaseSlider, "release", releaseAttach);
    setupSlider(volSlider, "masterVol", volAttach);

    // --- 3. Row 2: FM + Digital Degrade ---
    setupSlider(fmAmountSlider, "fmAmount", fmAmountAttach);
    setupSlider(fmRatioSlider, "fmRatio", fmRatioAttach);
    setupSlider(nyquistSlider, "nyquistLimit", nyquistAttach);
    setupSlider(bitDepthSlider, "bitDepth", bitDepthAttach);

    // --- 4. Utility Section ---
    auto* modeParam = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.apvts.getParameter("mode"));
    if (modeParam != nullptr)
        modeSelector.addItemList(modeParam->choices, 1);

    addAndMakeVisible(modeSelector);
    modeAttach = std::make_unique<ComboAttachment>(audioProcessor.apvts, "mode", modeSelector);

    loadButton.setButtonText("LOAD SAMPLE");
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { audioProcessor.loadSample(); };

    // Fold Toggle
    foldToggle.setButtonText("Fold"); // Renamed for space
    addAndMakeVisible(foldToggle);
    foldAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "fold", foldToggle);

    // S&H Toggle (The new Sample Rate Mode button)
    srModeToggle.setButtonText("S&H");
    addAndMakeVisible(srModeToggle);
    srModeAttach = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "srMode", srModeToggle);

    // --- 5. Window Size ---
    setSize(700, 300);
}

AliasSynthAudioProcessorEditor::~AliasSynthAudioProcessorEditor()
{
}

//==============================================================================
void AliasSynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // --- ROW 1: ADSR + Volume (5 Slots) ---
    auto topRow = area.removeFromTop(130);
    int topSlotWidth = topRow.getWidth() / 5;

    attackSlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    decaySlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    sustainSlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    releaseSlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    volSlider.setBounds(topRow.removeFromLeft(topSlotWidth));

    area.removeFromTop(20); // Spacer

    // --- ROW 2: Utilities + FM + Rate/Bits ---
    auto bottomRow = area.removeFromTop(130);
    int bottomSlotWidth = bottomRow.getWidth() / 5;

    // Slot 1: Utility Cluster (Selector, Load Button, Toggles)
    auto utilArea = bottomRow.removeFromLeft(bottomSlotWidth);

    modeSelector.setBounds(utilArea.removeFromTop(30).withSizeKeepingCentre(110, 24));
    utilArea.removeFromTop(5);
    loadButton.setBounds(utilArea.removeFromTop(35).withSizeKeepingCentre(110, 30));
    utilArea.removeFromTop(5);

    // Splitting the toggle row into two for "Fold" and "S&H"
    auto toggleRow = utilArea.removeFromTop(25).withSizeKeepingCentre(110, 25);
    foldToggle.setBounds(toggleRow.removeFromLeft(55));
    srModeToggle.setBounds(toggleRow);

    // Slots 2-5: The Knobs
    fmAmountSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));
    fmRatioSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));
    nyquistSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));
    bitDepthSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));
}

void AliasSynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background Gradient
    juce::Colour topColour = juce::Colour(0xFF40FFFF);
    juce::Colour bottomColour = juce::Colour(0xFF00E5EE);
    juce::ColourGradient gradient(topColour, 0, 0, bottomColour, 0, (float)getHeight(), false);

    g.setGradientFill(gradient);
    g.fillAll();

    g.setColour(juce::Colours::white);
    g.setFont(15.0f);

    auto drawLabel = [&](juce::String text, juce::Component& c) {
        g.drawText(text, c.getX(), c.getY() - 15, c.getWidth(), 20, juce::Justification::centred);
        };

    drawLabel("Attack", attackSlider);
    drawLabel("Decay", decaySlider);
    drawLabel("Sustain", sustainSlider);
    drawLabel("Release", releaseSlider);
    drawLabel("Volume", volSlider);
    drawLabel("FM Amt", fmAmountSlider);
    drawLabel("FM Ratio", fmRatioSlider);
    drawLabel("Nyquist", nyquistSlider);
    drawLabel("Bits", bitDepthSlider);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(12.0f);
    g.drawText("AliasSynth by aquanode", 20, getHeight() - 25, 200, 20, juce::Justification::left);
}