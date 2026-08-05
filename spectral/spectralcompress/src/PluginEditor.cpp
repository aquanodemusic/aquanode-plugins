#include "PluginEditor.h"

// ============================================================================
//  Look and feel
// ============================================================================
AquanodeLookAndFeel::AquanodeLookAndFeel(SpectralCompressAudioProcessor& p)
    : proc(p)
{
    refreshColours();
}

void AquanodeLookAndFeel::refreshColours()
{
    setColour(juce::Slider::textBoxTextColourId, proc.textColor);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, proc.accentColor.withAlpha(0.3f));
    setColour(juce::Label::textColourId, proc.textColor);
    setColour(juce::ComboBox::backgroundColourId, proc.backgroundColor.brighter(0.12f));
    setColour(juce::ComboBox::textColourId, proc.textColor);
    setColour(juce::ComboBox::outlineColourId, proc.gridColor);
    setColour(juce::ComboBox::arrowColourId, proc.accentColor);
    setColour(juce::PopupMenu::backgroundColourId, proc.sidebarColor.brighter(0.08f));
    setColour(juce::PopupMenu::textColourId, proc.textColor);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, proc.accentColor.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, proc.backgroundColor.brighter(0.12f));
    setColour(juce::TextButton::buttonOnColourId, proc.accentColor.withAlpha(0.35f));
    setColour(juce::TextButton::textColourOffId, proc.textColor);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

juce::Font AquanodeLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

void AquanodeLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle,
    float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float centreX = bounds.getCentreX();
    const float centreY = bounds.getCentreY();
    const float thickness = juce::jmax(2.5f, radius * 0.20f);
    const float arcRadius = radius - thickness * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Track
    juce::Path track;
    track.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f,
        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(proc.gridColor.withAlpha(0.8f));
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    // Value arc — bipolar controls fill from the centre
    const bool bipolar = (slider.getMinimum() < -0.001 && slider.getMaximum() > 0.001);
    const float originAngle = bipolar
        ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
        : rotaryStartAngle;

    if (std::abs(angle - originAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f,
            juce::jmin(originAngle, angle), juce::jmax(originAngle, angle), true);
        g.setColour(slider.isEnabled() ? proc.accentColor : proc.gridColor);
        g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    // Pointer
    juce::Path pointer;
    const float pointerLength = radius * 0.55f;
    const float pointerThickness = juce::jmax(1.8f, radius * 0.10f);
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + thickness * 0.4f,
        pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(proc.textColor);
    g.fillPath(pointer);

    g.setColour(proc.backgroundColor.brighter(0.06f));
    g.fillEllipse(centreX - arcRadius * 0.42f, centreY - arcRadius * 0.42f,
        arcRadius * 0.84f, arcRadius * 0.84f);
}

void AquanodeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
    const juce::Colour& backgroundColour,
    bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    juce::Colour base = backgroundColour;
    if (button.getToggleState()) base = proc.accentColor.withAlpha(0.30f);
    if (isDown)             base = base.brighter(0.25f);
    else if (isHighlighted) base = base.brighter(0.12f);

    g.setColour(base);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(button.getToggleState() ? proc.accentColor : proc.gridColor);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

void AquanodeLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
    int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(proc.gridColor);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float)width - 14.0f;
    const float cy = (float)height * 0.5f;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 3.0f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(proc.accentColor);
    g.strokePath(arrow, juce::PathStrokeType(1.6f));
}

