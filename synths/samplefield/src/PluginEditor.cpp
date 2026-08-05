#include "PluginEditor.h"
#include "PluginProcessor.h"

static juce::Colour fromU32(uint32_t c) { return juce::Colour(c); }

//==============================================================================
SampleFieldLookAndFeel::SampleFieldLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, fromU32(colCyan));
    setColour(juce::Slider::rotarySliderFillColourId, fromU32(colCyanLight));
    setColour(juce::Slider::rotarySliderOutlineColourId, fromU32(colKnobTrack));
    setColour(juce::Label::textColourId, fromU32(colText));
    setColour(juce::TextButton::buttonColourId, fromU32(colCyan));
    setColour(juce::TextButton::buttonOnColourId, fromU32(colCyanDark));
    setColour(juce::TextButton::textColourOffId, fromU32(colSurface));
    setColour(juce::TextButton::textColourOnId, fromU32(colSurface));
}

void SampleFieldLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float pos, float startAngle, float endAngle, juce::Slider& /*slider*/)
{
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float radius = juce::jmin(w, h) * 0.5f - 4.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::ColourGradient glow(fromU32(colCyanLight).withAlpha(0.45f), cx, cy,
        fromU32(colCyanLight).withAlpha(0.0f), cx, cy + radius * 1.25f, true);
    g.setGradientFill(glow);
    g.fillEllipse(cx - radius - 5, cy - radius - 5, (radius + 5) * 2.0f, (radius + 5) * 2.0f);

    juce::Path trackPath;
    trackPath.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(fromU32(colKnobTrack));
    g.strokePath(trackPath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        juce::Path fillPath;
        fillPath.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, angle, true);
        juce::ColourGradient fg(fromU32(colCyanDark), cx, cy - radius, fromU32(colCyan), cx, cy + radius, false);
        g.setGradientFill(fg);
        g.strokePath(fillPath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    {
        juce::ColourGradient body(fromU32(colSurface), cx - radius * 0.3f, cy - radius * 0.3f,
            fromU32(colCyanLight).brighter(0.6f), cx + radius * 0.3f, cy + radius * 0.3f, false);
        g.setGradientFill(body);
        g.fillEllipse(cx - radius * 0.78f, cy - radius * 0.78f, radius * 1.56f, radius * 1.56f);
    }

    g.setColour(fromU32(colCyan).withAlpha(0.4f));
    g.drawEllipse(cx - radius * 0.78f, cy - radius * 0.78f, radius * 1.56f, radius * 1.56f, 1.0f);

    const float thumbLen = radius * 0.55f;
    const float tx = cx + std::sin(angle) * thumbLen;
    const float ty = cy - std::cos(angle) * thumbLen;
    g.setColour(fromU32(colCyan));
    g.drawLine(cx, cy, tx, ty, 2.5f);
    g.setColour(fromU32(colCyanDark));
    g.fillEllipse(tx - 3.5f, ty - 3.5f, 7.0f, 7.0f);
}

void SampleFieldLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
    const juce::Colour& /*bg*/, bool isOver, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const float cr = 8.0f;

    g.setColour(fromU32(colCyanDark).withAlpha(0.25f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 2.0f), cr);

    juce::Colour top, bot;
    if (isDown) { top = fromU32(colCyanDark); bot = fromU32(colCyan); }
    else if (isOver) { top = fromU32(colCyan).brighter(0.15f); bot = fromU32(colCyanDark).brighter(0.1f); }
    else { top = fromU32(colCyan); bot = fromU32(colCyanDark); }

    juce::ColourGradient grad(top, 0.0f, bounds.getY(), bot, 0.0f, bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, cr);

    juce::ColourGradient shine(juce::Colours::white.withAlpha(isDown ? 0.05f : 0.20f),
        bounds.getCentreX(), bounds.getY(), juce::Colours::white.withAlpha(0.0f), bounds.getCentreX(), bounds.getCentreY(), false);
    g.setGradientFill(shine);
    g.fillRoundedRectangle(bounds, cr);

    g.setColour(fromU32(colCyanDark).withAlpha(0.6f));
    g.drawRoundedRectangle(bounds, cr, 1.0f);
}

void SampleFieldLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isOver*/, bool isDown)
{
    g.setFont(juce::Font("Arial", 11.5f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(isDown ? 0.85f : 1.0f));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

juce::Font SampleFieldLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font("Arial", 10.0f, juce::Font::plain);
}

//==============================================================================
LabelledKnob::LabelledKnob(const juce::String& labelText, const juce::String& parameterID, SampleFieldLookAndFeel& laf)
    : paramID(parameterID)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setLookAndFeel(&laf);
    slider.addListener(this);
    addAndMakeVisible(slider);

    nameLabel.setText(labelText, juce::dontSendNotification);
    nameLabel.setFont(juce::Font("Arial", 9.5f, juce::Font::bold));
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, fromU32(SampleFieldLookAndFeel::colTextMid));
    addAndMakeVisible(nameLabel);

    valueLabel.setFont(juce::Font("Arial", 9.0f, juce::Font::plain));
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setColour(juce::Label::textColourId, fromU32(SampleFieldLookAndFeel::colCyanDark));
    valueLabel.setColour(juce::Label::backgroundColourId, fromU32(SampleFieldLookAndFeel::colKnobTrack));
    valueLabel.setColour(juce::Label::outlineColourId, fromU32(SampleFieldLookAndFeel::colCyanLight));
    addAndMakeVisible(valueLabel);

    updateValueLabel();
}

LabelledKnob::~LabelledKnob() { slider.removeListener(this); }
void LabelledKnob::sliderValueChanged(juce::Slider*) { updateValueLabel(); }

void LabelledKnob::updateValueLabel()
{
    valueLabel.setText(formatParamValue(paramID, (float)slider.getValue()), juce::dontSendNotification);
}

void LabelledKnob::resized()
{
    auto b = getLocalBounds();
    const int nameLabelH = 15;
    const int valueLabelH = 16;
    const int valuePadH = 2;

    nameLabel.setBounds(b.removeFromTop(nameLabelH));
    auto valueArea = b.removeFromBottom(valueLabelH);
    valueArea.reduce(8, valuePadH);
    valueLabel.setBounds(valueArea);
    slider.setBounds(b);
}

void LabelledKnob::paint(juce::Graphics& g)
{
    auto vb = valueLabel.getBounds().toFloat();
    g.setColour(fromU32(SampleFieldLookAndFeel::colKnobTrack));
    g.fillRoundedRectangle(vb, 4.0f);
    g.setColour(fromU32(SampleFieldLookAndFeel::colCyanLight));
    g.drawRoundedRectangle(vb, 4.0f, 0.8f);
}

//==============================================================================
void SectionHeader::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(fromU32(SampleFieldLookAndFeel::colCyan).withAlpha(0.5f));
    g.drawLine(b.getX(), b.getCentreY(), b.getRight(), b.getCentreY(), 1.0f);

    juce::Font f("Arial", 9.0f, juce::Font::bold);
    float tw = f.getStringWidthFloat(title) + 14.0f;
    float tx = b.getCentreX() - tw * 0.5f;
    g.setColour(fromU32(SampleFieldLookAndFeel::colBackground));
    g.fillRect(tx, 0.0f, tw, b.getHeight());

    g.setFont(f);
    g.setColour(fromU32(SampleFieldLookAndFeel::colTextMid));
    g.drawText(title, b, juce::Justification::centred, false);
}

//==============================================================================
TempoLockButton::TempoLockButton() { setRepaintsOnMouseActivity(true); }
void TempoLockButton::setCurrentSteps(int s) { currentSteps = juce::jlimit(0, 8, s); repaint(); }

juce::String TempoLockButton::labelText() const
{
    if (currentSteps == 0) return "TEMPO LOCK: OFF";
    return "TEMPO: " + juce::String(currentSteps) + "/8";
}

void TempoLockButton::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    const float cr = 6.0f;
    const bool active = (currentSteps > 0);

    g.setColour(fromU32(SampleFieldLookAndFeel::colCyanDark).withAlpha(0.18f));
    g.fillRoundedRectangle(b.translated(0.0f, 1.5f), cr);

    if (active)
    {
        juce::ColourGradient grad(fromU32(SampleFieldLookAndFeel::colCyan).brighter(isOver ? 0.15f : 0.0f), 0.0f, b.getY(),
            fromU32(SampleFieldLookAndFeel::colCyanDark), 0.0f, b.getBottom(), false);
        g.setGradientFill(grad);
    }
    else
    {
        g.setColour(isOver ? fromU32(SampleFieldLookAndFeel::colKnobTrack).brighter(0.08f) : fromU32(SampleFieldLookAndFeel::colKnobTrack));
    }
    g.fillRoundedRectangle(b, cr);

    g.setColour(active ? fromU32(SampleFieldLookAndFeel::colCyanDark).withAlpha(0.7f) : fromU32(SampleFieldLookAndFeel::colCyanLight).withAlpha(0.9f));
    g.drawRoundedRectangle(b, cr, 1.0f);

    g.setFont(juce::Font("Arial", 9.5f, juce::Font::bold));
    g.setColour(active ? juce::Colours::white : fromU32(SampleFieldLookAndFeel::colTextMid));
    g.drawText(labelText(), getLocalBounds(), juce::Justification::centred, false);
}

