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
   
    // --- Transpose Slider (Vertical, ±7 octaves) ---
    transposeSlider.setSliderStyle(juce::Slider::LinearVertical);
    transposeSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, 60, 20);

    addAndMakeVisible(transposeSlider);

    transposeAttach = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "transpose", transposeSlider);

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

    glideSlider.setSliderStyle(juce::Slider::LinearVertical);
    glideSlider.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, 60, 20);

    addAndMakeVisible(glideSlider);

    glideAttach = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "glideTime", glideSlider);

    // --- 5. Window Size ---
    setSize(700, 300);
}

AliasSynthAudioProcessorEditor::~AliasSynthAudioProcessorEditor()
{
}

//==============================================================================
void AliasSynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    // ===============================
    // Background gradient
    // ===============================
    juce::Colour topColour = juce::Colour(0xFF40FFFF);
    juce::Colour bottomColour = juce::Colour(0xFF00E5EE);

    juce::ColourGradient gradient(
        topColour, 0, 0,
        bottomColour, 0, (float)getHeight(),
        false);

    g.setGradientFill(gradient);
    g.fillAll();

    // ===============================
    // Labels
    // ===============================
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);

    auto drawLabel = [&](juce::String text, juce::Component& c)
        {
            g.drawText(
                text,
                c.getX(),
                c.getY() - 15,
                c.getWidth(),
                20,
                juce::Justification::centred);
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

    // ===============================
    // Transpose label
    // ===============================
    g.setFont(13.0f);
    g.drawText(
        "Transpose",
        transposeSlider.getX(),
        transposeSlider.getY() - 32,
        transposeSlider.getWidth(),
        32,
        juce::Justification::centred);

    // ===============================
    // Footer
    // ===============================
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(12.0f);

    g.drawText(
        "AliasSynth by aquanode",
        20,
        getHeight() - 25,
        200,
        20,
        juce::Justification::left);

    g.setFont(13.0f);
    g.drawText(
        "Glide\n(ms)",
        glideSlider.getX(),
        glideSlider.getY() - 30,
        glideSlider.getWidth(),
        30,
        juce::Justification::centred);
}

void AliasSynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // ===============================
    // ROW 1: ADSR + Volume + Transpose
    // ===============================
    auto topRow = area.removeFromTop(130);

    // Transpose slider on the top-right
    auto transposeArea = topRow.removeFromRight(80);
    transposeSlider.setBounds(transposeArea.reduced(10));

    // Remaining space for ADSR + Volume
    int topSlotWidth = topRow.getWidth() / 5;

    attackSlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    decaySlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    sustainSlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    releaseSlider.setBounds(topRow.removeFromLeft(topSlotWidth));
    volSlider.setBounds(topRow.removeFromLeft(topSlotWidth));

    area.removeFromTop(20); // Spacer

    // ====================================
    // ROW 2: Utilities + FM + Rate / Bits
    // ====================================
    auto bottomRow = area.removeFromTop(130);

    // Reserve space on the right for Glide slider
    auto glideArea = bottomRow.removeFromRight(80);
    glideSlider.setBounds(glideArea.reduced(10));

    // Remaining space for utilities + knobs
    int bottomSlotWidth = bottomRow.getWidth() / 5;

    // Slot 1: Utility cluster
    auto utilArea = bottomRow.removeFromLeft(bottomSlotWidth);

    modeSelector.setBounds(
        utilArea.removeFromTop(30).withSizeKeepingCentre(110, 24));

    utilArea.removeFromTop(5);

    loadButton.setBounds(
        utilArea.removeFromTop(35).withSizeKeepingCentre(110, 30));

    utilArea.removeFromTop(5);

    // Toggle row: Fold + S&H
    auto toggleRow =
        utilArea.removeFromTop(25).withSizeKeepingCentre(110, 25);

    foldToggle.setBounds(toggleRow.removeFromLeft(55));
    srModeToggle.setBounds(toggleRow);

    // Slots 2–5: FM / Ratio / Nyquist / Bits
    fmAmountSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));
    fmRatioSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));
    nyquistSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));
    bitDepthSlider.setBounds(bottomRow.removeFromLeft(bottomSlotWidth));


}