// ============================================================================
//  Editor construction
// ============================================================================
SpectralCompressAudioProcessorEditor::SpectralCompressAudioProcessorEditor(
    SpectralCompressAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), lookAndFeel(p)
{
    setLookAndFeel(&lookAndFeel);

    curveData.fill(SpectralCompressAudioProcessor::kCurveDefaultDB);
    shiftedCurve.fill(SpectralCompressAudioProcessor::kCurveDefaultDB);

    // ---- headers -------------------------------------------------------
    titleLabel.setText("SPECTRAL COMPRESS", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(19.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, audioProcessor.accentColor);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("aquanode", juce::dontSendNotification);
    subtitleLabel.setFont(juce::Font(12.0f));
    subtitleLabel.setColour(juce::Label::textColourId, audioProcessor.textColor.withAlpha(0.6f));
    addAndMakeVisible(subtitleLabel);

    setupHeader(globalHeader, "GLOBAL");
    setupHeader(downHeader, "DOWNWARDS");
    setupHeader(upHeader, "UPWARDS");
    setupHeader(scHeader, "SIDECHAIN / MORPH");
    setupHeader(curveHeader, "CURVE");

    // ---- combo boxes ----------------------------------------------------
    auto setupCombo = [this](juce::ComboBox& box, juce::Label& label,
        const juce::String& name, const juce::StringArray& items)
        {
            label.setText(name, juce::dontSendNotification);
            label.setFont(juce::Font(11.0f));
            label.setColour(juce::Label::textColourId, audioProcessor.textColor.withAlpha(0.65f));
            addAndMakeVisible(label);

            box.addItemList(items, 1);
            box.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(box);
        };

    visSmoothLabel.setText("VISUAL SMOOTH", juce::dontSendNotification);
    visSmoothLabel.setFont(juce::Font(11.0f));
    visSmoothLabel.setColour(juce::Label::textColourId,
        audioProcessor.textColor.withAlpha(0.65f));
    addAndMakeVisible(visSmoothLabel);

    visSmoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    visSmoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 14);
    visSmoothSlider.setColour(juce::Slider::trackColourId, audioProcessor.accentColor);
    visSmoothSlider.setColour(juce::Slider::backgroundColourId, audioProcessor.gridColor);
    visSmoothSlider.setColour(juce::Slider::thumbColourId, audioProcessor.textColor);
    addAndMakeVisible(visSmoothSlider);
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "visSmooth", visSmoothSlider));

    setupCombo(fftCombo, fftLabel, "FFT SIZE", { "512", "1024", "2048", "4096", "8192" });
    setupCombo(overlapCombo, overlapLabel, "OVERLAP", { "2x", "4x", "8x", "16x" });
    setupCombo(modeCombo, modeLabel, "MODE",
        { "Level Curve", "Sidechain Match", "Sidechain Compress" });

    fftAttachment = std::make_unique<ComboAttachment>(audioProcessor.apvts, "fftSize", fftCombo);
    overlapAttachment = std::make_unique<ComboAttachment>(audioProcessor.apvts, "overlap", overlapCombo);
    modeAttachment = std::make_unique<ComboAttachment>(audioProcessor.apvts, "mode", modeCombo);
    modeCombo.onChange = [this] { repaint(); };

    // ---- knobs ----------------------------------------------------------
    setupKnob(gainKnob, "GAIN", "gain");
    setupKnob(mixKnob, "MIX", "mix");
    setupKnob(attackKnob, "ATTACK", "attack");
    setupKnob(releaseKnob, "RELEASE", "release");

    setupKnob(downOffsetKnob, "OFFSET", "downOffset");
    setupKnob(downRatioKnob, "RATIO", "downRatio");
    setupKnob(downKneeKnob, "KNEE", "downKnee");
    setupKnob(downAmountKnob, "AMOUNT", "downAmount");

    setupKnob(upOffsetKnob, "OFFSET", "upOffset");
    setupKnob(upRatioKnob, "RATIO", "upRatio");
    setupKnob(upKneeKnob, "KNEE", "upKnee");
    setupKnob(upAmountKnob, "AMOUNT", "upAmount");

    setupKnob(matchKnob, "MATCH", "scMorph");
    setupKnob(morphKnob, "MORPH", "morphAmount");
    setupKnob(clarityKnob, "CLARITY", "morphClarity");
    setupKnob(scLinkKnob, "SC LINK", "scLink");
    setupKnob(stereoLinkKnob, "ST LINK", "stereoLink");
    setupKnob(shiftKnob, "SHIFT", "curveShift");

    shiftKnob.slider.onValueChange = [this] { repaint(canvasArea()); };

    downOffsetKnob.slider.setColour(juce::Slider::thumbColourId, audioProcessor.downColor);
    upOffsetKnob.slider.setColour(juce::Slider::thumbColourId, audioProcessor.upColor);

    // ---- buttons --------------------------------------------------------
    auto setupButton = [this](juce::TextButton& b, const juce::String& text, bool toggle)
        {
            b.setButtonText(text);
            b.setClickingTogglesState(toggle);
            addAndMakeVisible(b);
        };

    setupButton(resetCurveButton, "Reset", false);
    setupButton(learnCurveButton, "Learn", false);
    setupButton(tiltDownButton, "Tilt -", false);
    setupButton(tiltUpButton, "Tilt +", false);
    setupButton(showScButton, "Sidechain", true);
    setupButton(showGrButton, "Reduction", true);
    setupButton(showOutButton, "Output", true);
    setupButton(lightModeButton, "Light", true);
    setupButton(deltaButton, "Delta", true);
    deltaAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "delta", deltaButton);
    lightModeButton.setToggleState(audioProcessor.isLightMode(), juce::dontSendNotification);

    showScButton.setToggleState(true, juce::dontSendNotification);
    showGrButton.setToggleState(true, juce::dontSendNotification);
    showOutButton.setToggleState(true, juce::dontSendNotification);

    resetCurveButton.onClick = [this]
        {
            audioProcessor.resetCurve(SpectralCompressAudioProcessor::kCurveDefaultDB);
            repaint();
        };
    learnCurveButton.onClick = [this] { audioProcessor.learnCurveFromInput(0.0f); repaint(); };
    tiltDownButton.onClick = [this] { audioProcessor.tiltCurve(-1.5f); repaint(); };
    tiltUpButton.onClick = [this] { audioProcessor.tiltCurve(1.5f); repaint(); };

    showScButton.onClick = [this] { showSidechain = showScButton.getToggleState(); repaint(); };
    showGrButton.onClick = [this] { showGR = showGrButton.getToggleState(); repaint(); };
    showOutButton.onClick = [this] { showOutput = showOutButton.getToggleState(); repaint(); };
    lightModeButton.onClick = [this]
        {
            audioProcessor.setLightMode(lightModeButton.getToggleState());
            refreshUIColors();
        };

    scStatusLabel.setFont(juce::Font(11.0f));
    scStatusLabel.setColour(juce::Label::textColourId, audioProcessor.textColor.withAlpha(0.6f));
    scStatusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(scStatusLabel);

    setResizable(true, true);
    setResizeLimits(940, 560, 2600, 1600);
    setSize(kBaseWidth, kBaseHeight);

    refreshUIColors();
    startTimerHz(30);
}

