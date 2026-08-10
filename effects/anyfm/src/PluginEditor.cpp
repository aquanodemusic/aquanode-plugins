#include "PluginProcessor.h"
#include "PluginEditor.h"

AnyFMAudioProcessorEditor::AnyFMAudioProcessorEditor(AnyFMAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(680, 370);

    const auto dx7Cyan   = juce::Colour(0xFF00E5E5);
    const auto dx7Darker = juce::Colour(0xFF1A1A1A);

    // ---- Rotary slider setup helper ----
    auto setupSlider = [&](juce::Slider& slider, juce::Label& label, const juce::String& name)
    {
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        slider.setColour(juce::Slider::rotarySliderFillColourId,    dx7Cyan);
        slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::black);
        slider.setColour(juce::Slider::thumbColourId,               juce::Colours::white);
        slider.setColour(juce::Slider::textBoxTextColourId,         juce::Colours::white);
        slider.setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentWhite);

        addAndMakeVisible(label);
        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.setFont(juce::Font(14.0f, juce::Font::bold));
        label.attachToComponent(&slider, false);
    };

    setupSlider(modulationIndexSlider, modulationIndexLabel, "MOD DEPTH");
    setupSlider(modulatorGainSlider,   modulatorGainLabel,   "MOD GAIN");
    setupSlider(carrierGainSlider,     carrierGainLabel,     "CARRIER");
    setupSlider(dryWetSlider,          dryWetLabel,          "DRY/WET");

    // ---- Slider attachments – handles two-way sync + gesture tracking ----
    auto& apvts = audioProcessor.apvts;
    modIndexAttachment = std::make_unique<SliderAttachment>(apvts, "modIndex",    modulationIndexSlider);
    modGainAttachment  = std::make_unique<SliderAttachment>(apvts, "modGain",     modulatorGainSlider);
    carGainAttachment  = std::make_unique<SliderAttachment>(apvts, "carGain",     carrierGainSlider);
    dryWetAttachment   = std::make_unique<SliderAttachment>(apvts, "dryWet",      dryWetSlider);

    // ---- ComboBox style helper ----
    auto styleCombo = [&](juce::ComboBox& combo)
    {
        combo.setColour(juce::ComboBox::backgroundColourId, dx7Darker);
        combo.setColour(juce::ComboBox::textColourId,       dx7Cyan);
        combo.setColour(juce::ComboBox::outlineColourId,    dx7Cyan.withAlpha(0.5f));
        combo.setColour(juce::ComboBox::arrowColourId,      dx7Cyan);
    };

    // ---- Algorithm combo ----
    // Items must be added BEFORE creating the ComboBoxAttachment
    addAndMakeVisible(fmTypeCombo);
    fmTypeCombo.addItem("Phase Mod (FM)",          1);
    fmTypeCombo.addItem("Ring Mod (AM)",            2);
    fmTypeCombo.addItem("Feedback (Experimental)", 3);
    styleCombo(fmTypeCombo);
    fmTypeAttachment = std::make_unique<ComboAttachment>(apvts, "fmType", fmTypeCombo);

    addAndMakeVisible(fmTypeLabel);
    fmTypeLabel.setText("ALGORITHM", juce::dontSendNotification);
    fmTypeLabel.setJustificationType(juce::Justification::right);
    fmTypeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    fmTypeLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    fmTypeLabel.attachToComponent(&fmTypeCombo, true);

    // ---- Routing combo ----
    addAndMakeVisible(routingCombo);
    routingCombo.addItem("Main to Main (Self-FM)",              1);
    routingCombo.addItem("Main to Sidechain (Main mods SC)",    2);
    routingCombo.addItem("Sidechain to Sidechain (SC Self-FM)", 3);
    routingCombo.addItem("Sidechain to Main (SC mods Main)",    4);
    styleCombo(routingCombo);
    routingAttachment = std::make_unique<ComboAttachment>(apvts, "routingMode", routingCombo);

    addAndMakeVisible(routingLabel);
    routingLabel.setText("ROUTING", juce::dontSendNotification);
    routingLabel.setJustificationType(juce::Justification::right);
    routingLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    routingLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    routingLabel.attachToComponent(&routingCombo, true);

    // ---- Info footer ----
    addAndMakeVisible(infoLabel);
    infoLabel.setJustificationType(juce::Justification::centred);
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    infoLabel.setFont(juce::Font(12.0f, juce::Font::italic));

    startTimerHz(30);
}

