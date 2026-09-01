#include "PluginEditor.h"

//==============================================================================
// FullFilterEditorContent
//==============================================================================
FullFilterEditorContent::FullFilterEditorContent(FullFilterAudioProcessor& p)
    : processor(p),
    visualizer(p),
    volumeBarEditor(p, BellBarEditor::Mode::Volume),
    freqBarEditor(p, BellBarEditor::Mode::Frequency)
{
    addAndMakeVisible(visualizer);
    addAndMakeVisible(volumeBarEditor);
    addAndMakeVisible(freqBarEditor);

    // Fresh sky blue & cyan color palette
    const juce::Colour electricBlue(0xff00d2ff);
    const juce::Colour glowBlue(0xff38bdf8);
    const juce::Colour darkTealBg(0xff092d2b);

    squareWaveButton.setClickingTogglesState(true);
    squareWaveButton.setToggleState(processor.getEvenHarmonicsMuted(), juce::dontSendNotification);
    squareWaveButton.setColour(juce::TextButton::buttonColourId, darkTealBg);
    squareWaveButton.setColour(juce::TextButton::buttonOnColourId, electricBlue);
    squareWaveButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    squareWaveButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    squareWaveButton.onClick = [this] { processor.setEvenHarmonicsMuted(squareWaveButton.getToggleState()); };
    addAndMakeVisible(squareWaveButton);

    resetFreqButton.setColour(juce::TextButton::buttonColourId, darkTealBg);
    resetFreqButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    resetFreqButton.onClick = [this] { processor.resetAllBellFrequencies(); };
    addAndMakeVisible(resetFreqButton);

    selectFreqButton.setClickingTogglesState(true);
    selectFreqButton.setColour(juce::TextButton::buttonColourId, darkTealBg);
    selectFreqButton.setColour(juce::TextButton::buttonOnColourId, glowBlue);
    selectFreqButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    selectFreqButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    selectFreqButton.onClick = [this]
        {
            const bool active = selectFreqButton.getToggleState();
            freqBarEditor.setSelectModeActive(active);
            freqMultiplySlider.setEnabled(active);
        };
    addAndMakeVisible(selectFreqButton);

    freqMultiplySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    freqMultiplySlider.setRange(0.5, 2.0);
    freqMultiplySlider.setNumDecimalPlacesToDisplay(2);
    freqMultiplySlider.setValue(1.0, juce::dontSendNotification);
    freqMultiplySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    freqMultiplySlider.setTextValueSuffix("x");
    freqMultiplySlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff05201e));
    freqMultiplySlider.setColour(juce::Slider::trackColourId, electricBlue);
    freqMultiplySlider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    freqMultiplySlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    freqMultiplySlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    freqMultiplySlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    freqMultiplySlider.setEnabled(false);
    freqMultiplySlider.onDragEnd = [this]
        {
            const float factor = (float)freqMultiplySlider.getValue();
            freqBarEditor.applyFrequencyMultiplierToSelection(factor);
            freqMultiplySlider.setValue(1.0, juce::dontSendNotification);
        };
    addAndMakeVisible(freqMultiplySlider);

    acousticPresetLabel.setText("Acoustic Shape", juce::dontSendNotification);
    acousticPresetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe0ffff));
    acousticPresetLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(acousticPresetLabel);

    acousticPresetCombo.addItem("Beam", 1);
    acousticPresetCombo.addItem("Bell", 2);
    acousticPresetCombo.addItem("Plate", 3);
    acousticPresetCombo.addItem("Vibraphone", 4);
    acousticPresetCombo.addItem("Marimba", 5);
    acousticPresetCombo.setTextWhenNothingSelected("Choose a shape");
    acousticPresetCombo.setColour(juce::ComboBox::backgroundColourId, darkTealBg);
    acousticPresetCombo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    acousticPresetCombo.setColour(juce::ComboBox::outlineColourId, electricBlue.withAlpha(0.5f));
    acousticPresetCombo.setColour(juce::ComboBox::arrowColourId, electricBlue);
    addAndMakeVisible(acousticPresetCombo);

    acousticApplyButton.setColour(juce::TextButton::buttonColourId, darkTealBg);
    acousticApplyButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    acousticApplyButton.onClick = [this]
        {
            using Preset = FullFilterAudioProcessor::AcousticPreset;
            switch (acousticPresetCombo.getSelectedId())
            {
            case 1: processor.applyAcousticPreset(Preset::Beam);       break;
            case 2: processor.applyAcousticPreset(Preset::Bell);       break;
            case 3: processor.applyAcousticPreset(Preset::Plate);      break;
            case 4: processor.applyAcousticPreset(Preset::Vibraphone); break;
            case 5: processor.applyAcousticPreset(Preset::Marimba);    break;
            default: break;
            }
        };
    addAndMakeVisible(acousticApplyButton);

    // Big, hard-to-miss import button - deliberately louder than the rest
    // of the teal/blue palette so it reads as a distinct "load something"
    // action rather than another parameter control.
    wavetableImportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffff8a00));
    wavetableImportButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffa733));
    wavetableImportButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    wavetableImportButton.onClick = [this]
        {
            wavetableChooser = std::make_unique<juce::FileChooser>(
                "Import wavetable...", juce::File(), "*.wav");

            const auto flags = juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles;

            wavetableChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();
                    if (file == juce::File())
                        return; // cancelled

                    if (!processor.loadWavetableFile(file))
                        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            "Wavetable Import", "Couldn't read that file as audio.");
                });
        };
    addAndMakeVisible(wavetableImportButton);

    wavetableModeCombo.addItem("Wavetable to Volume", 1);
    wavetableModeCombo.addItem("Wavetable to Filter Position", 2);
    wavetableModeCombo.addItem("Wavetable to Both", 3);
    wavetableModeCombo.setColour(juce::ComboBox::backgroundColourId, darkTealBg);
    wavetableModeCombo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    wavetableModeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffff8a00).withAlpha(0.6f));
    wavetableModeCombo.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffff8a00));
    addAndMakeVisible(wavetableModeCombo);
    wavetableModeAttachment = std::make_unique<ComboAttachment>(processor.apvts, "wavetableMode", wavetableModeCombo);

    setupKnob(rootSlider, rootLabel, "Root");
    setupKnob(levelSlider, levelLabel, "Level");
    setupKnob(lowpassSlider, lowpassLabel, "Low Pass");
    setupKnob(qSlider, qLabel, "Q");
    setupKnob(amountSlider, amountLabel, "Amount");
    setupKnob(glideSlider, glideLabel, "Glide");
    setupKnob(polyphonySlider, polyphonyLabel, "Polyphony");
    setupKnob(positionSlider, positionLabel, "Position");
    setupKnob(attackSlider, attackLabel, "Attack");
    setupKnob(decaySlider, decayLabel, "Decay");
    setupKnob(sustainSlider, sustainLabel, "Sustain");
    setupKnob(releaseSlider, releaseLabel, "Release");

    rootAttachment = std::make_unique<Attachment>(processor.apvts, "root", rootSlider);
    levelAttachment = std::make_unique<Attachment>(processor.apvts, "level", levelSlider);
    lowpassAttachment = std::make_unique<Attachment>(processor.apvts, "lowpass", lowpassSlider);
    qAttachment = std::make_unique<Attachment>(processor.apvts, "q", qSlider);
    amountAttachment = std::make_unique<Attachment>(processor.apvts, "amount", amountSlider);
    glideAttachment = std::make_unique<Attachment>(processor.apvts, "glide", glideSlider);
    polyphonyAttachment = std::make_unique<Attachment>(processor.apvts, "polyphony", polyphonySlider);
    positionAttachment = std::make_unique<Attachment>(processor.apvts, "wavetablePosition", positionSlider);
    attackAttachment = std::make_unique<Attachment>(processor.apvts, "attack", attackSlider);
    decayAttachment = std::make_unique<Attachment>(processor.apvts, "decay", decaySlider);
    sustainAttachment = std::make_unique<Attachment>(processor.apvts, "sustain", sustainSlider);
    releaseAttachment = std::make_unique<Attachment>(processor.apvts, "release", releaseSlider);

    adsrEnabledButton.setButtonText("ADSR");
    adsrEnabledButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    adsrEnabledButton.setColour(juce::ToggleButton::tickColourId, electricBlue);
    adsrEnabledButton.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colours::white.withAlpha(0.3f));
    addAndMakeVisible(adsrEnabledButton);
    adsrEnabledAttachment = std::make_unique<ButtonAttachment>(processor.apvts, "adsrEnabled", adsrEnabledButton);

    setSize(nativeWidth, nativeHeight);
}