SpectralCompressAudioProcessorEditor::~SpectralCompressAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void SpectralCompressAudioProcessorEditor::setupHeader(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setFont(juce::Font(11.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, audioProcessor.accentColor.withAlpha(0.85f));
    addAndMakeVisible(l);
}

void SpectralCompressAudioProcessorEditor::setupKnob(KnobPack& k, const juce::String& name,
    const juce::String& paramID)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 14);
    k.slider.setColour(juce::Slider::textBoxTextColourId, audioProcessor.textColor);
    addAndMakeVisible(k.slider);

    k.label.setText(name, juce::dontSendNotification);
    k.label.setFont(juce::Font(10.0f));
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, audioProcessor.textColor.withAlpha(0.65f));
    addAndMakeVisible(k.label);

    sliderAttachments.push_back(
        std::make_unique<SliderAttachment>(audioProcessor.apvts, paramID, k.slider));
}

std::vector<SpectralCompressAudioProcessorEditor::KnobPack*>
SpectralCompressAudioProcessorEditor::allKnobs()
{
    return { &gainKnob, &mixKnob, &attackKnob, &releaseKnob,
             &downOffsetKnob, &downRatioKnob, &downKneeKnob, &downAmountKnob,
             &upOffsetKnob, &upRatioKnob, &upKneeKnob, &upAmountKnob,
             &matchKnob, &morphKnob, &clarityKnob, &scLinkKnob, &stereoLinkKnob,
             &shiftKnob };
}

void SpectralCompressAudioProcessorEditor::refreshUIColors()
{
    lookAndFeel.refreshColours();

    titleLabel.setColour(juce::Label::textColourId, audioProcessor.accentColor);
    subtitleLabel.setColour(juce::Label::textColourId,
        audioProcessor.textColor.withAlpha(0.6f));

    for (auto* h : { &globalHeader, &downHeader, &upHeader, &scHeader, &curveHeader })
        h->setColour(juce::Label::textColourId, audioProcessor.accentColor.withAlpha(0.85f));

    for (auto* l : { &fftLabel, &overlapLabel, &modeLabel, &visSmoothLabel, &scStatusLabel })
        l->setColour(juce::Label::textColourId, audioProcessor.textColor.withAlpha(0.65f));

    for (auto* k : allKnobs())
    {
        k->label.setColour(juce::Label::textColourId, audioProcessor.textColor.withAlpha(0.65f));
        k->slider.setColour(juce::Slider::textBoxTextColourId, audioProcessor.textColor);
    }

    downOffsetKnob.slider.setColour(juce::Slider::thumbColourId, audioProcessor.downColor);
    upOffsetKnob.slider.setColour(juce::Slider::thumbColourId, audioProcessor.upColor);

    visSmoothSlider.setColour(juce::Slider::trackColourId, audioProcessor.accentColor);
    visSmoothSlider.setColour(juce::Slider::backgroundColourId, audioProcessor.gridColor);
    visSmoothSlider.setColour(juce::Slider::thumbColourId, audioProcessor.textColor);
    visSmoothSlider.setColour(juce::Slider::textBoxTextColourId, audioProcessor.textColor);

    for (auto* b : { &fftCombo, &overlapCombo, &modeCombo })
    {
        b->setColour(juce::ComboBox::backgroundColourId,
            audioProcessor.backgroundColor.brighter(0.12f));
        b->setColour(juce::ComboBox::textColourId, audioProcessor.textColor);
        b->setColour(juce::ComboBox::outlineColourId, audioProcessor.gridColor);
        b->setColour(juce::ComboBox::arrowColourId, audioProcessor.accentColor);
    }

    repaint();
}

// ============================================================================
//  Layout
// ============================================================================
int SpectralCompressAudioProcessorEditor::sidebarWidth() const
{
    return (int)(kSidebarW * scaleFactor);
}

juce::Rectangle<int> SpectralCompressAudioProcessorEditor::canvasArea() const
{
    return getLocalBounds().withTrimmedLeft(sidebarWidth());
}

