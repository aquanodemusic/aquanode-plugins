/*  ==============================================================================
    SpectralSmooth — minimal editor implementation
    ==============================================================================*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralSmoothAudioProcessorEditor::SpectralSmoothAudioProcessorEditor(SpectralSmoothAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    titleLabel.setText("Spectral Smoothe", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::left);
    titleLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    // --- info text block ---------------------------------------------------
    infoLabel.setText(
        "Follow Modes: Freeze gets Amplitude Modulated by Original.\n"
        "Stretch Mode: Linearly Interpolated Freezing, Continous Evolution.\n"
        "Recommended: 8192 FFT Size for Evolve Mode, for others 2048 is fine.",
        juce::dontSendNotification);
    infoLabel.setJustificationType(juce::Justification::topLeft);
    infoLabel.setFont(juce::Font(12.0f, juce::Font::plain));
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(infoLabel);

    // --- freeze button, big and obvious ------------------------------------
    freezeButton.setClickingTogglesState(true);
    freezeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orangered);
    addAndMakeVisible(freezeButton);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, pid::freeze, freezeButton);

    // --- freeze mode + fft size combo boxes ---------------------------------
    modeBox.addItemList({ "Static Hold", "Evolving Hold", "Continuous Hold",
                           "Static Follow", "Evolving Follow", "Continuous Follow",
                           "Stretch" }, 1);
    modeBox.setTooltip("Hold: gain-match freezes with the spectrum, level stays steady.\n"
        "Follow: gain-match keeps tracking the live input while frozen, "
        "so the frozen sound can dip quiet if the input does.\n"
        "Stretch: continuously morphs the frozen spectrum toward a freshly "
        "re-captured one over each grain period, gain-match stays steady like Hold.");
    addAndMakeVisible(modeBox);

    modeLabel.setText("Freeze Mode", juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(modeLabel);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, pid::freezeMode, modeBox);
    modeBox.onChange = [this] { updateModeDependentEnablement(); };

    fftBox.addItemList({ "512", "1024", "2048", "4096", "8192", "16384" }, 1);
    addAndMakeVisible(fftBox);

    fftLabel.setText("FFT Size", juce::dontSendNotification);
    fftLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(fftLabel);

    fftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, pid::fftSize, fftBox);

    // --- knobs ---------------------------------------------------------------
    setupKnob(characterKnob, pid::character, "Character");
    setupKnob(evolveKnob, pid::evolveRate, "Evolve Rate");
    setupKnob(diffusionKnob, pid::diffusion, "Cont. Diffuse");
    setupKnob(stretchKnob, pid::stretchTime, "Stretch Time");
    setupKnob(mixKnob, pid::mix, "Mix");

    // --- gain match toggle ---------------------------------------------------
    gainMatchToggle.setButtonText("Gain Match");
    addAndMakeVisible(gainMatchToggle);
    gainMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, pid::gainMatch, gainMatchToggle);

    // Expanded vertical space slightly to accommodate the top info text cleanly
    setSize(680, 380);

    updateModeDependentEnablement();
}

SpectralSmoothAudioProcessorEditor::~SpectralSmoothAudioProcessorEditor() {}

void SpectralSmoothAudioProcessorEditor::setupKnob(Knob& k, const juce::String& paramId, const juce::String& labelText)
{
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    addAndMakeVisible(k.slider);
    k.label.setText(labelText, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramId, k.slider);
}

void SpectralSmoothAudioProcessorEditor::updateModeDependentEnablement()
{
    const auto mode = (FreezeMode)modeBox.getSelectedItemIndex();
    const bool isStretch = (mode == FreezeMode::Stretch);
    const int base = isStretch ? -1 : freezeModeBase(mode); // 0 = Static, 1 = Evolving, 2 = Continuous

    const bool evolveActive = (base == 1);
    evolveKnob.slider.setEnabled(evolveActive);
    evolveKnob.label.setEnabled(evolveActive);

    const bool diffusionActive = (base == 2);
    diffusionKnob.slider.setEnabled(diffusionActive);
    diffusionKnob.label.setEnabled(diffusionActive);

    stretchKnob.slider.setEnabled(isStretch);
    stretchKnob.label.setEnabled(isStretch);
}

void SpectralSmoothAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1c1f));
}

void SpectralSmoothAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);

    // --- Header Section: Title on Left, Multi-line Info on Right ---
    auto headerRow = area.removeFromTop(54);
    titleLabel.setBounds(headerRow.removeFromLeft(220));
    infoLabel.setBounds(headerRow);

    area.removeFromTop(12);

    // --- Top Control Row: Freeze, Mode, FFT Size, Gain Match ---
    auto topRow = area.removeFromTop(60);

    // Freeze Button
    freezeButton.setBounds(topRow.removeFromLeft(90));
    topRow.removeFromLeft(16);

    // Freeze Mode Combo
    auto modeCol = topRow.removeFromLeft(190);
    modeLabel.setBounds(modeCol.removeFromTop(16));
    modeBox.setBounds(modeCol.removeFromTop(28));

    topRow.removeFromLeft(16);

    // FFT Size Combo
    auto fftCol = topRow.removeFromLeft(120);
    fftLabel.setBounds(fftCol.removeFromTop(16));
    fftBox.setBounds(fftCol.removeFromTop(28));

    topRow.removeFromLeft(16);

    // Gain Match Toggle (Aligned to the right of FFT Box)
    gainMatchToggle.setBounds(topRow.removeFromLeft(110).withSizeKeepingCentre(110, 24));

    area.removeFromTop(16);

    // --- Knob Row ---
    auto knobRow = area.removeFromTop(180);
    const int knobWidth = knobRow.getWidth() / 5;
    auto layoutKnob = [&](Knob& k)
        {
            auto col = knobRow.removeFromLeft(knobWidth).reduced(6);
            k.label.setBounds(col.removeFromTop(18));
            k.slider.setBounds(col);
        };

    layoutKnob(characterKnob);
    layoutKnob(evolveKnob);
    layoutKnob(diffusionKnob);
    layoutKnob(stretchKnob);
    layoutKnob(mixKnob);
}