void TempoLockButton::mouseDown(const juce::MouseEvent&)
{
    currentSteps = (currentSteps + 1) % 9;
    repaint();
    if (onStepChanged) onStepChanged(currentSteps);
}

//==============================================================================
SampleFieldAudioProcessorEditor::SampleFieldAudioProcessorEditor(SampleFieldAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&laf);

    addAndMakeVisible(loadButton);
    addAndMakeVisible(unloadButton);
    loadButton.setLookAndFeel(&laf);
    unloadButton.setLookAndFeel(&laf);

    loadButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Load samples", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

            fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                [this](const juce::FileChooser& fc)
                {
                    juce::Array<juce::File> files;
                    for (const auto& r : fc.getResults()) files.add(r);
                    if (!files.isEmpty())
                    {
                        audioProcessor.loadSamples(files);
                        updateSampleCountLabel();
                    }
                });
        };

    unloadButton.onClick = [this]
        {
            audioProcessor.unloadAllSamples();
            updateSampleCountLabel();
        };

    sampleCountLabel.setFont(juce::Font("Arial", 10.0f, juce::Font::plain));
    sampleCountLabel.setColour(juce::Label::textColourId, fromU32(SampleFieldLookAndFeel::colTextMid));
    sampleCountLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(sampleCountLabel);
    updateSampleCountLabel();

    // Global
    addAndMakeVisible(knobPan);
    addAndMakeVisible(knobRate);
    addAndMakeVisible(knobVol);
    addAndMakeVisible(knobTime);

    tempoLockBtn.setCurrentSteps(audioProcessor.getTempoLock());
    tempoLockBtn.onStepChanged = [this](int steps) { audioProcessor.setTempoLock(steps); };
    addAndMakeVisible(tempoLockBtn);

    // Randomisation
    addAndMakeVisible(knobPanRnd);
    addAndMakeVisible(knobRateRnd);
    addAndMakeVisible(knobVolRnd);
    addAndMakeVisible(knobSkip);

    // Delay
    addAndMakeVisible(knobDelayTime);
    addAndMakeVisible(knobDelayVol);
    addAndMakeVisible(knobDelayProb);

    delayTempoLockBtn.setCurrentSteps(audioProcessor.getDelayTempoLock());
    delayTempoLockBtn.onStepChanged = [this](int steps) { audioProcessor.setDelayTempoLock(steps); };
    addAndMakeVisible(delayTempoLockBtn);

    addAndMakeVisible(headerGlobal);
    addAndMakeVisible(headerRandom);
    addAndMakeVisible(headerDelay);

    // Attachments
    attPan = std::make_unique<SliderAttachment>(audioProcessor.apvts, "pan", knobPan.slider);
    attRate = std::make_unique<SliderAttachment>(audioProcessor.apvts, "rate", knobRate.slider);
    attVol = std::make_unique<SliderAttachment>(audioProcessor.apvts, "vol", knobVol.slider);
    attTime = std::make_unique<SliderAttachment>(audioProcessor.apvts, "time", knobTime.slider);
    attPanRnd = std::make_unique<SliderAttachment>(audioProcessor.apvts, "panRnd", knobPanRnd.slider);
    attRateRnd = std::make_unique<SliderAttachment>(audioProcessor.apvts, "rateRnd", knobRateRnd.slider);
    attVolRnd = std::make_unique<SliderAttachment>(audioProcessor.apvts, "volRnd", knobVolRnd.slider);
    attSkip = std::make_unique<SliderAttachment>(audioProcessor.apvts, "skip", knobSkip.slider);

    attDelayTime = std::make_unique<SliderAttachment>(audioProcessor.apvts, "delayTime", knobDelayTime.slider);
    attDelayVol = std::make_unique<SliderAttachment>(audioProcessor.apvts, "delayVol", knobDelayVol.slider);
    attDelayProb = std::make_unique<SliderAttachment>(audioProcessor.apvts, "delayProb", knobDelayProb.slider);

    setSize(480, 560); // Scaled vertically up to cleanly contain three rows
    startTimerHz(4);
}

SampleFieldAudioProcessorEditor::~SampleFieldAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SampleFieldAudioProcessorEditor::timerCallback() { updateSampleCountLabel(); }

void SampleFieldAudioProcessorEditor::updateSampleCountLabel()
{
    int n = audioProcessor.getNumLoadedSamples();
    sampleCountLabel.setText(juce::String(n) + " / 128 samples", juce::dontSendNotification);
}