void SpectralCompressAudioProcessorEditor::resized()
{
    scaleFactor = juce::jlimit(0.85f, 1.6f, (float)getHeight() / (float)kBaseHeight);

    const int pad = (int)(12 * scaleFactor);
    auto sidebar = getLocalBounds().removeFromLeft(sidebarWidth()).reduced(pad, pad);

    const int usableW = sidebar.getWidth();
    const int headerH = (int)(16 * scaleFactor);
    const int gap = (int)(6 * scaleFactor);
    const int comboH = (int)(22 * scaleFactor);
    const int comboLabelH = (int)(13 * scaleFactor);
    const int knobH = (int)(58 * scaleFactor);
    const int knobLabelH = (int)(12 * scaleFactor);
    const int textBoxH = (int)(14 * scaleFactor);
    const int rowH = knobH + knobLabelH + textBoxH;
    const int buttonH = (int)(22 * scaleFactor);

    // ---- title -----------------------------------------------------------
    auto titleRow = sidebar.removeFromTop((int)(24 * scaleFactor));
    titleLabel.setBounds(titleRow.removeFromLeft((int)(usableW * 0.72f)));
    subtitleLabel.setBounds(titleRow);
    titleLabel.setFont(juce::Font(17.0f * scaleFactor, juce::Font::bold));
    subtitleLabel.setFont(juce::Font(11.0f * scaleFactor));
    subtitleLabel.setJustificationType(juce::Justification::centredRight);
    sidebar.removeFromTop(gap);

    auto placeHeader = [&](juce::Label& l)
        {
            l.setBounds(sidebar.removeFromTop(headerH));
            l.setFont(juce::Font(10.5f * scaleFactor, juce::Font::bold));
            sidebar.removeFromTop((int)(3 * scaleFactor));
        };

    // Lays out up to four knobs on one row
    auto placeKnobRow = [&](std::initializer_list<KnobPack*> knobs)
        {
            auto row = sidebar.removeFromTop(rowH);
            const int n = (int)knobs.size();
            const int cellW = usableW / juce::jmax(1, n);
            int i = 0;
            for (auto* k : knobs)
            {
                auto cell = row.withX(row.getX() + i * cellW).withWidth(cellW);
                k->label.setBounds(cell.removeFromTop(knobLabelH));
                k->label.setFont(juce::Font(9.5f * scaleFactor));
                k->slider.setBounds(cell);
                k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                    cellW - (int)(6 * scaleFactor), textBoxH);
                ++i;
            }
            sidebar.removeFromTop(gap);
        };

    // ---- global ----------------------------------------------------------
    placeHeader(globalHeader);
    {
        // Visual smoothing sits right at the top — the analyser can move fast
        auto visRow = sidebar.removeFromTop((int)(18 * scaleFactor));
        visSmoothLabel.setBounds(visRow.removeFromLeft((int)(usableW * 0.36f)));
        visSmoothLabel.setFont(juce::Font(9.5f * scaleFactor));
        visSmoothSlider.setBounds(visRow);
        visSmoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false,
            (int)(40 * scaleFactor), (int)(14 * scaleFactor));
        sidebar.removeFromTop(gap);

        auto row = sidebar.removeFromTop(comboLabelH + comboH);
        const int half = usableW / 2 - gap / 2;
        auto left = row.removeFromLeft(half);
        row.removeFromLeft(gap);
        auto right = row;

        fftLabel.setBounds(left.removeFromTop(comboLabelH));
        fftCombo.setBounds(left);
        overlapLabel.setBounds(right.removeFromTop(comboLabelH));
        overlapCombo.setBounds(right);
        sidebar.removeFromTop(gap);

        auto modeRow = sidebar.removeFromTop(comboLabelH + comboH);
        modeLabel.setBounds(modeRow.removeFromTop(comboLabelH));
        modeCombo.setBounds(modeRow);
        sidebar.removeFromTop(gap);
    }
    placeKnobRow({ &gainKnob, &mixKnob, &attackKnob, &releaseKnob });

    // ---- downwards / upwards --------------------------------------------
    placeHeader(downHeader);
    placeKnobRow({ &downOffsetKnob, &downRatioKnob, &downKneeKnob, &downAmountKnob });

    placeHeader(upHeader);
    placeKnobRow({ &upOffsetKnob, &upRatioKnob, &upKneeKnob, &upAmountKnob });

    // ---- sidechain -------------------------------------------------------
    placeHeader(scHeader);
    placeKnobRow({ &matchKnob, &morphKnob, &clarityKnob, &scLinkKnob, &stereoLinkKnob });
    scStatusLabel.setBounds(sidebar.removeFromTop((int)(14 * scaleFactor)));
    scStatusLabel.setFont(juce::Font(10.0f * scaleFactor));
    sidebar.removeFromTop(gap);

    // ---- curve tools -----------------------------------------------------
    placeHeader(curveHeader);
    {
        // The shift knob sits beside the curve tools rather than on its own row
        auto block = sidebar.removeFromTop(rowH);
        auto knobCell = block.removeFromLeft(usableW / 4);
        block.removeFromLeft(gap);

        shiftKnob.label.setBounds(knobCell.removeFromTop(knobLabelH));
        shiftKnob.label.setFont(juce::Font(9.5f * scaleFactor));
        shiftKnob.slider.setBounds(knobCell);
        shiftKnob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false,
            knobCell.getWidth() - (int)(6 * scaleFactor), textBoxH);

        auto buttons = block.withSizeKeepingCentre(block.getWidth(), 2 * buttonH + gap);
        auto row = buttons.removeFromTop(buttonH);
        const int bw = buttons.getWidth();
        const int cellW = (bw - 3 * gap) / 4;
        resetCurveButton.setBounds(row.removeFromLeft(cellW)); row.removeFromLeft(gap);
        learnCurveButton.setBounds(row.removeFromLeft(cellW)); row.removeFromLeft(gap);
        tiltDownButton.setBounds(row.removeFromLeft(cellW));  row.removeFromLeft(gap);
        tiltUpButton.setBounds(row.removeFromLeft(cellW));

        buttons.removeFromTop(gap);
        auto row2 = buttons.removeFromTop(buttonH);
        const int cellW2 = (bw - 4 * gap) / 5;
        deltaButton.setBounds(row2.removeFromLeft(cellW2));   row2.removeFromLeft(gap);
        showScButton.setBounds(row2.removeFromLeft(cellW2));  row2.removeFromLeft(gap);
        showGrButton.setBounds(row2.removeFromLeft(cellW2));  row2.removeFromLeft(gap);
        showOutButton.setBounds(row2.removeFromLeft(cellW2)); row2.removeFromLeft(gap);
        lightModeButton.setBounds(row2.removeFromLeft(cellW2));
    }
}

// ============================================================================
//  Geometry helpers
// ============================================================================
float SpectralCompressAudioProcessorEditor::hzToX(float hz) const
{
    const auto ca = canvasArea();
    const float lo = std::log10(20.0f);
    const float hi = std::log10(juce::jmax(1000.0f, (float)(audioProcessor.currentSampleRate * 0.5)));
    const float t = (std::log10(juce::jmax(20.0f, hz)) - lo) / (hi - lo);
    return (float)ca.getX() + t * (float)ca.getWidth();
}