AnyFMAudioProcessorEditor::~AnyFMAudioProcessorEditor()
{
    stopTimer();
}

// ---------------------------------------------------------------------------
void AnyFMAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto dx7Cyan   = juce::Colour(0xFF00E5E5);
    const auto dx7Brown  = juce::Colour(0xFF2B4B4B);
    const auto dx7Header = juce::Colour(0xFF1AAAAA);

    g.fillAll(dx7Brown);

    auto bounds = getLocalBounds();
    g.setColour(dx7Header);
    g.fillRect(bounds.removeFromTop(60));

    g.setColour(dx7Cyan);
    g.fillRect(0, 58, getWidth(), 2);

    g.setColour(dx7Cyan);
    g.setFont(juce::Font(32.0f, juce::Font::bold));
    g.drawText("anyFM", 25, 12, 200, 40, juce::Justification::left);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(14.0f));
    g.drawText("SIDECHAIN FM MODULATOR", 150, 22, 320, 20, juce::Justification::left);
}

// ---------------------------------------------------------------------------
void AnyFMAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(60);

    auto footer = area.removeFromBottom(40);
    infoLabel.setBounds(footer);

    auto content = area.reduced(20);

    // ---------------------------------------------------------------
    // ROW 1: ROUTING (left half) | ALGORITHM (right half), side by side
    // ---------------------------------------------------------------
    const int labelW = 90;
    const int comboH = 28;

    content.removeFromTop(20);

    auto comboRow = content.removeFromTop(comboH);
    auto routHalf = comboRow.removeFromLeft(comboRow.getWidth() / 2);
    auto algoHalf = comboRow;

    routingCombo.setBounds(routHalf.withTrimmedLeft(labelW).reduced(4, 0));
    fmTypeCombo .setBounds(algoHalf.withTrimmedLeft(labelW).reduced(4, 0));

    // ---------------------------------------------------------------
    // ROW 2: four main rotary knobs
    // ---------------------------------------------------------------
    content.removeFromTop(28);

    juce::FlexBox flex;
    flex.flexWrap       = juce::FlexBox::Wrap::noWrap;
    flex.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
    flex.alignContent   = juce::FlexBox::AlignContent::center;

    flex.items.add(juce::FlexItem(modulationIndexSlider).withFlex(1.0f).withMargin(10));
    flex.items.add(juce::FlexItem(carrierGainSlider)    .withFlex(1.0f).withMargin(10));
    flex.items.add(juce::FlexItem(modulatorGainSlider)  .withFlex(1.0f).withMargin(10));
    flex.items.add(juce::FlexItem(dryWetSlider)         .withFlex(1.0f).withMargin(10));

    flex.performLayout(content);
}

// ---------------------------------------------------------------------------
static juce::String buildInfoText(juce::AudioProcessorValueTreeState& apvts)
{
    const int routing = (int) apvts.getRawParameterValue("routingMode")->load();
    const int fmMode  = (int) apvts.getRawParameterValue("fmType")     ->load();

    juce::String carrier, modulator;
    switch (routing)
    {
        case 0:  carrier = "Main";      modulator = "Main";      break;
        case 1:  carrier = "Sidechain"; modulator = "Main";      break;
        case 2:  carrier = "Sidechain"; modulator = "Sidechain"; break;
        default: carrier = "Main";      modulator = "Sidechain"; break;
    }
    if (fmMode == 2) modulator = "Self (feedback)";

    return "CARRIER: " + carrier + "  |  MODULATOR: " + modulator;
}

// ---------------------------------------------------------------------------
// Timer is now only used for the info-footer label – all param sync is handled
// by the APVTS attachments, so no polling of sliders or combos needed.
void AnyFMAudioProcessorEditor::timerCallback()
{
    infoLabel.setText(buildInfoText(audioProcessor.apvts), juce::dontSendNotification);
}