void FullFilterEditorContent::paintOverChildren(juce::Graphics& g)
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();

    // Parent background gradient used to mask the sharp square corners
    juce::ColourGradient bgGradient(
        juce::Colour(0xff22c55e), w * 0.5f, h * 0.4f,
        juce::Colour(0xff064e3b), 0.0f, h, true);
    bgGradient.addColour(0.5, juce::Colour(0xff15803d));

    const juce::Colour sandOutlineColor(0xffe2d5ab);
    const float cornerRadius = 8.0f;
    const float lineThickness = 2.0f;

    auto applyRoundedFrame = [&](juce::Component& comp)
        {
            const auto bounds = comp.getBounds().toFloat();

            // Path representing the 4 exterior corner tips (Full Rect minus Rounded Rect)
            juce::Path cornerMask;
            cornerMask.addRectangle(bounds);
            cornerMask.addRoundedRectangle(bounds, cornerRadius);
            cornerMask.setUsingNonZeroWinding(false); // Even-Odd winding cuts out the rounded center

            // 1. Clip off the sharp black rectangular corners with the background gradient
            g.setGradientFill(bgGradient);
            g.fillPath(cornerMask);

            // 2. Draw the sand-colored outline over the newly rounded frame
            g.setColour(sandOutlineColor);
            g.drawRoundedRectangle(bounds, cornerRadius, lineThickness);
        };

    applyRoundedFrame(visualizer);
    applyRoundedFrame(volumeBarEditor);
    applyRoundedFrame(freqBarEditor);
}