float SpectralCompressAudioProcessorEditor::binToX(int bin) const
{
    return hzToX(audioProcessor.binToHz(bin));
}

int SpectralCompressAudioProcessorEditor::xToBin(float x) const
{
    const auto ca = canvasArea();
    const float lo = std::log10(20.0f);
    const float nyq = juce::jmax(1000.0f, (float)(audioProcessor.currentSampleRate * 0.5));
    const float hi = std::log10(nyq);
    const float t = (x - (float)ca.getX()) / (float)juce::jmax(1, ca.getWidth());
    const float hz = std::pow(10.0f, lo + t * (hi - lo));
    const int   bin = juce::roundToInt(hz * (audioProcessor.numBins - 1) / nyq);
    return juce::jlimit(0, audioProcessor.numBins - 1, bin);
}

float SpectralCompressAudioProcessorEditor::dbToY(float dB) const
{
    const auto ca = canvasArea();
    dB = juce::jlimit(kDisplayMinDB, kDisplayMaxDB, dB);
    const float t = (kDisplayMaxDB - dB) / (kDisplayMaxDB - kDisplayMinDB);
    return (float)ca.getY() + t * (float)ca.getHeight();
}

float SpectralCompressAudioProcessorEditor::yToDB(float y) const
{
    const auto ca = canvasArea();
    const float t = (y - (float)ca.getY()) / (float)juce::jmax(1, ca.getHeight());
    return kDisplayMaxDB - t * (kDisplayMaxDB - kDisplayMinDB);
}

float SpectralCompressAudioProcessorEditor::magToDB(float mag) const
{
    return (mag > 0.0f) ? juce::jmax(kDisplayMinDB, 20.0f * std::log10(mag)) : kDisplayMinDB;
}

void SpectralCompressAudioProcessorEditor::buildPathFromBins(juce::Path& path,
    const float* dbValues,
    bool peak) const
{
    const auto ca = canvasArea();
    const int  nb = audioProcessor.numBins;
    const float left = (float)ca.getX();
    const float right = (float)ca.getRight();

    bool  started = false;
    int   curCol = -1;
    float colVal = kDisplayMinDB;

    auto emit = [&](int col, float v)
        {
            const float y = dbToY(v);
            if (!started) { path.startNewSubPath((float)col, y); started = true; }
            else            path.lineTo((float)col, y);
        };

    for (int b = 0; b < nb; ++b)
    {
        const float x = binToX(b);
        if (x < left - 1.0f) continue;
        if (x > right + 1.0f) break;

        const int   col = (int)x;
        const float v = dbValues[b];

        if (col != curCol)
        {
            if (curCol >= 0) emit(curCol, colVal);
            curCol = col;
            colVal = v;
        }
        else
        {
            colVal = peak ? juce::jmax(colVal, v) : v;
        }
    }
    if (curCol >= 0) emit(curCol, colVal);
}

// ============================================================================
//  Timer
// ============================================================================
void SpectralCompressAudioProcessorEditor::timerCallback()
{
    const int nb = audioProcessor.numBins;
    audioProcessor.getInputSpectrum(inputData.data(), nb);
    audioProcessor.getSidechainSpectrum(sidechainData.data(), nb);
    audioProcessor.getGainReduction(grData.data(), nb);
    audioProcessor.getCurveData(curveData.data(), nb);

    // The curve is stored unshifted; everything on screen shows it rotated by
    // the SHIFT knob, which is exactly what the compressor bank reads.
    const int shift = audioProcessor.getCurveShiftBins();
    for (int b = 0; b < nb; ++b)
    {
        int src = b - shift;
        if (src < 0)   src += nb;
        if (src >= nb) src -= nb;
        shiftedCurve[b] = curveData[src];
    }

    const int mode = audioProcessor.getModeValue();
    if (mode == SpectralCompressAudioProcessor::ModeLevel)
        scStatusLabel.setText("Sidechain unused in Level Curve mode", juce::dontSendNotification);
    else if (audioProcessor.sidechainIsConnected())
        scStatusLabel.setText("Sidechain connected", juce::dontSendNotification);
    else
        scStatusLabel.setText("No sidechain - use one in your DAW!", juce::dontSendNotification);

    const bool scUsed = (mode != SpectralCompressAudioProcessor::ModeLevel);
    const float morphAmount = audioProcessor.apvts.getRawParameterValue("morphAmount")->load();

    matchKnob.slider.setEnabled(mode == SpectralCompressAudioProcessor::ModeMatch);
    clarityKnob.slider.setEnabled(morphAmount > 0.001f);
    scLinkKnob.slider.setEnabled(scUsed || morphAmount > 0.001f);

    if (morphAmount > 0.001f && !audioProcessor.sidechainIsConnected())
        scStatusLabel.setText("Morph needs a sidechain - use one in your DAW!",
            juce::dontSendNotification);

    repaint(canvasArea());
}

// ============================================================================
//  Painting
// ============================================================================
void SpectralCompressAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(audioProcessor.backgroundColor);

    drawGrid(g);
    drawGainReduction(g);
    drawSpectra(g);
    drawCurves(g);
    drawLegend(g);
    drawHoverReadout(g);
    drawSidebarBackdrop(g);
}

void SpectralCompressAudioProcessorEditor::drawSidebarBackdrop(juce::Graphics& g)
{
    auto sidebar = getLocalBounds().removeFromLeft(sidebarWidth());
    g.setColour(audioProcessor.sidebarColor);
    g.fillRect(sidebar);
    g.setColour(audioProcessor.gridColor.withAlpha(0.8f));
    g.drawVerticalLine(sidebar.getRight() - 1, 0.0f, (float)getHeight());
}

