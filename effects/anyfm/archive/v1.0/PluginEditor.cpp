#include "PluginProcessor.h"
#include "PluginEditor.h"

AnyFMAudioProcessorEditor::AnyFMAudioProcessorEditor(AnyFMAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(600, 400);

    // --- DX7 Farbpalette ---
    const auto dx7Cyan = juce::Colour(0xFF00E5E5);
    const auto dx7Darker = juce::Colour(0xFF1A1A1A);

    // --- Helper Lambda für Slider Setup ---
    auto setupSlider = [&](juce::Slider& slider, juce::Label& label, juce::String name, juce::AudioParameterFloat* param)
        {
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);

            // DX7 Look für den Slider (Cyan Wheels)
            slider.setColour(juce::Slider::rotarySliderFillColourId, dx7Cyan);
            slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::black);
            slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
            slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentWhite);

            slider.setRange(param->range.start, param->range.end, param->range.interval);
            slider.setValue(param->get());

            slider.onValueChange = [this, param, &slider]
                {
                    *param = slider.getValue();
                };

            addAndMakeVisible(label);
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId, juce::Colours::white);
            label.setFont(juce::Font(14.0f, juce::Font::bold));
            label.attachToComponent(&slider, false);
        };

    // Die Regler initialisieren
    setupSlider(modulationIndexSlider, modulationIndexLabel, "MOD DEPTH", audioProcessor.modulationIndex);
    setupSlider(modulatorGainSlider, modulatorGainLabel, "MOD GAIN", audioProcessor.modulatorGain);
    setupSlider(carrierGainSlider, carrierGainLabel, "CARRIER", audioProcessor.carrierGain);
    setupSlider(dryWetSlider, dryWetLabel, "DRY/WET", audioProcessor.dryWet);

    // --- FM Type Combo Box (Algorithm Auswahl) ---
    addAndMakeVisible(fmTypeCombo);
    fmTypeCombo.addItem("Phase Mod (FM)", 1);
    fmTypeCombo.addItem("Ring Mod (AM)", 2);
    fmTypeCombo.setSelectedId(audioProcessor.fmType->getIndex() + 1);

    fmTypeCombo.setColour(juce::ComboBox::backgroundColourId, dx7Darker);
    fmTypeCombo.setColour(juce::ComboBox::textColourId, dx7Cyan);
    fmTypeCombo.setColour(juce::ComboBox::outlineColourId, dx7Cyan.withAlpha(0.5f));

    fmTypeCombo.onChange = [this] { *audioProcessor.fmType = fmTypeCombo.getSelectedId() - 1; };

    addAndMakeVisible(fmTypeLabel);
    fmTypeLabel.setText("ALGORITHM", juce::dontSendNotification);
    fmTypeLabel.setJustificationType(juce::Justification::right);
    fmTypeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    fmTypeLabel.attachToComponent(&fmTypeCombo, true);

    // --- Info Label ---
    addAndMakeVisible(infoLabel);
    infoLabel.setText("CARRIER: Track Input | MODULATOR: Sidechain Input", juce::dontSendNotification);
    infoLabel.setJustificationType(juce::Justification::centred);
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    infoLabel.setFont(juce::Font(12.0f, juce::Font::italic));

    startTimerHz(30);
}

// DER FEHLENDE DESTRUKTOR:
AnyFMAudioProcessorEditor::~AnyFMAudioProcessorEditor()
{
    stopTimer();
}

void AnyFMAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto dx7Cyan = juce::Colour(0xFF00E5E5);
    const auto dx7Brown = juce::Colour(0xFF2B4B4B);
    const auto dx7Header = juce::Colour(0xFF1AAAAA);

    // Hintergrund (DX7 Braun)
    g.fillAll(dx7Brown);

    auto bounds = getLocalBounds();

    // Header Hintergrund
    g.setColour(dx7Header);
    g.fillRect(bounds.removeFromTop(60));

    // Cyanfarbene Trennlinie
    g.setColour(dx7Cyan);
    g.fillRect(0, 58, getWidth(), 2);

    // Titel
    g.setColour(dx7Cyan);
    g.setFont(juce::Font(32.0f, juce::Font::bold));
    g.drawText("anyFM", 25, 12, 200, 40, juce::Justification::left);

    // Subtitle
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(14.0f));
    g.drawText("DIGITAL SIDECHAIN MODULATOR", 150, 22, 300, 20, juce::Justification::left);

}

void AnyFMAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(80); // Platz für Header & Label-Abstand

    auto footerArea = area.removeFromBottom(40);
    infoLabel.setBounds(footerArea);

    auto contentArea = area.reduced(20);

    // Modus Auswahl oben rechts
    auto topRow = contentArea.removeFromTop(40);
    fmTypeCombo.setBounds(topRow.removeFromRight(200).reduced(0, 5));

    contentArea.removeFromTop(20);

    // FlexBox für die 4 cyanfarbenen Wheels
    juce::FlexBox flex;
    flex.flexWrap = juce::FlexBox::Wrap::noWrap;
    flex.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
    flex.alignContent = juce::FlexBox::AlignContent::center;

    flex.items.add(juce::FlexItem(modulationIndexSlider).withFlex(1.0f).withMargin(10));
    flex.items.add(juce::FlexItem(carrierGainSlider).withFlex(1.0f).withMargin(10));
    flex.items.add(juce::FlexItem(modulatorGainSlider).withFlex(1.0f).withMargin(10));
    flex.items.add(juce::FlexItem(dryWetSlider).withFlex(1.0f).withMargin(10));

    flex.performLayout(contentArea);
}

void AnyFMAudioProcessorEditor::timerCallback()
{
    // Synchronisation mit den Parametern (falls DAW automatisiert)
    if (!modulationIndexSlider.isMouseButtonDown())
        modulationIndexSlider.setValue(audioProcessor.modulationIndex->get(), juce::dontSendNotification);

    if (!modulatorGainSlider.isMouseButtonDown())
        modulatorGainSlider.setValue(audioProcessor.modulatorGain->get(), juce::dontSendNotification);

    if (!carrierGainSlider.isMouseButtonDown())
        carrierGainSlider.setValue(audioProcessor.carrierGain->get(), juce::dontSendNotification);

    if (!dryWetSlider.isMouseButtonDown())
        dryWetSlider.setValue(audioProcessor.dryWet->get(), juce::dontSendNotification);

    int expectedId = audioProcessor.fmType->getIndex() + 1;
    if (fmTypeCombo.getSelectedId() != expectedId && !fmTypeCombo.isPopupActive())
        fmTypeCombo.setSelectedId(expectedId, juce::dontSendNotification);
}