void FullFilterEditorContent::paint(juce::Graphics& g)
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();

    // Fresh radial green gradient background
    juce::ColourGradient bgGradient(
        juce::Colour(0xff22c55e), w * 0.5f, h * 0.4f,
        juce::Colour(0xff064e3b), 0.0f, h, true);
    bgGradient.addColour(0.5, juce::Colour(0xff15803d));

    g.setGradientFill(bgGradient);
    g.fillAll();

    // Soft, blurry blue glow aura in the center
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float rx = w * 0.35f;
    const float ry = h * 0.30f;

    juce::ColourGradient auraGlow(
        juce::Colour(0xff00d2ff).withAlpha(0.18f), cx, cy,
        juce::Colour(0xff00d2ff).withAlpha(0.00f), cx + rx, cy,
        true
    );
    auraGlow.addColour(0.4, juce::Colour(0xff00d2ff).withAlpha(0.07f));

    g.setGradientFill(auraGlow);
    g.fillEllipse(cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);

    // Light cyan title text
    g.setColour(juce::Colour(0xff7dd3fc));
    g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    g.drawFittedText("FullFilter Midi Playable Filter", getLocalBounds().removeFromTop(30),
        juce::Justification::centred, 1);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawFittedText("Volume / Mute  (drag a bar, click twice to reset)", volumeLabelArea, juce::Justification::centredLeft, 1);
    g.drawFittedText("Frequency  (drag a bar, click twice to reset)", freqLabelArea, juce::Justification::centredLeft, 1);
}

void FullFilterEditorContent::setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);

    // Simple, natural green/blue knob: a soft moss-green fill arc with a
    // quiet sky-blue thumb, no extra glow or ornamentation — kept plain so
    // it doesn't compete with the glowing bell displays above it.
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff5cb85c));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha(0.18f));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff6ec6ff));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.85f));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffdff5e6));
    label.attachToComponent(&slider, false);
    addAndMakeVisible(label);
}