void SpectralCompressAudioProcessorEditor::drawGrid(juce::Graphics& g)
{
    const auto ca = canvasArea();
    const float nyq = (float)(audioProcessor.currentSampleRate * 0.5);

    static const float freqs[] = { 20, 30, 50, 100, 200, 300, 500, 1000, 2000,
                                   3000, 5000, 10000, 20000 };
    g.setFont(10.0f * scaleFactor);
    for (float f : freqs)
    {
        if (f > nyq) break;
        const float x = hzToX(f);
        if (x < ca.getX() || x > ca.getRight()) continue;
        const bool major = (f == 100 || f == 1000 || f == 10000);
        g.setColour(audioProcessor.gridColor.withAlpha(major ? 0.55f : 0.22f));
        g.drawVerticalLine((int)x, (float)ca.getY(), (float)ca.getBottom());
        if (major)
        {
            const juce::String label = (f >= 1000) ? juce::String(f / 1000.0f, 0) + "k"
                : juce::String((int)f);
            g.setColour(audioProcessor.textColor.withAlpha(0.55f));
            g.drawText(label, (int)x + 3, ca.getBottom() - (int)(16 * scaleFactor),
                40, 14, juce::Justification::left, false);
        }
    }

    for (float db = 0.0f; db >= -96.0f; db -= 12.0f)
    {
        const float y = dbToY(db);
        g.setColour(audioProcessor.gridColor.withAlpha(db == 0.0f ? 0.45f : 0.20f));
        g.drawHorizontalLine((int)y, (float)ca.getX(), (float)ca.getRight());
        g.setColour(audioProcessor.textColor.withAlpha(0.45f));
        g.setFont(9.5f * scaleFactor);
        g.drawText(juce::String((int)db), ca.getX() + 3, (int)y - 11, 40, 12,
            juce::Justification::left, false);
    }
}

void SpectralCompressAudioProcessorEditor::drawSpectra(juce::Graphics& g)
{
    const auto ca = canvasArea();
    const int  nb = audioProcessor.numBins;
    const int  mode = audioProcessor.getModeValue();

    // ---- sidechain ------------------------------------------------------
    if (showSidechain && mode != SpectralCompressAudioProcessor::ModeLevel)
    {
        for (int b = 0; b < nb; ++b) scratchA[b] = magToDB(sidechainData[b]);
        juce::Path scPath;
        buildPathFromBins(scPath, scratchA.data(), true);
        g.setColour(audioProcessor.sidechainColor.withAlpha(0.75f));
        g.strokePath(scPath, juce::PathStrokeType(1.2f));
    }

    // ---- input ----------------------------------------------------------
    for (int b = 0; b < nb; ++b) scratchA[b] = magToDB(inputData[b]);

    juce::Path inPath;
    buildPathFromBins(inPath, scratchA.data(), true);

    juce::Path fill = inPath;
    if (!inPath.isEmpty())
    {
        const auto bounds = inPath.getBounds();
        fill.lineTo(bounds.getRight(), (float)ca.getBottom());
        fill.lineTo(bounds.getX(), (float)ca.getBottom());
        fill.closeSubPath();
        g.setColour(audioProcessor.spectrumColor.withAlpha(0.35f));
        g.fillPath(fill);
    }
    g.setColour(audioProcessor.spectrumColor.brighter(0.5f));
    g.strokePath(inPath, juce::PathStrokeType(1.2f));

    // ---- output (input + gain reduction) --------------------------------
    if (showOutput)
    {
        for (int b = 0; b < nb; ++b)
            scratchB[b] = juce::jlimit(kDisplayMinDB, kDisplayMaxDB,
                magToDB(inputData[b]) + grData[b]);
        juce::Path outPath;
        buildPathFromBins(outPath, scratchB.data(), true);
        g.setColour(audioProcessor.outputColor.withAlpha(0.9f));
        g.strokePath(outPath, juce::PathStrokeType(1.4f));
    }
}

void SpectralCompressAudioProcessorEditor::drawGainReduction(juce::Graphics& g)
{
    if (!showGR) return;

    const auto ca = canvasArea();
    const int  nb = audioProcessor.numBins;

    // Vertical bars from the input level to the compressed level: red where
    // the bin is pushed down, green where it is pushed up.
    for (int b = 0; b < nb; ++b)
    {
        const float gr = grData[b];
        if (std::abs(gr) < 0.15f) continue;

        const float x = binToX(b);
        if (x < ca.getX() || x > ca.getRight()) continue;

        const float inDb = magToDB(inputData[b]);
        if (inDb <= kDisplayMinDB + 1.0f) continue;

        const float y0 = dbToY(inDb);
        const float y1 = dbToY(juce::jlimit(kDisplayMinDB, kDisplayMaxDB, inDb + gr));

        const float width = juce::jmax(1.0f, binToX(b + 1) - x);
        g.setColour((gr < 0.0f ? audioProcessor.downColor : audioProcessor.upColor)
            .withAlpha(0.28f));
        g.fillRect(x, juce::jmin(y0, y1), width, std::abs(y1 - y0));
    }
}