void SampleFieldAudioProcessorEditor::paintBackground(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient bg(fromU32(0xFFE4F7FB), b.getCentreX(), 0.0f, fromU32(0xFFCCEFF7), b.getCentreX(), b.getBottom(), false);
    g.setGradientFill(bg);
    g.fillAll();

    juce::ColourGradient radial(juce::Colours::white.withAlpha(0.55f), b.getCentreX(), b.getY() + 40.0f,
        juce::Colours::white.withAlpha(0.0f), b.getCentreX(), b.getY() + 200.0f, true);
    g.setGradientFill(radial);
    g.fillAll();

    const float margin = 10.0f;
    auto card = b.reduced(margin);
    g.setColour(juce::Colours::white.withAlpha(0.70f));
    g.fillRoundedRectangle(card, 14.0f);

    g.setColour(fromU32(SampleFieldLookAndFeel::colCyanLight).withAlpha(0.8f));
    g.drawRoundedRectangle(card, 14.0f, 1.2f);

    juce::ColourGradient gloss(juce::Colours::white.withAlpha(0.45f), b.getCentreX(), card.getY(),
        juce::Colours::white.withAlpha(0.0f), b.getCentreX(), card.getY() + card.getHeight() * 0.4f, false);
    g.setGradientFill(gloss);
    g.fillRoundedRectangle(card, 14.0f);
}

void SampleFieldAudioProcessorEditor::paint(juce::Graphics& g)
{
    paintBackground(g);
    g.setFont(juce::Font("Arial", 18.0f, juce::Font::bold));
    juce::ColourGradient nameGrad(fromU32(SampleFieldLookAndFeel::colCyan), 0.0f, 28.0f, fromU32(SampleFieldLookAndFeel::colCyanDark), 0.0f, 46.0f, false);
    g.setGradientFill(nameGrad);
    g.drawText("S A M P L E F I E L D", getLocalBounds().withHeight(56), juce::Justification::centred, false);
}

//==============================================================================
void SampleFieldAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(18);

    // Header Title Space
    area.removeFromTop(45);

    // Actions Button Bar Row
    auto btnRow = area.removeFromTop(34);
    loadButton.setBounds(btnRow.removeFromLeft(140));
    btnRow.removeFromLeft(8);
    unloadButton.setBounds(btnRow.removeFromLeft(140));
    btnRow.removeFromLeft(8);
    sampleCountLabel.setBounds(btnRow);

    area.removeFromTop(12);

    const int sectionH = 14;
    const int knobH = 100;
    const int knobW = 78;
    const int knobGapX = 12;
    const int rowGapY = 16;
    const int tempoLockH = 20;

    const int totalW = 4 * knobW + 3 * knobGapX;
    const int startX = area.getX() + (area.getWidth() - totalW) / 2;

    // ---- ROW 1: GLOBAL ----
    headerGlobal.setBounds(area.removeFromTop(sectionH));
    area.removeFromTop(4);
    int currentY = area.getY();

    knobPan.setBounds(startX, currentY, knobW, knobH);
    knobRate.setBounds(startX + (knobW + knobGapX), currentY, knobW, knobH);
    knobVol.setBounds(startX + 2 * (knobW + knobGapX), currentY, knobW, knobH);
    knobTime.setBounds(startX + 3 * (knobW + knobGapX), currentY, knobW, knobH);
    tempoLockBtn.setBounds(startX + 3 * (knobW + knobGapX), currentY + knobH + 2, knobW, tempoLockH);

    area.removeFromTop(knobH + tempoLockH + rowGapY);

    // ---- ROW 2: RANDOM ----
    headerRandom.setBounds(area.removeFromTop(sectionH));
    area.removeFromTop(4);
    currentY = area.getY();

    knobPanRnd.setBounds(startX, currentY, knobW, knobH);
    knobRateRnd.setBounds(startX + (knobW + knobGapX), currentY, knobW, knobH);
    knobVolRnd.setBounds(startX + 2 * (knobW + knobGapX), currentY, knobW, knobH);
    knobSkip.setBounds(startX + 3 * (knobW + knobGapX), currentY, knobW, knobH);

    area.removeFromTop(knobH + rowGapY);

    // ---- ROW 3: DELAY ----
    headerDelay.setBounds(area.removeFromTop(sectionH));
    area.removeFromTop(4);
    currentY = area.getY();

    knobDelayTime.setBounds(startX, currentY, knobW, knobH);
    delayTempoLockBtn.setBounds(startX, currentY + knobH + 2, knobW, tempoLockH);
    knobDelayVol.setBounds(startX + (knobW + knobGapX), currentY, knobW, knobH);
    knobDelayProb.setBounds(startX + 2 * (knobW + knobGapX), currentY, knobW, knobH);
}