void FullFilterEditorContent::resized()
{
    auto area = getLocalBounds().reduced(20, 15);
    area.removeFromTop(30);

    visualizer.setBounds(area.removeFromTop(75));
    area.removeFromTop(15);

    const int leftWidth = 410;
    const int columnGap = 24;

    auto leftColumn = area.removeFromLeft(leftWidth);
    area.removeFromLeft(columnGap);
    auto rightColumn = area;

    volumeLabelArea = leftColumn.removeFromTop(18);
    leftColumn.removeFromTop(2);
    volumeBarEditor.setBounds(leftColumn.removeFromTop(75));
    leftColumn.removeFromTop(8);

    {
        auto squareWaveRow = leftColumn.removeFromTop(24);
        adsrEnabledButton.setBounds(squareWaveRow.removeFromLeft(70));
        squareWaveRow.removeFromLeft(8);
        squareWaveButton.setBounds(squareWaveRow.removeFromLeft(250));
    }
    leftColumn.removeFromTop(10);

    {
        auto controlsRow = leftColumn.removeFromTop(24);
        resetFreqButton.setBounds(controlsRow.removeFromLeft(90));
        controlsRow.removeFromLeft(6);
        selectFreqButton.setBounds(controlsRow.removeFromLeft(70));
        controlsRow.removeFromLeft(8);
        freqMultiplySlider.setBounds(controlsRow);
    }
    leftColumn.removeFromTop(12);

    freqLabelArea = leftColumn.removeFromTop(18);
    leftColumn.removeFromTop(2);
    freqBarEditor.setBounds(leftColumn.removeFromTop(75));
    leftColumn.removeFromTop(10);

    {
        auto acousticRow = leftColumn.removeFromTop(24);
        acousticPresetLabel.setBounds(acousticRow.removeFromLeft(100));
        acousticRow.removeFromLeft(6);
        acousticPresetCombo.setBounds(acousticRow.removeFromLeft(150));
        acousticRow.removeFromLeft(8);
        acousticApplyButton.setBounds(acousticRow.removeFromLeft(80));
    }
    leftColumn.removeFromTop(8);

    // Lower-left corner: big Wavetable Import button + Mode combobox,
    // filling whatever's left of the left column.
    {
        auto wavetableRow = leftColumn.reduced(0, 2);
        wavetableModeCombo.setBounds(wavetableRow.removeFromRight(190));
        wavetableRow.removeFromRight(8);
        wavetableImportButton.setBounds(wavetableRow);
    }

    auto layoutKnobCell = [](juce::Rectangle<int> cell, juce::Slider& slider)
        {
            cell.removeFromTop(18);
            slider.setBounds(cell);
        };

    // ADSR Row (4 knobs: Attack, Decay, Sustain, Release)
    auto adsrRow = rightColumn.removeFromTop(80);
    const int adsrColWidth = adsrRow.getWidth() / 4;
    layoutKnobCell(adsrRow.removeFromLeft(adsrColWidth).reduced(4, 0), attackSlider);
    layoutKnobCell(adsrRow.removeFromLeft(adsrColWidth).reduced(4, 0), decaySlider);
    layoutKnobCell(adsrRow.removeFromLeft(adsrColWidth).reduced(4, 0), sustainSlider);
    layoutKnobCell(adsrRow.reduced(4, 0), releaseSlider);

    rightColumn.removeFromTop(12);

    const int mainRowHeight = (rightColumn.getHeight() - 8) / 2;
    auto mainTopRow = rightColumn.removeFromTop(mainRowHeight);
    rightColumn.removeFromTop(8);
    auto mainBottomRow = rightColumn;

    // Main Top Row (4 knobs: Root, Level, Low Pass, Q)
    const int topKnobWidth = mainTopRow.getWidth() / 4;
    layoutKnobCell(mainTopRow.removeFromLeft(topKnobWidth).reduced(4, 0), rootSlider);
    layoutKnobCell(mainTopRow.removeFromLeft(topKnobWidth).reduced(4, 0), levelSlider);
    layoutKnobCell(mainTopRow.removeFromLeft(topKnobWidth).reduced(4, 0), lowpassSlider);
    layoutKnobCell(mainTopRow.reduced(4, 0), qSlider);

    // Main Bottom Row (4 knobs: Position, Amount, Glide, Polyphony)
    const int bottomKnobWidth = mainBottomRow.getWidth() / 4;
    layoutKnobCell(mainBottomRow.removeFromLeft(bottomKnobWidth).reduced(4, 0), positionSlider);
    layoutKnobCell(mainBottomRow.removeFromLeft(bottomKnobWidth).reduced(4, 0), amountSlider);
    layoutKnobCell(mainBottomRow.removeFromLeft(bottomKnobWidth).reduced(4, 0), glideSlider);
    layoutKnobCell(mainBottomRow.reduced(4, 0), polyphonySlider);
}

//==============================================================================
// FullFilterAudioProcessorEditor
//==============================================================================
FullFilterAudioProcessorEditor::FullFilterAudioProcessorEditor(FullFilterAudioProcessor& p)
    : AudioProcessorEditor(&p), content(p)
{
    addAndMakeVisible(content);

    constrainer.setFixedAspectRatio((double)FullFilterEditorContent::nativeWidth
        / (double)FullFilterEditorContent::nativeHeight);
    constrainer.setSizeLimits(FullFilterEditorContent::nativeWidth / 2,
        FullFilterEditorContent::nativeHeight / 2,
        FullFilterEditorContent::nativeWidth * 2,
        FullFilterEditorContent::nativeHeight * 2);
    setConstrainer(&constrainer);

    setResizable(true, true);
    setSize(FullFilterEditorContent::nativeWidth, FullFilterEditorContent::nativeHeight);
}

void FullFilterAudioProcessorEditor::resized()
{
    const float scaleX = (float)getWidth() / (float)FullFilterEditorContent::nativeWidth;
    const float scaleY = (float)getHeight() / (float)FullFilterEditorContent::nativeHeight;
    const float scale = (scaleX + scaleY) * 0.5f;

    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, FullFilterEditorContent::nativeWidth, FullFilterEditorContent::nativeHeight);
}