void SpectralCompressAudioProcessorEditor::drawCurves(juce::Graphics& g)
{
    const int  nb = audioProcessor.numBins;
    const int  mode = audioProcessor.getModeValue();

    const float morph = audioProcessor.apvts.getRawParameterValue("scMorph")->load();
    const float downOffset = audioProcessor.apvts.getRawParameterValue("downOffset")->load();
    const float upOffset = audioProcessor.apvts.getRawParameterValue("upOffset")->load();
    const float downRatio = audioProcessor.apvts.getRawParameterValue("downRatio")->load();
    const float upRatio = audioProcessor.apvts.getRawParameterValue("upRatio")->load();

    // The effective target: in Sidechain Match the drawn curve morphs towards
    // the sidechain's own spectrum, so show what the compressors actually see.
    const bool morphing = (mode == SpectralCompressAudioProcessor::ModeMatch)
        && morph > 0.001f && audioProcessor.sidechainIsConnected();

    for (int b = 0; b < nb; ++b)
    {
        float target = shiftedCurve[b];
        if (morphing)
            target = target * (1.0f - morph) + magToDB(sidechainData[b]) * morph;
        scratchA[b] = target;
    }

    // Threshold lines for both directions
    if (downRatio > 1.005f)
    {
        for (int b = 0; b < nb; ++b) scratchB[b] = scratchA[b] + downOffset;
        juce::Path p;
        buildPathFromBins(p, scratchB.data(), false);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, p, dashes, 2);
        g.setColour(audioProcessor.downColor.withAlpha(0.65f));
        g.fillPath(dashed);
    }

    if (upRatio > 1.005f)
    {
        for (int b = 0; b < nb; ++b) scratchB[b] = scratchA[b] + upOffset;
        juce::Path p;
        buildPathFromBins(p, scratchB.data(), false);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, p, dashes, 2);
        g.setColour(audioProcessor.upColor.withAlpha(0.65f));
        g.fillPath(dashed);
    }

    // The effective (morphed) target, if it differs from the drawn one
    if (morphing)
    {
        juce::Path p;
        buildPathFromBins(p, scratchA.data(), false);
        g.setColour(audioProcessor.curveColor.withAlpha(0.45f));
        g.strokePath(p, juce::PathStrokeType(1.6f));
    }

    // The drawn curve itself
    juce::Path curvePath;
    buildPathFromBins(curvePath, shiftedCurve.data(), false);
    g.setColour(audioProcessor.curveColor);
    g.strokePath(curvePath, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));
}

void SpectralCompressAudioProcessorEditor::drawLegend(juce::Graphics& g)
{
    const auto ca = canvasArea();
    const int  mode = audioProcessor.getModeValue();

    const float morphAmount = audioProcessor.apvts.getRawParameterValue("morphAmount")->load();
    const float downRatio = audioProcessor.apvts.getRawParameterValue("downRatio")->load();
    const float upRatio = audioProcessor.apvts.getRawParameterValue("upRatio")->load();

    const bool scShown = showSidechain
        && (mode != SpectralCompressAudioProcessor::ModeLevel || morphAmount > 0.001f);

    struct Entry { juce::Colour colour; juce::String text; bool dashed; };
    std::vector<Entry> entries;

    entries.push_back({ audioProcessor.spectrumColor.brighter(0.5f), "Input spectrum", false });
    if (showOutput)
        entries.push_back({ audioProcessor.outputColor, "Output (input + reduction)", false });
    if (showGR)
    {
        entries.push_back({ audioProcessor.downColor, "Turned down", false });
        entries.push_back({ audioProcessor.upColor,   "Turned up", false });
    }
    if (scShown)
        entries.push_back({ audioProcessor.sidechainColor, "Sidechain", false });

    entries.push_back({ audioProcessor.curveColor, "Target curve (drawn)", false });
    if (downRatio > 1.005f)
        entries.push_back({ audioProcessor.downColor, "Downwards threshold", true });
    if (upRatio > 1.005f)
        entries.push_back({ audioProcessor.upColor, "Upwards threshold", true });

    const float rowH = 14.0f * scaleFactor;
    const float swatch = 16.0f * scaleFactor;
    const float padding = 7.0f * scaleFactor;

    g.setFont(10.5f * scaleFactor);
    float textW = 0.0f;
    for (const auto& e : entries)
        textW = juce::jmax(textW, (float)g.getCurrentFont().getStringWidth(e.text));

    const float w = swatch + padding * 2.0f + 6.0f * scaleFactor + textW;
    const float h = rowH * (float)entries.size() + padding * 2.0f;
    const float x = (float)ca.getRight() - w - 10.0f * scaleFactor;
    const float y = (float)ca.getY() + 10.0f * scaleFactor;

    g.setColour(audioProcessor.sidebarColor.withAlpha(0.82f));
    g.fillRoundedRectangle(x, y, w, h, 4.0f);
    g.setColour(audioProcessor.gridColor.withAlpha(0.7f));
    g.drawRoundedRectangle(x, y, w, h, 4.0f, 1.0f);

    float rowY = y + padding;
    for (const auto& e : entries)
    {
        const float lineY = rowY + rowH * 0.5f;
        g.setColour(e.colour);

        if (e.dashed)
        {
            const float dashes[] = { 3.0f, 3.0f };
            juce::Path line;
            line.startNewSubPath(x + padding, lineY);
            line.lineTo(x + padding + swatch, lineY);
            juce::Path dashed;
            juce::PathStrokeType(1.6f).createDashedStroke(dashed, line, dashes, 2);
            g.fillPath(dashed);
        }
        else
        {
            g.fillRect(x + padding, lineY - 1.2f, swatch, 2.4f);
        }

        g.setColour(audioProcessor.textColor.withAlpha(0.9f));
        g.drawText(e.text, (int)(x + padding + swatch + 6.0f * scaleFactor), (int)rowY,
            (int)textW + 4, (int)rowH, juce::Justification::centredLeft, false);

        rowY += rowH;
    }

    // Make it obvious that the output is not the full signal any more
    if (audioProcessor.apvts.getRawParameterValue("delta")->load() >= 0.5f)
    {
        g.setFont(juce::Font(11.0f * scaleFactor, juce::Font::bold));
        g.setColour(audioProcessor.downColor);
        g.drawText("Delta mode: Hear difference only",
            ca.getX() + (int)(10 * scaleFactor), ca.getY() + (int)(10 * scaleFactor),
            (int)(200 * scaleFactor), (int)(16 * scaleFactor),
            juce::Justification::centredLeft, false);
    }
}

void SpectralCompressAudioProcessorEditor::drawHoverReadout(juce::Graphics& g)
{
    const auto ca = canvasArea();
    if (hoverX < ca.getX() || hoverX > ca.getRight()) return;

    const int bin = xToBin(hoverX);
    const float hz = audioProcessor.binToHz(bin);

    g.setColour(audioProcessor.gridColor.withAlpha(0.6f));
    g.drawVerticalLine((int)hoverX, (float)ca.getY(), (float)ca.getBottom());

    juce::String text;
    text << (hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz" : juce::String(hz, 1) + " Hz")
        << "   bin " << bin
        << "   in " << juce::String(magToDB(inputData[bin]), 1) << " dB"
        << "   curve " << juce::String(shiftedCurve[bin], 1) << " dB"
        << "   gr " << juce::String(grData[bin], 1) << " dB";

    g.setFont(11.0f * scaleFactor);
    const int w = (int)(g.getCurrentFont().getStringWidth(text)) + 14;
    const int h = (int)(18 * scaleFactor);
    int x = (int)hoverX + 8;
    if (x + w > ca.getRight()) x = (int)hoverX - w - 8;

    g.setColour(audioProcessor.backgroundColor.withAlpha(0.85f));
    g.fillRoundedRectangle((float)x, (float)ca.getY() + 6.0f, (float)w, (float)h, 3.0f);
    g.setColour(audioProcessor.textColor);
    g.drawText(text, x + 7, ca.getY() + 6, w - 14, h, juce::Justification::centredLeft, false);
}

// ============================================================================
//  Mouse — drawing the curve
// ============================================================================
int SpectralCompressAudioProcessorEditor::wrapBin(int bin) const
{
    const int nb = juce::jmax(1, audioProcessor.numBins);
    bin %= nb;
    if (bin < 0) bin += nb;
    return bin;
}

void SpectralCompressAudioProcessorEditor::applyDrawSegment(float x0, float y0,
    float x1, float y1)
{
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    int   startBin = xToBin(x0);
    int   endBin = xToBin(x1);
    float startDb = yToDB(y0);
    float endDb = flatDraw ? startDb : yToDB(y1);

    if (isErasing)
        startDb = endDb = SpectralCompressAudioProcessor::kCurveDefaultDB;

    if (startBin > endBin) { std::swap(startBin, endBin); std::swap(startDb, endDb); }

    // Screen position -> storage position, undoing the shift.  A segment can
    // straddle the wrap point, in which case it is written as two runs.
    const int shift = audioProcessor.getCurveShiftBins();
    const int count = juce::jmin(nb, endBin - startBin + 1);

    auto dbAt = [&](int i)
        {
            if (count <= 1) return startDb;
            return startDb + (endDb - startDb) * (float)i / (float)(count - 1);
        };

    const int src = wrapBin(startBin - shift);
    const int firstRun = juce::jmin(count, nb - src);

    audioProcessor.setCurveRange(src, src + firstRun - 1, dbAt(0), dbAt(firstRun - 1));

    if (firstRun < count)
    {
        const int remaining = count - firstRun;
        audioProcessor.setCurveRange(0, remaining - 1, dbAt(firstRun), dbAt(count - 1));
    }
}

void SpectralCompressAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    const auto ca = canvasArea();
    if (!ca.contains(e.getPosition())) return;

    isDrawing = true;
    isErasing = e.mods.isRightButtonDown();
    flatDraw = e.mods.isAltDown();
    lastDragX = (float)e.x;
    lastDragY = (float)e.y;

    applyDrawSegment(lastDragX, lastDragY, lastDragX, lastDragY);
    repaint(ca);
}

void SpectralCompressAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDrawing) return;

    const auto ca = canvasArea();
    const float x = (float)juce::jlimit(ca.getX(), ca.getRight(), e.x);
    const float y = (float)juce::jlimit(ca.getY(), ca.getBottom(), e.y);

    applyDrawSegment(lastDragX, lastDragY, x, y);

    lastDragX = x;
    if (!flatDraw) lastDragY = y;

    hoverX = x;
    hoverY = y;
    repaint(ca);
}

void SpectralCompressAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    isDrawing = false;
    isErasing = false;
    flatDraw = false;
}

void SpectralCompressAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!canvasArea().contains(e.getPosition())) return;
    audioProcessor.resetCurve(SpectralCompressAudioProcessor::kCurveDefaultDB);
    repaint(canvasArea());
}

void SpectralCompressAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    const auto ca = canvasArea();
    if (ca.contains(e.getPosition()))
    {
        hoverX = (float)e.x;
        hoverY = (float)e.y;
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
    else
    {
        hoverX = -1.0f;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    repaint(ca);
}

void SpectralCompressAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    hoverX = -1.0f;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(canvasArea());
}
