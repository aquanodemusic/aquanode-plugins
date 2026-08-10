#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr int kSidebarW = 370;
static constexpr int kPad = 8;
static constexpr int kColGap = 4;
static constexpr int kRowH = 20;
static constexpr int kRowGap = 3;
static constexpr int kSectionGap = 10;
static constexpr int kHeaderH = 14;
static constexpr int kUsableW = kSidebarW - 2 * kPad;
static constexpr int kColW = (kUsableW - 2 * kColGap) / 3;

static bool parseHexColour(const juce::String& text, juce::Colour& out)
{
    juce::String s = text.trim().removeCharacters("#").toUpperCase();
    if (s.length() == 6) s = "FF" + s;
    if (s.length() != 8) return false;
    for (auto ch : s)
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    out = juce::Colour(s.getHexValue32());
    return true;
}
static juce::String colourToHex(juce::Colour c) { return c.toDisplayString(true).toUpperCase(); }

static void styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff55eedd));
    b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
}
static void styleValueSlider(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, kRowH);
    s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff222222));
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xff444444));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff888888));
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1a1a1a));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff333333));
}
static void styleColorEditor(juce::TextEditor& e)
{
    e.setFont(10.5f);
    e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    e.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    e.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff444444));
    e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff55eedd));
    e.setJustification(juce::Justification::centred);
}
static void styleSectionLabel(juce::Label& l)
{
    l.setFont(juce::Font(11.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, juce::Colour(0xff55eedd));
    l.setJustificationType(juce::Justification::centredLeft);
}
static void styleSmoothSlider(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, kRowH);
    s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff222222));
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xff444444));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff888888));
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1a1a1a));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff333333));
}
static void styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    c.setColour(juce::ComboBox::textColourId, juce::Colours::lightgrey);
    c.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff444444));
    c.setColour(juce::ComboBox::arrowColourId, juce::Colours::lightgrey);
}

SpectralCompareAudioProcessorEditor::SpectralCompareAudioProcessorEditor(
    SpectralCompareAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(kBaseWidth, kBaseHeight);
    setResizable(true, true);
    setResizeLimits(kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 2, kBaseHeight * 2);
    getConstrainer()->setFixedAspectRatio((float)kBaseWidth / (float)kBaseHeight);

    styleSectionLabel(generalLabel);   generalLabel.setText("General Controls", juce::dontSendNotification);  addAndMakeVisible(generalLabel);
    styleSectionLabel(visualsLabel);   visualsLabel.setText("Visuals", juce::dontSendNotification);           addAndMakeVisible(visualsLabel);
    styleSectionLabel(audioLabel);     audioLabel.setText("Audio, Gating and Enhancing", juce::dontSendNotification); addAndMakeVisible(audioLabel);
    styleSectionLabel(morphingLabel);  morphingLabel.setText("Morphing", juce::dontSendNotification);         addAndMakeVisible(morphingLabel);

    static const char* kColorCatNames[9] = {
        "Background", "Grid Color", "Sidebar",
        "Main Color", "Sidechain",  "Delta",
        "Morph Color","Output",     "Text Color"
    };
    for (int i = 0; i < 9; ++i)
    {
        colorCatLabels[i].setText(kColorCatNames[i], juce::dontSendNotification);
        colorCatLabels[i].setFont(juce::Font(11.0f));
        colorCatLabels[i].setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
        colorCatLabels[i].setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(colorCatLabels[i]);
    }

    auto makeColLabel = [&](juce::Label& lbl, const juce::String& text) {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(11.0f));
        lbl.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
        lbl.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lbl);
        };
    makeColLabel(fftSizeLabel, "FFT Size");
    makeColLabel(freqFromLabel, "Inspect From Hz");
    makeColLabel(freqToLabel, "Inspect To Hz");

    auto makeSliderLabel = [&](juce::Label& lbl, const juce::String& text) {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(10.5f));
        lbl.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
        lbl.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lbl);
        };
    makeSliderLabel(gateLowHzLabel, "Low Hz");
    SmoothingLabel.setFont(juce::Font(11.0f));
    SmoothingLabel.setColour(juce::Label::textColourId, juce::Colour(0xff55eedd));
    SmoothingLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(SmoothingLabel);
    makeSliderLabel(gateHighHzLabel, "High Hz");
    makeSliderLabel(enhLowHzLabel, "Low Hz");
    makeSliderLabel(enhHighHzLabel, "High Hz");
    makeSliderLabel(morphAmtLabel, "Amount");
    makeSliderLabel(clarityAmtLabel, "Clarity");

    fftSizeCombo.addItem("1024", 1);
    fftSizeCombo.addItem("2048", 2);
    fftSizeCombo.addItem("4096", 3);
    fftSizeCombo.addItem("8192", 4);
    styleCombo(fftSizeCombo);
    fftSizeCombo.setTooltip("FFT window size: larger = more frequency resolution, more latency");
    addAndMakeVisible(fftSizeCombo);

    {
        auto setup = [&](juce::Slider& s, const juce::String& tip) {
            styleValueSlider(s); s.setTooltip(tip); addAndMakeVisible(s);
            };
        setup(freqFromKnob, "View range lower frequency");
        setup(freqToKnob, "View range upper frequency");
        freqFromKnob.onValueChange = [this]() { viewFreqMin = (float)freqFromKnob.getValue(); repaint(canvasArea()); };
        freqToKnob.onValueChange = [this]() { viewFreqMax = (float)freqToKnob.getValue();   repaint(canvasArea()); };
    }

    auto makeShowFreeze = [&](
        juce::TextButton& showBtn, const juce::String& shortName,
        juce::TextButton& freezeBtn,
        juce::Slider& speedSlider,
        bool& showFlag, bool& frozenFlag,
        std::array<float, maxDisplayBins>& liveData,
        std::array<float, maxDisplayBins>& frozenData,
        juce::Colour accentCol)
        {
            styleButton(showBtn);
            showBtn.setButtonText("Show " + shortName);
            showBtn.setClickingTogglesState(true);
            showBtn.setToggleState(showFlag, juce::dontSendNotification);
            showBtn.setColour(juce::TextButton::buttonOnColourId, accentCol);
            showBtn.setColour(juce::TextButton::textColourOnId,
                accentCol.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
            showBtn.onClick = [&, shortName]() { showFlag = showBtn.getToggleState(); repaint(canvasArea()); };
            addAndMakeVisible(showBtn);

            styleSmoothSlider(speedSlider);
            speedSlider.setTooltip("Display smoothing speed (0 = instant, 0.99 = very slow)");
            addAndMakeVisible(speedSlider);

            styleButton(freezeBtn);
            freezeBtn.setButtonText("Freeze " + shortName);
            freezeBtn.setClickingTogglesState(true);
            freezeBtn.setColour(juce::TextButton::buttonOnColourId, accentCol.darker(0.25f));
            freezeBtn.setColour(juce::TextButton::textColourOnId,
                accentCol.darker(0.25f).getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
            freezeBtn.onClick = [&, shortName]() {
                frozenFlag = freezeBtn.getToggleState();
                if (frozenFlag) frozenData = liveData;
                };
            addAndMakeVisible(freezeBtn);
        };

    makeShowFreeze(showMainButton, "Main", freezeMainButton, mainSmoothSlider, showMain, mainIsFrozen, mainDisplayData, frozenMainDisplay, audioProcessor.getMainSpectrumColor());
    makeShowFreeze(showSidechainButton, "Side", freezeSidechainButton, sidechainSmoothSlider, showSidechain, sidechainIsFrozen, sidechainDisplayData, frozenSidechainDisplay, audioProcessor.getSidechainSpectrumColor());
    makeShowFreeze(showDeltaButton, "Delta", freezeDeltaButton, deltaSmoothSlider, showDelta, deltaIsFrozen, smoothedDeltaDisplay, frozenDeltaDisplay, audioProcessor.getDeltaColor());
    makeShowFreeze(showMorphButton, "Morph", freezeMorphButton, morphSmoothSlider, showMorph, morphIsFrozen, morphDisplayData, frozenMorphDisplay, audioProcessor.getMorphColor());
    makeShowFreeze(showOutputButton, "Output", freezeOutputButton, outputSmoothSlider, showOutput, outputIsFrozen, outputDisplayData, frozenOutputDisplay, audioProcessor.getOutputSpectrumColor());

    auto setupColorEditor = [&](juce::TextEditor& e, juce::Colour initialCol, std::function<void()> onCommit) {
        styleColorEditor(e);
        e.setText(colourToHex(initialCol), juce::dontSendNotification);
        e.onReturnKey = onCommit; e.onFocusLost = onCommit;
        addAndMakeVisible(e);
        };
    setupColorEditor(bgColorInput, audioProcessor.getBackgroundColor(), [this] { applyBgColor(); });
    setupColorEditor(gridColorInput, audioProcessor.getGridColor(), [this] { applyGridColor(); });
    setupColorEditor(sidebarColorInput, audioProcessor.getSidebarColor(), [this] { applySidebarColor(); });
    setupColorEditor(mainColorInput, audioProcessor.getMainSpectrumColor(), [this] { applyMainColor(); });
    setupColorEditor(sidechainColorInput, audioProcessor.getSidechainSpectrumColor(), [this] { applySidechainColor(); });
    setupColorEditor(deltaColorInput, audioProcessor.getDeltaColor(), [this] { applyDeltaColor(); });
    setupColorEditor(morphColorInput, audioProcessor.getMorphColor(), [this] { applyMorphColor(); });
    setupColorEditor(outputColorInput, audioProcessor.getOutputSpectrumColor(), [this] { applyOutputColor(); });
    setupColorEditor(textColorInput, audioProcessor.getTextColor(), [this] { applyTextColor(); });

    styleButton(resetVisualsButton);
    resetVisualsButton.setButtonText("Reset Visuals");
    resetVisualsButton.onClick = [this]() {
        showMainButton.setToggleState(true, juce::sendNotificationSync);
        showSidechainButton.setToggleState(true, juce::sendNotificationSync);
        showDeltaButton.setToggleState(false, juce::sendNotificationSync);
        showMorphButton.setToggleState(false, juce::sendNotificationSync);
        showOutputButton.setToggleState(false, juce::sendNotificationSync);
        freezeMainButton.setToggleState(false, juce::sendNotificationSync);
        freezeSidechainButton.setToggleState(false, juce::sendNotificationSync);
        freezeDeltaButton.setToggleState(false, juce::sendNotificationSync);
        freezeMorphButton.setToggleState(false, juce::sendNotificationSync);
        freezeOutputButton.setToggleState(false, juce::sendNotificationSync);
        mainSmoothSlider.setValue(0.7, juce::sendNotificationSync);
        sidechainSmoothSlider.setValue(0.7, juce::sendNotificationSync);
        deltaSmoothSlider.setValue(0.7, juce::sendNotificationSync);
        morphSmoothSlider.setValue(0.7, juce::sendNotificationSync);
        outputSmoothSlider.setValue(0.7, juce::sendNotificationSync);
        };
    addAndMakeVisible(resetVisualsButton);

    styleButton(resetColorsButton);
    resetColorsButton.setButtonText("Reset Colors");
    resetColorsButton.onClick = [this]() {
        audioProcessor.resetColors();
        bgColorInput.setText(colourToHex(audioProcessor.getBackgroundColor()), juce::dontSendNotification);
        gridColorInput.setText(colourToHex(audioProcessor.getGridColor()), juce::dontSendNotification);
        sidebarColorInput.setText(colourToHex(audioProcessor.getSidebarColor()), juce::dontSendNotification);
        mainColorInput.setText(colourToHex(audioProcessor.getMainSpectrumColor()), juce::dontSendNotification);
        sidechainColorInput.setText(colourToHex(audioProcessor.getSidechainSpectrumColor()), juce::dontSendNotification);
        deltaColorInput.setText(colourToHex(audioProcessor.getDeltaColor()), juce::dontSendNotification);
        morphColorInput.setText(colourToHex(audioProcessor.getMorphColor()), juce::dontSendNotification);
        outputColorInput.setText(colourToHex(audioProcessor.getOutputSpectrumColor()), juce::dontSendNotification);
        textColorInput.setText(colourToHex(audioProcessor.getTextColor()), juce::dontSendNotification);
        refreshUIColors();
        };
    addAndMakeVisible(resetColorsButton);

    styleButton(interpolateButton);
    interpolateButton.setButtonText("Interpolate");
    interpolateButton.setClickingTogglesState(true);
    interpolateButton.setToggleState(true, juce::dontSendNotification);
    interpolateButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff555555));
    interpolateButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    interpolateButton.onClick = [this]() { interpolate = interpolateButton.getToggleState(); };
    addAndMakeVisible(interpolateButton);

    styleButton(hearDeltaButton);
    hearDeltaButton.setButtonText("Hear Delta");
    hearDeltaButton.setClickingTogglesState(true);
    hearDeltaButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffddaa00));
    hearDeltaButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(hearDeltaButton);

    styleButton(monitorSideButton);
    monitorSideButton.setButtonText("Monitor Side");
    monitorSideButton.setClickingTogglesState(true);
    monitorSideButton.setColour(juce::TextButton::buttonOnColourId, audioProcessor.getSidechainSpectrumColor().darker(0.2f));
    monitorSideButton.setColour(juce::TextButton::textColourOnId,
        audioProcessor.getSidechainSpectrumColor().darker(0.2f).getPerceivedBrightness() > 0.55f
        ? juce::Colours::black : juce::Colours::white);
    addAndMakeVisible(monitorSideButton);

    styleButton(reassignButton);
    reassignButton.setButtonText("Reassign");
    reassignButton.setClickingTogglesState(true);
    reassignButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00aacc));
    reassignButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    reassignButton.setTooltip("Spectral reassignment: crisp frequency display (visual only)");
    addAndMakeVisible(reassignButton);

    styleButton(gateEnableButton);
    gateEnableButton.setButtonText("Gate: OFF");
    gateEnableButton.setClickingTogglesState(true);
    gateEnableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffdd4444));
    gateEnableButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    gateEnableButton.onClick = [this]() {
        gateEnableButton.setButtonText(gateEnableButton.getToggleState() ? "Gate" : "Gate");
        };
    addAndMakeVisible(gateEnableButton);

    styleValueSlider(gateBinStartKnob);
    gateBinStartKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xffdd4444));
    gateBinStartKnob.setTooltip("Gate lower frequency (Hz)");
    addAndMakeVisible(gateBinStartKnob);

    styleValueSlider(gateBinEndKnob);
    gateBinEndKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xffdd4444));
    gateBinEndKnob.setTooltip("Gate upper frequency (Hz)");
    addAndMakeVisible(gateBinEndKnob);

    styleButton(enhanceEnableButton);
    enhanceEnableButton.setButtonText("Enhance: OFF");
    enhanceEnableButton.setClickingTogglesState(true);
    enhanceEnableButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff44aa66));
    enhanceEnableButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    enhanceEnableButton.onClick = [this]() {
        enhanceEnableButton.setButtonText(enhanceEnableButton.getToggleState() ? "Enhance" : "Enhance");
        };
    addAndMakeVisible(enhanceEnableButton);

    styleValueSlider(enhanceBinStartKnob);
    enhanceBinStartKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xff44aa66));
    enhanceBinStartKnob.setTooltip("Enhance lower frequency (Hz)");
    addAndMakeVisible(enhanceBinStartKnob);

    styleValueSlider(enhanceBinEndKnob);
    enhanceBinEndKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xff44aa66));
    enhanceBinEndKnob.setTooltip("Enhance upper frequency (Hz)");
    addAndMakeVisible(enhanceBinEndKnob);

    styleValueSlider(morphSlider);
    morphSlider.setColour(juce::Slider::thumbColourId, audioProcessor.getMorphColor());
    morphSlider.setColour(juce::Slider::trackColourId, audioProcessor.getMorphColor().withAlpha(0.4f));
    morphSlider.setTooltip("Morph amount (0=dry, 1=full envelope transfer, >1=hyper-morph)");
    addAndMakeVisible(morphSlider);

    styleValueSlider(claritySlider);
    claritySlider.setColour(juce::Slider::thumbColourId, audioProcessor.getMorphColor().brighter(0.2f));
    claritySlider.setTooltip("Envelope smoothing bandwidth (Clarity)");
    addAndMakeVisible(claritySlider);

    styleButton(morphSmoothToggle);
    morphSmoothToggle.setButtonText("Use Smooth");
    morphSmoothToggle.setClickingTogglesState(true);
    morphSmoothToggle.setColour(juce::TextButton::buttonOnColourId, audioProcessor.getMorphColor());
    morphSmoothToggle.setColour(juce::TextButton::textColourOnId,
        audioProcessor.getMorphColor().getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
    morphSmoothToggle.setTooltip("Temporal smoothing of morph envelope (audible hold effect)");
    morphSmoothToggle.onClick = [this]() {};
    addAndMakeVisible(morphSmoothToggle);

    styleButton(enhanceModeButton);
    enhanceModeButton.setButtonText("Compress Up");
    enhanceModeButton.setClickingTogglesState(true);
    enhanceModeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffaa6600));
    enhanceModeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    enhanceModeButton.onClick = [this]() {
        enhanceModeButton.setButtonText(enhanceModeButton.getToggleState() ? "Compress Down" : "Compress Up");
        };
    addAndMakeVisible(enhanceModeButton);

    styleCombo(drawTargetCombo);
    drawTargetCombo.addItem("Draw Main", 1);
    drawTargetCombo.addItem("Draw Side", 2);
    drawTargetCombo.addItem("Draw Gate", 3);
    drawTargetCombo.addItem("Draw Enhance", 4);
    drawTargetCombo.setSelectedId(1, juce::dontSendNotification);
    drawTargetCombo.setTooltip("Select which curve to draw on the canvas");
    drawTargetCombo.onChange = [this]() {
        drawTarget = drawTargetCombo.getSelectedId() - 1;   // 0-3
        };
    addAndMakeVisible(drawTargetCombo);

    styleButton(resetDrawsButton);
    resetDrawsButton.setButtonText("Reset Draws");
    resetDrawsButton.setTooltip("Reset all four drawable curves to default values");
    resetDrawsButton.onClick = [this]() {
        audioProcessor.resetMainFilterCurve();
        audioProcessor.resetSidechainFilterCurve();
        audioProcessor.resetGateFilterCurve();
        audioProcessor.resetEnhanceFilterCurve();
        mainFilterDisplay.fill(0.0f);
        sidechainFilterDisplay.fill(0.0f);
        gateFilterDisplay.fill(-60.0f);
        enhanceFilterDisplay.fill(-30.0f);
        };
    addAndMakeVisible(resetDrawsButton);

    fftSizeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.apvts, "fftSize", fftSizeCombo);
    freqFromAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "freqFrom", freqFromKnob);
    freqToAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "freqTo", freqToKnob);
    mainSmoothAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "smoothMain", mainSmoothSlider);
    sidechainSmoothAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "smoothSidechain", sidechainSmoothSlider);
    deltaSmoothAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "smoothDelta", deltaSmoothSlider);
    morphSmoothAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "smoothMorph", morphSmoothSlider);
    outputSmoothAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "smoothOutput", outputSmoothSlider);
    morphAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "morph", morphSlider);
    clarityAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "clarity", claritySlider);
    morphSmoothAudioAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "smoothMorphAudio", morphSmoothToggle);
    hearDeltaAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "hearDelta", hearDeltaButton);
    monitorSideAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "monitorSide", monitorSideButton);
    reassignAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "reassign", reassignButton);
    gateEnableAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "gateEnable", gateEnableButton);
    gateBinStartAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "gateBinStart", gateBinStartKnob);
    gateBinEndAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "gateBinEnd", gateBinEndKnob);
    enhanceEnableAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "enhanceEnable", enhanceEnableButton);
    enhanceModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "enhanceAttenuate", enhanceModeButton);
    enhanceBinStartAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "enhanceBinStart", enhanceBinStartKnob);
    enhanceBinEndAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "enhanceBinEnd", enhanceBinEndKnob);
    interpolateAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "interpolate", interpolateButton);
    showMainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "showMain", showMainButton);
    showSidechainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "showSidechain", showSidechainButton);
    showDeltaAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "showDelta", showDeltaButton);
    showMorphAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "showMorph", showMorphButton);
    showOutputAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "showOutput", showOutputButton);

    gateEnableButton.setButtonText(gateEnableButton.getToggleState() ? "Gate: ON" : "Gate: OFF");
    enhanceEnableButton.setButtonText(enhanceEnableButton.getToggleState() ? "Enhance: ON" : "Enhance: OFF");
    enhanceModeButton.setButtonText(enhanceModeButton.getToggleState() ? "Compress Down" : "Compress Up");

    viewFreqMin = (float)freqFromKnob.getValue();
    viewFreqMax = (float)freqToKnob.getValue();

    toggleSidebarButton.setButtonText("Hide Sidebar");
    toggleSidebarButton.onClick = [this]() {
        sidebarVisible = !sidebarVisible;
        toggleSidebarButton.setButtonText(sidebarVisible ? "Hide Sidebar" : "Show Sidebar");
        resized(); repaint();
        };
    styleButton(toggleSidebarButton);
    addAndMakeVisible(toggleSidebarButton);

    styleButton(heatmapToggleButton);
    heatmapToggleButton.setButtonText("Waterfall");
    heatmapToggleButton.setClickingTogglesState(true);
    heatmapToggleButton.setToggleState(false, juce::dontSendNotification);
    heatmapToggleButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2266cc));
    heatmapToggleButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    heatmapToggleButton.setTooltip("Toggle scrolling waterfall spectrogram below the spectrum view");
    heatmapToggleButton.onClick = [this]() {
        showHeatmap = heatmapToggleButton.getToggleState();
        heatmapImage = juce::Image();
        resized(); repaint();
        };
    addAndMakeVisible(heatmapToggleButton);

    refreshUIColors();
    startTimerHz(60);
}

SpectralCompareAudioProcessorEditor::~SpectralCompareAudioProcessorEditor()
{
    stopTimer();
    fftSizeAttachment.reset(); freqFromAttachment.reset(); freqToAttachment.reset();
    mainSmoothAttachment.reset(); sidechainSmoothAttachment.reset(); deltaSmoothAttachment.reset();
    morphSmoothAttachment.reset(); outputSmoothAttachment.reset();
    morphAttachment.reset(); clarityAttachment.reset(); morphSmoothAudioAttachment.reset();
    hearDeltaAttachment.reset(); monitorSideAttachment.reset();
    gateEnableAttachment.reset();
    gateBinStartAttachment.reset(); gateBinEndAttachment.reset();
    enhanceEnableAttachment.reset(); enhanceModeAttachment.reset();
    enhanceBinStartAttachment.reset(); enhanceBinEndAttachment.reset();
    interpolateAttachment.reset();
    showMainAttachment.reset(); showSidechainAttachment.reset(); showDeltaAttachment.reset();
    showMorphAttachment.reset(); showOutputAttachment.reset();
}

void SpectralCompareAudioProcessorEditor::refreshUIColors()
{
    auto tintSlider = [](juce::Slider& s, juce::Colour c) {
        s.setColour(juce::Slider::thumbColourId, c);
        s.setColour(juce::Slider::trackColourId, c.withAlpha(0.4f));
        };
    auto tintShowFreeze = [](juce::TextButton& show, juce::TextButton& frz, juce::Colour c) {
        auto tc = [&](juce::Colour bg) {
            return bg.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white;
            };
        show.setColour(juce::TextButton::buttonOnColourId, c);
        show.setColour(juce::TextButton::textColourOnId, tc(c));
        frz.setColour(juce::TextButton::buttonOnColourId, c.darker(0.25f));
        frz.setColour(juce::TextButton::textColourOnId, tc(c.darker(0.25f)));
        };

    const auto mainCol = audioProcessor.getMainSpectrumColor();
    const auto sideCol = audioProcessor.getSidechainSpectrumColor();
    const auto deltaCol = audioProcessor.getDeltaColor();
    const auto morphCol = audioProcessor.getMorphColor();
    const auto outCol = audioProcessor.getOutputSpectrumColor();
    const auto textCol = audioProcessor.getTextColor();

    tintSlider(mainSmoothSlider, mainCol);
    tintSlider(sidechainSmoothSlider, sideCol);
    tintSlider(deltaSmoothSlider, deltaCol);
    tintSlider(morphSmoothSlider, morphCol);
    tintSlider(outputSmoothSlider, outCol);
    tintSlider(morphSlider, morphCol);
    tintSlider(claritySlider, morphCol.brighter(0.2f));

    tintShowFreeze(showMainButton, freezeMainButton, mainCol);
    tintShowFreeze(showSidechainButton, freezeSidechainButton, sideCol);
    tintShowFreeze(showDeltaButton, freezeDeltaButton, deltaCol);
    tintShowFreeze(showMorphButton, freezeMorphButton, morphCol);
    tintShowFreeze(showOutputButton, freezeOutputButton, outCol);

    morphSmoothToggle.setColour(juce::TextButton::buttonOnColourId, morphCol);
    morphSmoothToggle.setColour(juce::TextButton::textColourOnId,
        morphCol.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);

    monitorSideButton.setColour(juce::TextButton::buttonOnColourId, sideCol.darker(0.2f));
    monitorSideButton.setColour(juce::TextButton::textColourOnId,
        sideCol.darker(0.2f).getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);

    hearDeltaButton.setColour(juce::TextButton::buttonOnColourId, deltaCol.darker(0.2f));
    hearDeltaButton.setColour(juce::TextButton::textColourOnId,
        deltaCol.darker(0.2f).getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);

    drawTargetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    drawTargetCombo.setColour(juce::ComboBox::textColourId, textCol);
    drawTargetCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff444444));
    drawTargetCombo.setColour(juce::ComboBox::arrowColourId, textCol);

    auto applyBtnTextColor = [&](juce::TextButton& b) {
        b.setColour(juce::TextButton::textColourOffId, textCol);
        };
    applyBtnTextColor(showMainButton);       applyBtnTextColor(freezeMainButton);
    applyBtnTextColor(showSidechainButton);  applyBtnTextColor(freezeSidechainButton);
    applyBtnTextColor(showDeltaButton);      applyBtnTextColor(freezeDeltaButton);
    applyBtnTextColor(showMorphButton);      applyBtnTextColor(freezeMorphButton);
    applyBtnTextColor(showOutputButton);     applyBtnTextColor(freezeOutputButton);
    applyBtnTextColor(resetVisualsButton);
    applyBtnTextColor(resetColorsButton);
    applyBtnTextColor(interpolateButton);
    applyBtnTextColor(hearDeltaButton);
    applyBtnTextColor(monitorSideButton);
    applyBtnTextColor(reassignButton);
    applyBtnTextColor(gateEnableButton);
    applyBtnTextColor(enhanceEnableButton);
    applyBtnTextColor(enhanceModeButton);
    applyBtnTextColor(morphSmoothToggle);

    const auto sideCol2 = audioProcessor.getSidechainSpectrumColor().darker(0.2f);
    monitorSideButton.setColour(juce::TextButton::buttonOnColourId, sideCol2);
    monitorSideButton.setColour(juce::TextButton::textColourOnId,
        sideCol2.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);

    applyBtnTextColor(resetDrawsButton);
    applyBtnTextColor(heatmapToggleButton);

    auto setEditorColor = [&](juce::TextEditor& e, juce::Colour bg) {
        e.setColour(juce::TextEditor::backgroundColourId, bg);
        e.setColour(juce::TextEditor::textColourId, textCol);
        };
    setEditorColor(bgColorInput, audioProcessor.getBackgroundColor());
    setEditorColor(gridColorInput, audioProcessor.getGridColor());
    setEditorColor(sidebarColorInput, audioProcessor.getSidebarColor());
    setEditorColor(mainColorInput, mainCol);
    setEditorColor(sidechainColorInput, sideCol);
    setEditorColor(deltaColorInput, deltaCol);
    setEditorColor(morphColorInput, morphCol);
    setEditorColor(outputColorInput, outCol);
    setEditorColor(textColorInput, textCol);

    generalLabel.setColour(juce::Label::textColourId, textCol);
    visualsLabel.setColour(juce::Label::textColourId, textCol);
    SmoothingLabel.setColour(juce::Label::textColourId, textCol);
    audioLabel.setColour(juce::Label::textColourId, textCol);
    morphingLabel.setColour(juce::Label::textColourId, textCol);
    for (auto& lbl : colorCatLabels)
        lbl.setColour(juce::Label::textColourId, textCol.withAlpha(0.8f));

    const juce::Colour dimText = textCol.withAlpha(0.8f);
    fftSizeLabel.setColour(juce::Label::textColourId, dimText);
    freqFromLabel.setColour(juce::Label::textColourId, dimText);
    freqToLabel.setColour(juce::Label::textColourId, dimText);
    gateLowHzLabel.setColour(juce::Label::textColourId, dimText);
    gateHighHzLabel.setColour(juce::Label::textColourId, dimText);
    enhLowHzLabel.setColour(juce::Label::textColourId, dimText);
    enhHighHzLabel.setColour(juce::Label::textColourId, dimText);
    morphAmtLabel.setColour(juce::Label::textColourId, dimText);
    clarityAmtLabel.setColour(juce::Label::textColourId, dimText);

    repaint();
}

void SpectralCompareAudioProcessorEditor::timerCallback()
{
    const float nq = nyquist();
    if (nq > 1.0f && viewFreqMax > nq)
    {
        viewFreqMax = nq;
        freqToKnob.setRange(220.0, (double)nq, 1.0);
        freqToKnob.setValue(nq, juce::dontSendNotification);
    }

    if (!mainIsFrozen)      audioProcessor.getMainFFTData(mainDisplayData.data(), audioProcessor.numBins);
    if (!sidechainIsFrozen) audioProcessor.getSidechainFFTData(sidechainDisplayData.data(), audioProcessor.numBins);
    if (!morphIsFrozen)     audioProcessor.getMorphedFFTData(morphDisplayData.data(), audioProcessor.numBins);
    if (!outputIsFrozen)    audioProcessor.getOutputFFTData(outputDisplayData.data(), audioProcessor.numBins);

    // Pull latest spectral-reassignment frames when the Reassign mode is active.
    if (audioProcessor.isReassignActive())
    {
        ReassignFrame tmp;
        if (audioProcessor.popReassignMainFrame(tmp)) { reassignMainFrame = std::move(tmp); hasReassignMain = true; }
        if (audioProcessor.popReassignSideFrame(tmp)) { reassignSideFrame = std::move(tmp); hasReassignSide = true; }
    }
    else
    {
        // Reset so stale frames aren't drawn after toggling off.
        hasReassignMain = false;
        hasReassignSide = false;
    }

    audioProcessor.getMainFilterCurveData(mainFilterDisplay.data(), audioProcessor.numBins);
    audioProcessor.getSidechainFilterCurveData(sidechainFilterDisplay.data(), audioProcessor.numBins);
    audioProcessor.getGateFilterCurveData(gateFilterDisplay.data(), audioProcessor.numBins);
    audioProcessor.getEnhanceFilterCurveData(enhanceFilterDisplay.data(), audioProcessor.numBins);

    const bool hearDeltaOn = audioProcessor.apvts.getRawParameterValue("hearDelta")->load(std::memory_order_relaxed) >= 0.5f;
    morphSlider.setEnabled(!hearDeltaOn);
    claritySlider.setEnabled(!hearDeltaOn);
    morphSmoothToggle.setEnabled(!hearDeltaOn);
    morphingLabel.setText(hearDeltaOn
        ? "Morphing: To activate, turn off Hear Delta"
        : "Morphing and drawable spectral filters",
        juce::dontSendNotification);

    if (!deltaIsFrozen)
    {
        const float coeff = (float)deltaSmoothSlider.getValue();
        const int   nb = audioProcessor.numBins;
        for (int i = 0; i < nb; ++i)
        {
            const float mMag = mainDisplayData[i];
            const float sMag = sidechainDisplayData[i];
            const float dDB = (mMag > kSilenceFloor && sMag > kSilenceFloor)
                ? 20.0f * std::log10(mMag) - 20.0f * std::log10(sMag) : 0.0f;
            smoothedDeltaDisplay[i] = smoothedDeltaDisplay[i] * coeff + dDB * (1.0f - coeff);
        }
    }

    if (showHeatmap)
    {
        if (audioProcessor.isReassignActive())
            updateHeatmapReassign();
        else
            updateHeatmap();
    }

    repaint(canvasArea());
}

juce::Rectangle<int> SpectralCompareAudioProcessorEditor::canvasArea() const
{
    if (!sidebarVisible) return getLocalBounds();
    const int scaledSidebarW = (int)(kSidebarW * scaleFactor);
    return getLocalBounds().withTrimmedRight(scaledSidebarW);
}

juce::Rectangle<int> SpectralCompareAudioProcessorEditor::spectrumArea() const
{
    auto ca = canvasArea();
    if (!showHeatmap) return ca;
    const int splitY = ca.getY() + (int)(ca.getHeight() * heatmapSplitRatio);
    return ca.withBottom(splitY);
}

juce::Rectangle<int> SpectralCompareAudioProcessorEditor::heatmapArea() const
{
    auto ca = canvasArea();
    const int splitY = ca.getY() + (int)(ca.getHeight() * heatmapSplitRatio);
    return ca.withTop(splitY);
}

float SpectralCompareAudioProcessorEditor::binToX(int bin) const
{
    auto ca = spectrumArea();
    if (audioProcessor.numBins <= 1) return (float)ca.getX();
    const float logMin = std::log10(juce::jmax(viewFreqMin, 1.0f));
    const float logMax = std::log10(juce::jmax(viewFreqMax, viewFreqMin + 1.0f));
    float binHz = bin * nyquist() / (audioProcessor.numBins - 1);
    binHz = juce::jmax(binHz, 1.0f);
    float t = (std::log10(binHz) - logMin) / (logMax - logMin);
    return ca.getX() + t * ca.getWidth();
}

int SpectralCompareAudioProcessorEditor::xToBin(float x) const
{
    auto ca = spectrumArea();
    const float logMin = std::log10(juce::jmax(viewFreqMin, 1.0f));
    const float logMax = std::log10(juce::jmax(viewFreqMax, viewFreqMin + 1.0f));
    float t = (x - ca.getX()) / (float)ca.getWidth();
    float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
    int   bin = juce::roundToInt(hz * (audioProcessor.numBins - 1) / nyquist());
    return juce::jlimit(0, audioProcessor.numBins - 1, bin);
}

float SpectralCompareAudioProcessorEditor::magToY(float mag) const
{
    auto ca = spectrumArea();
    const float minDB = -90.0f, maxDB = 0.0f;
    float dB = (mag > 0.0f) ? 20.0f * std::log10(mag) : minDB;
    dB = juce::jlimit(minDB, maxDB, dB);
    float t = (dB - maxDB) / (minDB - maxDB);
    return ca.getY() + t * ca.getHeight();
}

float SpectralCompareAudioProcessorEditor::deltaDBtoY(float dB) const
{
    auto ca = spectrumArea();
    dB = juce::jlimit(-kDeltaRange, kDeltaRange, dB);
    float t = 0.5f - dB / (2.0f * kDeltaRange);
    return ca.getY() + t * ca.getHeight();
}

// ============================================================================
// Mouse
// ============================================================================
void SpectralCompareAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    if (showHeatmap)
    {
        auto ca = canvasArea();
        int splitY = ca.getY() + (int)(ca.getHeight() * heatmapSplitRatio);
        if (std::abs(e.y - splitY) <= 4 && e.x >= ca.getX() && e.x <= ca.getRight())
        {
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            hoverX = -1.0f;
            repaint();
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    hoverX = (float)e.x;
    repaint();
}

void SpectralCompareAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
    hoverX = -1.0f;
    repaint();
}

void SpectralCompareAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    // Check for split-line drag first
    if (showHeatmap)
    {
        auto ca = canvasArea();
        int splitY = ca.getY() + (int)(ca.getHeight() * heatmapSplitRatio);
        if (std::abs(event.y - splitY) <= 4 && event.x >= ca.getX() && event.x <= ca.getRight())
        {
            isDraggingSplit = true;
            return;
        }
    }

    auto ca = spectrumArea();
    if (!ca.contains(event.getPosition())) return;

    isDrawingFilter = true;
    filterLastDragX = (float)event.x;
    filterLastDragY = (float)event.y;

    const int bin = filterXToBin(filterLastDragX);
    switch (drawTarget)
    {
    case 0: {
        const float dB = juce::jlimit(-kFilterRange, kFilterRange, filterYToDB(filterLastDragY));
        audioProcessor.setMainFilterCurveRange(bin, bin, dB, dB); break;
    }
    case 1: {
        const float dB = juce::jlimit(-kFilterRange, kFilterRange, filterYToDB(filterLastDragY));
        audioProcessor.setSidechainFilterCurveRange(bin, bin, dB, dB); break;
    }
    case 2: {
        const float dB = spectrumYToDB(filterLastDragY);
        audioProcessor.setGateFilterCurveRange(bin, bin, dB, dB); break;
    }
    case 3: {
        const float dB = spectrumYToDB(filterLastDragY);
        audioProcessor.setEnhanceFilterCurveRange(bin, bin, dB, dB); break;
    }
    default: break;
    }
}

void SpectralCompareAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (isDraggingSplit)
    {
        auto ca = canvasArea();
        float newRatio = (float)(event.y - ca.getY()) / (float)ca.getHeight();
        heatmapSplitRatio = juce::jlimit(0.20f, 0.85f, newRatio);
        heatmapImage = juce::Image();  // rebuild at new dimensions
        repaint(canvasArea());
        return;
    }

    if (!isDrawingFilter) return;

    const float x = (float)event.x;
    const float y = (float)event.y;
    const int   startBin = filterXToBin(filterLastDragX);
    const int   endBin = filterXToBin(x);

    switch (drawTarget)
    {
    case 0: {
        const float s = juce::jlimit(-kFilterRange, kFilterRange, filterYToDB(filterLastDragY));
        const float e = juce::jlimit(-kFilterRange, kFilterRange, filterYToDB(y));
        audioProcessor.setMainFilterCurveRange(startBin, endBin, s, e); break;
    }
    case 1: {
        const float s = juce::jlimit(-kFilterRange, kFilterRange, filterYToDB(filterLastDragY));
        const float e = juce::jlimit(-kFilterRange, kFilterRange, filterYToDB(y));
        audioProcessor.setSidechainFilterCurveRange(startBin, endBin, s, e); break;
    }
    case 2: {
        const float s = spectrumYToDB(filterLastDragY);
        const float e = spectrumYToDB(y);
        audioProcessor.setGateFilterCurveRange(startBin, endBin, s, e); break;
    }
    case 3: {
        const float s = spectrumYToDB(filterLastDragY);
        const float e = spectrumYToDB(y);
        audioProcessor.setEnhanceFilterCurveRange(startBin, endBin, s, e); break;
    }
    default: break;
    }

    filterLastDragX = x;
    filterLastDragY = y;
}

void SpectralCompareAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    isDraggingSplit = false;
    isDrawingFilter = false;
}

// ============================================================================
// Paint
// ============================================================================
void SpectralCompareAudioProcessorEditor::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawGrid(g);
    drawFxRegionBars(g);
    drawFilterCurves(g);

    if (showSidechain)
    {
        if (audioProcessor.isReassignActive() && hasReassignSide)
            drawReassignedSpectrum(g, reassignSideFrame,
                audioProcessor.getSidechainSpectrumColor(), 0.7f);
        else
            drawSpectrum(g,
                sidechainIsFrozen ? frozenSidechainDisplay : sidechainDisplayData,
                audioProcessor.getSidechainSpectrumColor(), 0.7f);
    }
    if (showMain)
    {
        if (audioProcessor.isReassignActive() && hasReassignMain)
            drawReassignedSpectrum(g, reassignMainFrame,
                audioProcessor.getMainSpectrumColor(), 0.85f);
        else
            drawSpectrum(g,
                mainIsFrozen ? frozenMainDisplay : mainDisplayData,
                audioProcessor.getMainSpectrumColor(), 0.85f);
    }

    if (showDelta)  drawDelta(g);
    if (showMorph && audioProcessor.getMorphAmount() > 0.0f) drawMorphedSpectrum(g);
    if (showOutput) drawOutputSpectrum(g);

    drawLabels(g);
    drawHoverInfo(g);

    if (showHeatmap)
        drawHeatmap(g);

    if (sidebarVisible)
    {
        const int scaledSidebarW = (int)(kSidebarW * scaleFactor);
        auto sidebar = getLocalBounds().removeFromRight(scaledSidebarW);
        g.setColour(audioProcessor.getSidebarColor());
        g.fillRect(sidebar);
        g.setColour(audioProcessor.getSidebarColor().darker(0.4f));
        g.drawVerticalLine(sidebar.getX(), 0.0f, (float)getHeight());
    }
}

void SpectralCompareAudioProcessorEditor::drawBackground(juce::Graphics& g)
{
    g.fillAll(audioProcessor.getBackgroundColor());
}

void SpectralCompareAudioProcessorEditor::drawGrid(juce::Graphics& g)
{
    auto ca = spectrumArea();
    const juce::Colour gc = audioProcessor.getGridColor();
    const juce::Colour tc = audioProcessor.getTextColor();

    static const float freqMarkers[] = { 20,30,50,100,200,300,500,1000,2000,3000,5000,10000,20000 };
    g.setFont(10.0f * scaleFactor);
    for (float f : freqMarkers)
    {
        if (f > nyquist()) break;
        float x = binToX(juce::roundToInt(f * (audioProcessor.numBins - 1) / nyquist()));
        if (x < ca.getX() || x > ca.getRight()) continue;
        bool isMajor = (f == 20 || f == 100 || f == 1000 || f == 10000);
        g.setColour(gc.withAlpha(isMajor ? 0.5f : 0.2f));
        g.drawVerticalLine((int)x, (float)ca.getY(), (float)ca.getBottom());
        if (isMajor)
        {
            juce::String label = (f >= 1000) ? juce::String(f / 1000, 0) + "k" : juce::String((int)f);
            g.setColour(tc.withAlpha(0.8f));
            g.drawText(label, (int)x + 2, ca.getBottom() - 16, 40, 14, juce::Justification::left, false);
        }
    }

    static const float dbMarkers[] = { -6,-12,-24,-48,-72 };
    for (float db : dbMarkers)
    {
        float mag = std::pow(10.0f, db / 20.0f);
        float y = magToY(mag);
        if (y < ca.getY() || y > ca.getBottom()) continue;
        g.setColour(gc.withAlpha(0.25f));
        g.drawHorizontalLine((int)y, (float)ca.getX(), (float)ca.getRight());
        g.setColour(tc.withAlpha(0.7f));
        g.drawText(juce::String((int)db) + " dB", ca.getX() + 4, (int)y - 12, 50, 12,
            juce::Justification::left, false);
    }

    if (showDelta)
    {
        const float midY = (float)ca.getY() + ca.getHeight() * 0.5f;
        g.setColour(gc.withAlpha(0.55f));
        g.drawHorizontalLine((int)midY, (float)ca.getX(), (float)ca.getRight());
        g.setColour(tc.withAlpha(0.7f));
        g.drawText("0 dB", ca.getX() + 4, (int)midY - 12, 50, 12, juce::Justification::left, false);
        static const float deltaMarkers[] = { 6.0f,12.0f,24.0f };
        for (float d : deltaMarkers)
        {
            for (int sign : { -1, 1 })
            {
                float y2 = deltaDBtoY((float)sign * d);
                if (y2 < ca.getY() || y2 > ca.getBottom()) continue;
                g.setColour(gc.withAlpha(0.18f));
                g.drawHorizontalLine((int)y2, (float)ca.getX(), (float)ca.getRight());
                g.setColour(tc.withAlpha(0.5f));
                g.drawText((sign > 0 ? "+" : "-") + juce::String((int)d) + " dB",
                    ca.getX() + 4, (int)y2 - 12, 55, 12, juce::Justification::left, false);
            }
        }
    }
}

void SpectralCompareAudioProcessorEditor::drawFxRegionBars(juce::Graphics& g)
{
    auto ca = spectrumArea();
    const float nq = nyquist();
    const int   nb = audioProcessor.numBins;
    if (nb < 2 || nq < 1.0f) return;

    auto hzToBinX = [&](float hz) -> float {
        int bin = juce::jlimit(0, nb - 1, juce::roundToInt(hz * (nb - 1) / nq));
        return binToX(bin);
        };

    const juce::String gLabel = juce::String::fromUTF8("Gate");
    const juce::String eLabel = juce::String::fromUTF8("Enhance");

    const int labelW = (int)(52 * scaleFactor);
    const int labelH = (int)(14 * scaleFactor);
    const float labelFont = 11.0f * scaleFactor;

    {
        const float loHz = audioProcessor.apvts.getRawParameterValue("gateBinStart")->load(std::memory_order_relaxed);
        const float hiHz = audioProcessor.apvts.getRawParameterValue("gateBinEnd")->load(std::memory_order_relaxed);
        const bool  on = audioProcessor.apvts.getRawParameterValue("gateEnable")->load(std::memory_order_relaxed) >= 0.5f;
        const juce::Colour col = juce::Colour(0xffdd4444).withAlpha(on ? 0.80f : 0.30f);
        g.setColour(col);
        float x1 = hzToBinX(loHz), x2 = hzToBinX(hiHz);
        if (x1 >= ca.getX() && x1 <= ca.getRight()) g.drawVerticalLine((int)x1, (float)ca.getY(), (float)ca.getBottom());
        if (x2 >= ca.getX() && x2 <= ca.getRight() && (int)x2 != (int)x1) g.drawVerticalLine((int)x2, (float)ca.getY(), (float)ca.getBottom());
        g.setFont(labelFont);
        if (x1 >= ca.getX() && x1 <= ca.getRight())
            g.drawText(gLabel, (int)x1 + 2, ca.getY() + 4, labelW, labelH, juce::Justification::left, false);
        if (x2 >= ca.getX() && x2 <= ca.getRight() && (int)x2 != (int)x1)
            g.drawText(gLabel, (int)x2 + 2, ca.getY() + 4, labelW, labelH, juce::Justification::left, false);
    }
    {
        const float loHz = audioProcessor.apvts.getRawParameterValue("enhanceBinStart")->load(std::memory_order_relaxed);
        const float hiHz = audioProcessor.apvts.getRawParameterValue("enhanceBinEnd")->load(std::memory_order_relaxed);
        const bool  on = audioProcessor.apvts.getRawParameterValue("enhanceEnable")->load(std::memory_order_relaxed) >= 0.5f;
        const juce::Colour col = juce::Colour(0xff44aa66).withAlpha(on ? 0.80f : 0.30f);
        g.setColour(col);
        float x1 = hzToBinX(loHz), x2 = hzToBinX(hiHz);
        if (x1 >= ca.getX() && x1 <= ca.getRight()) g.drawVerticalLine((int)x1, (float)ca.getY(), (float)ca.getBottom());
        if (x2 >= ca.getX() && x2 <= ca.getRight() && (int)x2 != (int)x1) g.drawVerticalLine((int)x2, (float)ca.getY(), (float)ca.getBottom());
        g.setFont(labelFont);
        if (x1 >= ca.getX() && x1 <= ca.getRight())
            g.drawText(eLabel, (int)x1 + 2, ca.getY() + 4 + labelH + 2, labelW, labelH, juce::Justification::left, false);
        if (x2 >= ca.getX() && x2 <= ca.getRight() && (int)x2 != (int)x1)
            g.drawText(eLabel, (int)x2 + 2, ca.getY() + 4 + labelH + 2, labelW, labelH, juce::Justification::left, false);
    }
}

void SpectralCompareAudioProcessorEditor::drawSpectrum(
    juce::Graphics& g,
    const std::array<float, maxDisplayBins>& data,
    juce::Colour colour, float alpha)
{
    auto ca = spectrumArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    const float colAlpha = colour.getAlpha() / 255.0f;


    if (interpolate)
    {
        // Break path into segments at silent bins so background shows through.
        juce::Path fillPath, strokePath;
        float lastX = 0.0f;
        bool inSeg = false;

        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight())
            {
                if (inSeg) { fillPath.lineTo(lastX, (float)ca.getBottom()); fillPath.closeSubPath(); inSeg = false; }
                continue;
            }
            const bool hasSignal = data[bin] >= kSilenceFloor;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));

            if (hasSignal)
            {
                if (!inSeg)
                {
                    fillPath.startNewSubPath(x, (float)ca.getBottom());
                    fillPath.lineTo(x, y);
                    strokePath.startNewSubPath(x, y);
                    inSeg = true;
                }
                else
                {
                    fillPath.lineTo(x, y);
                    strokePath.lineTo(x, y);
                }
                lastX = x;
            }
            else
            {
                if (inSeg) { fillPath.lineTo(lastX, (float)ca.getBottom()); fillPath.closeSubPath(); inSeg = false; }
            }
        }
        if (inSeg) { fillPath.lineTo(lastX, (float)ca.getBottom()); fillPath.closeSubPath(); }

        g.setColour(colour.withAlpha(colAlpha * alpha * 0.35f));
        g.fillPath(fillPath);
        g.setColour(colour.withAlpha(colAlpha * alpha));
        g.strokePath(strokePath, juce::PathStrokeType(1.5f * scaleFactor));
    }
    else
    {
        g.setColour(colour.withAlpha(colAlpha * alpha * 0.35f));
        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight()) continue;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));
            float h = (float)ca.getBottom() - y;
            if (h <= 0.0f) continue;
            float nextX = (bin + 1 < nb) ? binToX(bin + 1) : x + 1.0f;
            float w = juce::jmax(1.0f, nextX - x - 1.0f);
            g.fillRect(x, y, w, h);
        }
    }
}

void SpectralCompareAudioProcessorEditor::drawOutputSpectrum(juce::Graphics& g)
{
    auto ca = spectrumArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    const juce::Colour col = audioProcessor.getOutputSpectrumColor();
    const auto& data = outputIsFrozen ? frozenOutputDisplay : outputDisplayData;

    const float colAlpha2 = col.getAlpha() / 255.0f;

    if (interpolate)
    {
        juce::Path fillPath, strokePath;
        float lastX2 = 0.0f;
        bool inSeg2 = false;

        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight())
            {
                if (inSeg2) { fillPath.lineTo(lastX2, (float)ca.getBottom()); fillPath.closeSubPath(); inSeg2 = false; }
                continue;
            }
            const bool hasSignal2 = data[bin] >= kSilenceFloor;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));

            if (hasSignal2)
            {
                if (!inSeg2)
                {
                    fillPath.startNewSubPath(x, (float)ca.getBottom());
                    fillPath.lineTo(x, y);
                    strokePath.startNewSubPath(x, y);
                    inSeg2 = true;
                }
                else { fillPath.lineTo(x, y); strokePath.lineTo(x, y); }
                lastX2 = x;
            }
            else
            {
                if (inSeg2) { fillPath.lineTo(lastX2, (float)ca.getBottom()); fillPath.closeSubPath(); inSeg2 = false; }
            }
        }
        if (inSeg2) { fillPath.lineTo(lastX2, (float)ca.getBottom()); fillPath.closeSubPath(); }

        g.setColour(col.withAlpha(colAlpha2 * 0.30f));
        g.fillPath(fillPath);
        g.setColour(col.withAlpha(colAlpha2 * 0.95f));
        g.strokePath(strokePath, juce::PathStrokeType(2.0f * scaleFactor, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour(col.withAlpha(colAlpha2 * 0.30f));
        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight()) continue;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));
            float h = (float)ca.getBottom() - y;
            if (h <= 0.0f) continue;
            float nextX = (bin + 1 < nb) ? binToX(bin + 1) : x + 1.0f;
            float w = juce::jmax(1.0f, nextX - x - 1.0f);
            g.fillRect(x, y, w, h);
        }
    }
}

void SpectralCompareAudioProcessorEditor::drawDelta(juce::Graphics& g)
{
    auto ca = spectrumArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    const juce::Colour col = audioProcessor.getDeltaColor();
    const float midY = (float)ca.getY() + ca.getHeight() * 0.5f;
    const auto& deltaData = deltaIsFrozen ? frozenDeltaDisplay : smoothedDeltaDisplay;

    if (!interpolate)
    {
        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight()) continue;
            const float mMag = mainIsFrozen ? frozenMainDisplay[bin] : mainDisplayData[bin];
            const float sMag = sidechainIsFrozen ? frozenSidechainDisplay[bin] : sidechainDisplayData[bin];
            if (mMag < kSilenceFloor || sMag < kSilenceFloor) continue;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), deltaDBtoY(deltaData[bin]));
            float nX = (bin + 1 < nb) ? binToX(bin + 1) : x + 1.0f;
            float top = juce::jmin(y, midY), h = std::abs(midY - y);
            if (h < 1.0f) continue;
            float w = juce::jmax(1.0f, nX - x - 1.0f);
            g.setColour(col.withAlpha(0.18f));
            g.fillRect(x, top, w, h);
            g.setColour(col.withAlpha(0.9f));
            g.fillRect(x, y, w, 1.5f * scaleFactor);
        }
        return;
    }

    juce::Path stroke, fill;
    bool strokeOpen = false, fillOpen = false;
    for (int bin = 0; bin < nb; ++bin)
    {
        const float mMag = mainIsFrozen ? frozenMainDisplay[bin] : mainDisplayData[bin];
        const float sMag = sidechainIsFrozen ? frozenSidechainDisplay[bin] : sidechainDisplayData[bin];
        if (mMag < kSilenceFloor || sMag < kSilenceFloor)
        {
            if (fillOpen) { fill.lineTo(binToX(bin - 1), midY); fill.closeSubPath(); fillOpen = false; }
            strokeOpen = false;
            continue;
        }
        float x = binToX(bin);
        if (x < ca.getX() || x > ca.getRight())
        {
            if (fillOpen) { fill.lineTo(x, midY); fill.closeSubPath(); fillOpen = false; }
            strokeOpen = false;
            continue;
        }
        float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), deltaDBtoY(deltaData[bin]));
        if (!strokeOpen) { stroke.startNewSubPath(x, y); strokeOpen = true; }
        else { stroke.lineTo(x, y); }
        if (!fillOpen) { fill.startNewSubPath(x, midY); fill.lineTo(x, y); fillOpen = true; }
        else { fill.lineTo(x, y); }
    }
    if (fillOpen) { fill.lineTo(binToX(nb - 1), midY); fill.closeSubPath(); }
    g.setColour(col.withAlpha(0.9f));
    g.strokePath(stroke, juce::PathStrokeType(2.0f * scaleFactor, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(col.withAlpha(0.18f));
    g.fillPath(fill);
}

void SpectralCompareAudioProcessorEditor::drawMorphedSpectrum(juce::Graphics& g)
{
    auto ca = spectrumArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    const juce::Colour col = audioProcessor.getMorphColor();
    const auto& data = morphIsFrozen ? frozenMorphDisplay : morphDisplayData;

    const float morphColAlpha = col.getAlpha() / 255.0f;

    if (!interpolate)
    {
        g.setColour(col.withAlpha(morphColAlpha * 0.30f));
        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight()) continue;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));
            float h = (float)ca.getBottom() - y;
            if (h <= 0.0f) continue;
            float nextX = (bin + 1 < nb) ? binToX(bin + 1) : x + 1.0f;
            float w = juce::jmax(1.0f, nextX - x - 1.0f);
            g.fillRect(x, y, w, h);
        }
        return;
    }

    {
        juce::Path fillPath, strokePath;
        float lastXM = 0.0f;
        bool inSegM = false;

        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight())
            {
                if (inSegM) { fillPath.lineTo(lastXM, (float)ca.getBottom()); fillPath.closeSubPath(); inSegM = false; }
                continue;
            }
            const bool hasSigM = data[bin] >= kSilenceFloor;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));

            if (hasSigM)
            {
                if (!inSegM) { fillPath.startNewSubPath(x, (float)ca.getBottom()); fillPath.lineTo(x, y); strokePath.startNewSubPath(x, y); inSegM = true; }
                else { fillPath.lineTo(x, y); strokePath.lineTo(x, y); }
                lastXM = x;
            }
            else
            {
                if (inSegM) { fillPath.lineTo(lastXM, (float)ca.getBottom()); fillPath.closeSubPath(); inSegM = false; }
            }
        }
        if (inSegM) { fillPath.lineTo(lastXM, (float)ca.getBottom()); fillPath.closeSubPath(); }

        g.setColour(col.withAlpha(morphColAlpha * 0.30f));
        g.fillPath(fillPath);
        g.setColour(col.withAlpha(morphColAlpha * 0.95f));
        g.strokePath(strokePath, juce::PathStrokeType(2.0f * scaleFactor, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void SpectralCompareAudioProcessorEditor::drawLabels(juce::Graphics& g)
{
    auto ca = spectrumArea();
    g.setFont(11.0f * scaleFactor);
    const juce::Colour tc = audioProcessor.getTextColor();
    float lx = (float)ca.getX() + 8.0f * scaleFactor, ly = (float)ca.getY() + 8.0f * scaleFactor;

    auto entry = [&](juce::Colour col, const juce::String& label) {
        g.setColour(col);
        g.fillRect(lx, ly + 2.0f * scaleFactor, 12.0f * scaleFactor, 8.0f * scaleFactor);
        g.setColour(tc);
        g.drawText(label, (int)lx + (int)(16 * scaleFactor), (int)ly, (int)(160 * scaleFactor), (int)(12 * scaleFactor), juce::Justification::left, false);
        ly += 16.0f * scaleFactor;
        };

    if (showMain)
        entry(audioProcessor.getMainSpectrumColor(),
            "Main" + juce::String(mainIsFrozen ? " [frozen]" : ""));
    if (showSidechain)
        entry(audioProcessor.getSidechainSpectrumColor(),
            "Sidechain" + juce::String(sidechainIsFrozen ? " [frozen]" : ""));
    if (showDelta)
        entry(audioProcessor.getDeltaColor(), "Delta" + juce::String(deltaIsFrozen ? " [frozen]" : ""));
    if (showMorph && audioProcessor.getMorphAmount() > 0.0f)
    {
        const float pct = audioProcessor.getMorphAmount() * 100.0f;
        entry(audioProcessor.getMorphColor(),
            "Morph (" + juce::String((int)std::round(pct)) + "%)" + juce::String(morphIsFrozen ? " [frozen]" : ""));
    }
    if (showOutput)
        entry(audioProcessor.getOutputSpectrumColor(), "Output" + juce::String(outputIsFrozen ? " [frozen]" : ""));
}

void SpectralCompareAudioProcessorEditor::drawHoverInfo(juce::Graphics& g)
{
    auto ca = spectrumArea();
    if (hoverX < ca.getX() || hoverX > ca.getRight()) return;

    // Vertical line follows the mouse
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawVerticalLine((int)hoverX, (float)ca.getY(), (float)ca.getBottom());

    int   bin = juce::jlimit(0, audioProcessor.numBins - 1, xToBin(hoverX));
    float hz = bin * nyquist() / (audioProcessor.numBins - 1);
    juce::String freqStr = (hz >= 1000.0f)
        ? (juce::String(hz / 1000.0f, 2) + " kHz") : (juce::String((int)hz) + " Hz");

    float mainMag = mainDisplayData[bin];
    float sideMag = sidechainDisplayData[bin];
    float mainDB = (mainMag > 0) ? 20.0f * std::log10(mainMag) : -144.0f;
    float sideDB = (sideMag > 0) ? 20.0f * std::log10(sideMag) : -144.0f;

    juce::String info = freqStr
        + "  Main: " + juce::String(mainDB, 1) + " dB"
        + "  Side: " + juce::String(sideDB, 1) + " dB";

    if (showDelta && mainMag > kSilenceFloor && sideMag > kSilenceFloor)
    {
        float deltaDB = mainDB - sideDB;
        info += "  \u0394: " + (deltaDB >= 0 ? juce::String("+") : juce::String("")) + juce::String(deltaDB, 1) + " dB";
    }
    if (showMorph && audioProcessor.getMorphAmount() > 0.0f)
    {
        float morphMag = morphDisplayData[bin];
        float morphDB = (morphMag > 0.0f) ? 20.0f * std::log10(morphMag) : -144.0f;
        info += "  Morph: " + juce::String(morphDB, 1) + " dB";
    }
    if (showOutput)
    {
        float outMag = outputDisplayData[bin];
        float outDB = (outMag > 0.0f) ? 20.0f * std::log10(outMag) : -144.0f;
        info += "  Out: " + juce::String(outDB, 1) + " dB";
    }

    // Text is always centered at the bottom of the canvas — does NOT follow the mouse
    const int th = (int)(18 * scaleFactor);
    const int tw = ca.getWidth() - (int)(16 * scaleFactor);
    const int tx = ca.getX() + (int)(8 * scaleFactor);
    const int ty = ca.getBottom() - th - (int)(4 * scaleFactor);
    g.setColour(audioProcessor.getTextColor());
    g.setFont(11.0f * scaleFactor);
    g.drawText(info, tx, ty, tw, th, juce::Justification::centred, false);
}

int   SpectralCompareAudioProcessorEditor::filterXToBin(float x) const { return xToBin(x); }

float SpectralCompareAudioProcessorEditor::filterYToDB(float y) const
{
    auto ca = spectrumArea();
    float center = (float)ca.getY() + (float)ca.getHeight() * 0.5f;
    return (center - y) / ((float)ca.getHeight() * 0.5f) * kFilterRange;
}

float SpectralCompareAudioProcessorEditor::filterDBtoY(float dB) const
{
    auto ca = spectrumArea();
    float center = (float)ca.getY() + (float)ca.getHeight() * 0.5f;
    return center - (dB / kFilterRange) * ((float)ca.getHeight() * 0.5f);
}

// Converts a canvas Y coordinate to dB on the spectrum amplitude scale (0 at top, -90 at bottom).
float SpectralCompareAudioProcessorEditor::spectrumYToDB(float y) const
{
    auto ca = spectrumArea();
    const float minDB = -90.0f, maxDB = 0.0f;
    float t = (y - (float)ca.getY()) / (float)ca.getHeight();
    return juce::jlimit(minDB, maxDB, maxDB + t * (minDB - maxDB));
}

// Converts a dB value (spectrum amplitude scale) to canvas Y — same result as magToY(pow10(dB/20)).
float SpectralCompareAudioProcessorEditor::spectrumDBtoY(float dB) const
{
    return magToY(std::pow(10.0f, dB / 20.0f));
}

void SpectralCompareAudioProcessorEditor::drawFilterCurves(juce::Graphics& g)
{
    auto ca = spectrumArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    // Faint center-line for main/side filter reference
    const float centerY = (float)ca.getY() + (float)ca.getHeight() * 0.5f;
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine((int)centerY, (float)ca.getX(), (float)ca.getRight());

    // Main / sidechain filter curves — drawn on the ±kFilterRange scale (center = 0 dB boost)
    auto drawFilterScaleCurve = [&](const std::array<float, maxDisplayBins>& curve,
        juce::Colour col, bool isActive)
        {
            juce::Path path;
            bool started = false;
            for (int bin = 0; bin < nb; ++bin)
            {
                float x = binToX(bin);
                if (x < (float)ca.getX() || x >(float)ca.getRight()) continue;
                float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), filterDBtoY(curve[bin]));
                if (!started) { path.startNewSubPath(x, y); started = true; }
                else           path.lineTo(x, y);
            }
            if (!started) return;
            juce::Path filled = path;
            filled.lineTo(binToX(nb - 1), centerY);
            filled.lineTo(binToX(0), centerY);
            filled.closeSubPath();
            g.setColour(col.withAlpha(isActive ? 0.12f : 0.07f));
            g.fillPath(filled);
            g.setColour(col.withAlpha(isActive ? 0.85f : 0.45f));
            g.strokePath(path, juce::PathStrokeType(isActive ? 2.0f * scaleFactor : 1.2f * scaleFactor));
        };

    // Gate / Enhance curves — drawn on the spectrum amplitude scale (0 dB at top, -90 dB at bottom).
    // This lets you see the threshold directly against the spectrum peaks.
    // Gate  : fill downward (below threshold = killed region).
    // Enhance compress-up  : fill downward (below threshold = boosted up to line).
    // Enhance compress-down: fill upward   (above threshold = attenuated down to line).
    const bool attenuateMode = audioProcessor.apvts.getRawParameterValue("enhanceAttenuate")
        ->load(std::memory_order_relaxed) >= 0.5f;

    auto drawSpectrumScaleCurve = [&](const std::array<float, maxDisplayBins>& curve,
        juce::Colour col, bool isActive, bool fillDown)
        {
            juce::Path path;
            bool started = false;
            for (int bin = 0; bin < nb; ++bin)
            {
                float x = binToX(bin);
                if (x < (float)ca.getX() || x >(float)ca.getRight()) continue;
                float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(),
                    spectrumDBtoY(curve[bin]));
                if (!started) { path.startNewSubPath(x, y); started = true; }
                else           path.lineTo(x, y);
            }
            if (!started) return;

            // Fill towards bottom (gate / compress-up) or top (compress-down)
            const float edgeY = fillDown ? (float)ca.getBottom() : (float)ca.getY();
            juce::Path filled = path;
            filled.lineTo(binToX(nb - 1), edgeY);
            filled.lineTo(binToX(0), edgeY);
            filled.closeSubPath();
            g.setColour(col.withAlpha(isActive ? 0.14f : 0.08f));
            g.fillPath(filled);
            g.setColour(col.withAlpha(isActive ? 0.90f : 0.50f));
            g.strokePath(path, juce::PathStrokeType(isActive ? 2.0f * scaleFactor : 1.4f * scaleFactor));
        };

    const bool mainActive = (drawTarget == 0);
    const bool sideActive = (drawTarget == 1);
    const bool gateActive = (drawTarget == 2);
    const bool enhanceActive = (drawTarget == 3);

    // Inactive curves first, active on top
    if (!mainActive)    drawFilterScaleCurve(mainFilterDisplay, audioProcessor.getMainSpectrumColor(), false);
    if (!sideActive)    drawFilterScaleCurve(sidechainFilterDisplay, audioProcessor.getSidechainSpectrumColor(), false);
    if (!gateActive)    drawSpectrumScaleCurve(gateFilterDisplay, juce::Colour(0xffdd4444), false, true);
    if (!enhanceActive) drawSpectrumScaleCurve(enhanceFilterDisplay, juce::Colour(0xff44aa66), false, !attenuateMode);

    if (mainActive)     drawFilterScaleCurve(mainFilterDisplay, audioProcessor.getMainSpectrumColor(), true);
    if (sideActive)     drawFilterScaleCurve(sidechainFilterDisplay, audioProcessor.getSidechainSpectrumColor(), true);
    if (gateActive)     drawSpectrumScaleCurve(gateFilterDisplay, juce::Colour(0xffdd4444), true, true);
    if (enhanceActive)  drawSpectrumScaleCurve(enhanceFilterDisplay, juce::Colour(0xff44aa66), true, !attenuateMode);
}

// ============================================================================
// Waterfall heatmap
// ============================================================================
// ============================================================================
// hzToX — direct frequency → canvas X (log scale, same mapping as binToX)
// ============================================================================
float SpectralCompareAudioProcessorEditor::hzToX(float hz) const
{
    auto ca = spectrumArea();
    const float logMin = std::log10(juce::jmax(viewFreqMin, 1.0f));
    const float logMax = std::log10(juce::jmax(viewFreqMax, viewFreqMin + 1.0f));
    const float t = (std::log10(juce::jmax(hz, 1.0f)) - logMin) / (logMax - logMin);
    return ca.getX() + t * ca.getWidth();
}

// ============================================================================
// drawReassignedSpectrum — scatter-plot periodogram for one reassigned frame.
//
// Each reassigned event becomes a thin vertical bar (1.5 px wide) at its
// exact reassigned frequency, height proportional to magnitude.  The
// Interpolate button has no effect in this mode.
// ============================================================================
void SpectralCompareAudioProcessorEditor::drawReassignedSpectrum(
    juce::Graphics& g,
    const ReassignFrame& frame,
    juce::Colour colour, float alpha)
{
    auto ca = spectrumArea();
    if (ca.isEmpty() || frame.freqHz.empty()) return;

    const float colAlpha = colour.getAlpha() / 255.0f;
    const float barW = juce::jmax(1.0f, 1.5f * scaleFactor);
    const float bottom = float(ca.getBottom());

    g.setColour(colour.withAlpha(colAlpha * alpha * 0.35f));

    for (size_t i = 0; i < frame.freqHz.size(); ++i)
    {
        const float x = hzToX(frame.freqHz[i]);
        if (x < float(ca.getX()) || x > float(ca.getRight())) continue;

        if (frame.magLin[i] < kSilenceFloor) continue;

        const float y = juce::jlimit(float(ca.getY()), bottom, magToY(frame.magLin[i]));
        const float h = bottom - y;
        if (h <= 0.0f) continue;

        g.fillRect(x - barW * 0.5f, y, barW, h);
    }

    // Draw a brighter stroke line at the top of each bar for legibility.
    g.setColour(colour.withAlpha(colAlpha * alpha));
    for (size_t i = 0; i < frame.freqHz.size(); ++i)
    {
        const float x = hzToX(frame.freqHz[i]);
        if (x < float(ca.getX()) || x > float(ca.getRight())) continue;
        if (frame.magLin[i] < kSilenceFloor) continue;
        const float y = juce::jlimit(float(ca.getY()), bottom, magToY(frame.magLin[i]));
        g.fillRect(x - barW * 0.5f, y, barW, juce::jmin(2.0f * scaleFactor, bottom - y));
    }
}

// ============================================================================
// updateHeatmapReassign — scatter-plot into the heatmap's rightmost column.
//
// Replaces updateHeatmap() when Reassign mode is active.  The scrolling is
// identical; painting uses the reassigned (freqHz, magLin) pairs instead of
// the binned magnitude arrays.
// ============================================================================
void SpectralCompareAudioProcessorEditor::updateHeatmapReassign()
{
    auto ha = heatmapArea();
    const int imgW = ha.getWidth();
    const int imgH = ha.getHeight();
    if (imgW <= 0 || imgH <= 0) return;

    if (heatmapImage.getWidth() != imgW || heatmapImage.getHeight() != imgH)
        heatmapImage = juce::Image(juce::Image::ARGB, imgW, imgH, true);

    // Scroll left by one pixel — same as the standard heatmap.
    heatmapImage.moveImageSection(0, 0, 1, 0, imgW - 1, imgH);

    const float logMin = std::log10(juce::jmax(viewFreqMin, 1.0f));
    const float logMax = std::log10(juce::jmax(viewFreqMax, viewFreqMin + 1.0f));
    const float logRange = logMax - logMin;
    if (logRange <= 0.0f) return;

    // Build a per-pixel-row accumulator for each signal.
    // We support main and sidechain (same four-signal struct as updateHeatmap,
    // but filled from reassigned frames instead of bin arrays).

    struct SigAccum {
        std::vector<float> maxMag;   // per row, linear mag
        float cr, cg, cb, ca_sig;
        bool  visible;
    };

    SigAccum sigs[2] = {
        { std::vector<float>(size_t(imgH), 0.0f),
          audioProcessor.getMainSpectrumColor().getFloatRed(),
          audioProcessor.getMainSpectrumColor().getFloatGreen(),
          audioProcessor.getMainSpectrumColor().getFloatBlue(),
          audioProcessor.getMainSpectrumColor().getAlpha() / 255.0f,
          showMain && hasReassignMain },
        { std::vector<float>(size_t(imgH), 0.0f),
          audioProcessor.getSidechainSpectrumColor().getFloatRed(),
          audioProcessor.getSidechainSpectrumColor().getFloatGreen(),
          audioProcessor.getSidechainSpectrumColor().getFloatBlue(),
          audioProcessor.getSidechainSpectrumColor().getAlpha() / 255.0f,
          showSidechain && hasReassignSide },
    };

    const ReassignFrame* frames[2] =
    { &reassignMainFrame, &reassignSideFrame };

    for (int s = 0; s < 2; ++s)
    {
        if (!sigs[s].visible) continue;
        const auto& frame = *frames[s];
        for (size_t i = 0; i < frame.freqHz.size(); ++i)
        {
            const float f = frame.freqHz[i];
            if (f < viewFreqMin || f > viewFreqMax) continue;
            const float norm = (std::log2(f) - std::log2(viewFreqMin))
                / (std::log2(viewFreqMax) - std::log2(viewFreqMin));
            const int   row = juce::jlimit(0, imgH - 1,
                int((1.0f - norm) * float(imgH - 1)));
            sigs[s].maxMag[size_t(row)] = juce::jmax(sigs[s].maxMag[size_t(row)],
                frame.magLin[i]);
        }
    }

    // Paint the rightmost column from the accumulators.
    juce::Image::BitmapData bmp(heatmapImage, juce::Image::BitmapData::readWrite);

    for (int py = 0; py < imgH; ++py)
    {
        float r = 0.0f, gv = 0.0f, b = 0.0f, maxIntensity = 0.0f;

        for (int s = 0; s < 2; ++s)
        {
            if (!sigs[s].visible) continue;
            const float mag = sigs[s].maxMag[size_t(py)];
            const float dB = (mag > 1e-9f) ? 20.0f * std::log10(mag) : -90.0f;
            const float norm = juce::jlimit(0.0f, 1.0f, (dB + 80.0f) / 80.0f);
            const float intensity = norm * norm * sigs[s].ca_sig;
            r += sigs[s].cr * intensity;
            gv += sigs[s].cg * intensity;
            b += sigs[s].cb * intensity;
            maxIntensity = juce::jmax(maxIntensity, intensity);
        }

        const float pixAlpha = juce::jlimit(0.0f, 1.0f, maxIntensity * 2.0f);
        auto* px = bmp.getPixelPointer(imgW - 1, py);
        px[0] = juce::uint8(juce::jlimit(0.0f, 1.0f, b) * 255.0f);
        px[1] = juce::uint8(juce::jlimit(0.0f, 1.0f, gv) * 255.0f);
        px[2] = juce::uint8(juce::jlimit(0.0f, 1.0f, r) * 255.0f);
        px[3] = juce::uint8(pixAlpha * 255.0f);
    }
}

void SpectralCompareAudioProcessorEditor::updateHeatmap()
{
    auto ha = heatmapArea();
    const int imgW = ha.getWidth();
    const int imgH = ha.getHeight();
    if (imgW <= 0 || imgH <= 0) return;

    if (heatmapImage.getWidth() != imgW || heatmapImage.getHeight() != imgH)
    {
        heatmapImage = juce::Image(juce::Image::ARGB, imgW, imgH, true);
        // Start transparent — background will show through silent areas
    }

    heatmapImage.moveImageSection(0, 0, 1, 0, imgW - 1, imgH);

    struct Signal {
        const std::array<float, maxDisplayBins>* data;
        bool  visible;
        float cr, cg, cb, ca_sig;   // color rgb + color alpha (from user setting)
    };

    const Signal signals[] = {
        { mainIsFrozen ? &frozenMainDisplay : &mainDisplayData,
          showMain,
          audioProcessor.getMainSpectrumColor().getFloatRed(),
          audioProcessor.getMainSpectrumColor().getFloatGreen(),
          audioProcessor.getMainSpectrumColor().getFloatBlue(),
          audioProcessor.getMainSpectrumColor().getAlpha() / 255.0f },
        { sidechainIsFrozen ? &frozenSidechainDisplay : &sidechainDisplayData,
          showSidechain,
          audioProcessor.getSidechainSpectrumColor().getFloatRed(),
          audioProcessor.getSidechainSpectrumColor().getFloatGreen(),
          audioProcessor.getSidechainSpectrumColor().getFloatBlue(),
          audioProcessor.getSidechainSpectrumColor().getAlpha() / 255.0f },
        { morphIsFrozen ? &frozenMorphDisplay : &morphDisplayData,
          showMorph && (audioProcessor.getMorphAmount() > 0.0f),
          audioProcessor.getMorphColor().getFloatRed(),
          audioProcessor.getMorphColor().getFloatGreen(),
          audioProcessor.getMorphColor().getFloatBlue(),
          audioProcessor.getMorphColor().getAlpha() / 255.0f },
        { outputIsFrozen ? &frozenOutputDisplay : &outputDisplayData,
          showOutput,
          audioProcessor.getOutputSpectrumColor().getFloatRed(),
          audioProcessor.getOutputSpectrumColor().getFloatGreen(),
          audioProcessor.getOutputSpectrumColor().getFloatBlue(),
          audioProcessor.getOutputSpectrumColor().getAlpha() / 255.0f },
    };

    const int   nb = audioProcessor.numBins;
    const float nyq = nyquist();
    const float logMin = std::log10(juce::jmax(viewFreqMin, 1.0f));
    const float logMax = std::log10(juce::jmax(viewFreqMax, viewFreqMin + 1.0f));
    const float logRange = logMax - logMin;

    juce::Image::BitmapData bmp(heatmapImage, juce::Image::BitmapData::readWrite);

    for (int py = 0; py < imgH; ++py)
    {
        const float freqNorm = 1.0f - (float)py / (float)(imgH - 1);
        const float freq = std::pow(10.0f, logMin + freqNorm * logRange);
        const int   bin = juce::jlimit(0, nb - 1, juce::roundToInt(freq * (float)(nb - 1) / nyq));

        float r = 0.0f, gv = 0.0f, b = 0.0f;
        float maxIntensity = 0.0f;

        for (const auto& s : signals)
        {
            if (!s.visible) continue;
            const float mag = (*s.data)[bin];
            const float dB = (mag > 1e-9f) ? 20.0f * std::log10(mag) : -90.0f;
            const float norm = juce::jlimit(0.0f, 1.0f, (dB + 80.0f) / 80.0f);
            const float intensity = norm * norm * s.ca_sig;
            r += s.cr * intensity;
            gv += s.cg * intensity;
            b += s.cb * intensity;
            maxIntensity = juce::jmax(maxIntensity, intensity);
        }

        // Delta contribution (absolute dB difference)
        if (showDelta)
        {
            const auto& deltaArr = deltaIsFrozen ? frozenDeltaDisplay : smoothedDeltaDisplay;
            const float absDelta = std::abs(deltaArr[bin]);
            const juce::Colour dc = audioProcessor.getDeltaColor();
            const float intensity = juce::jlimit(0.0f, 1.0f, absDelta / kDeltaRange)
                * (dc.getAlpha() / 255.0f);
            r += dc.getFloatRed() * intensity;
            gv += dc.getFloatGreen() * intensity;
            b += dc.getFloatBlue() * intensity;
            maxIntensity = juce::jmax(maxIntensity, intensity);
        }

        // Alpha: how much signal is present. Silent bins = fully transparent,
        // so background colour shows through regardless of what it is.
        const float pixAlpha = juce::jlimit(0.0f, 1.0f, maxIntensity * 2.0f);

        auto* px = bmp.getPixelPointer(imgW - 1, py);
        px[0] = (juce::uint8)(juce::jlimit(0.0f, 1.0f, b) * 255.0f);
        px[1] = (juce::uint8)(juce::jlimit(0.0f, 1.0f, gv) * 255.0f);
        px[2] = (juce::uint8)(juce::jlimit(0.0f, 1.0f, r) * 255.0f);
        px[3] = (juce::uint8)(pixAlpha * 255.0f);
    }
}

void SpectralCompareAudioProcessorEditor::drawHeatmap(juce::Graphics& g)
{
    auto ha = heatmapArea();
    if (ha.getWidth() <= 0 || ha.getHeight() <= 0) return;

    // FIX 3: Draw divider with explicit background fill for the no-image case
    if (heatmapImage.isValid())
        g.drawImage(heatmapImage, ha.getX(), ha.getY(), ha.getWidth(), ha.getHeight(),
            0, 0, heatmapImage.getWidth(), heatmapImage.getHeight());
    else
    {
        g.setColour(audioProcessor.getBackgroundColor());
        g.fillRect(ha);
    }

    // Draggable divider line
    const juce::Colour gc = audioProcessor.getGridColor();
    g.setColour(gc.withAlpha(isDraggingSplit ? 0.9f : 0.55f));
    g.drawHorizontalLine(ha.getY(), (float)ha.getX(), (float)ha.getRight());
    g.setColour(gc.withAlpha(isDraggingSplit ? 0.35f : 0.15f));
    g.drawHorizontalLine(ha.getY() - 1, (float)ha.getX(), (float)ha.getRight());
    g.drawHorizontalLine(ha.getY() + 1, (float)ha.getX(), (float)ha.getRight());

    // Frequency grid lines + labels
    const juce::Colour tc = audioProcessor.getTextColor().withAlpha(0.65f);
    const float        logMin = std::log10(juce::jmax(viewFreqMin, 1.0f));
    const float        logMax = std::log10(juce::jmax(viewFreqMax, viewFreqMin + 1.0f));
    const float        logRange = logMax - logMin;
    const float        nyq = nyquist();

    static const float kHeatmapFreqMarkers[] = { 50,100,200,500,1000,2000,5000,10000,20000 };
    g.setFont(9.0f * scaleFactor);
    for (float f : kHeatmapFreqMarkers)
    {
        if (f > nyq || f < viewFreqMin || f > viewFreqMax) continue;
        const float freqNorm = (std::log10(f) - logMin) / logRange;
        const int   fy = ha.getBottom() - (int)(freqNorm * (float)ha.getHeight());
        if (fy < ha.getY() || fy > ha.getBottom()) continue;
        g.setColour(gc.withAlpha(0.18f));
        g.drawHorizontalLine(fy, (float)ha.getX(), (float)ha.getRight());
        g.setColour(tc);
        juce::String label = (f >= 1000) ? (juce::String(f / 1000, 0) + "k") : juce::String((int)f);
        g.drawText(label, ha.getX() + (int)(3 * scaleFactor), fy - (int)(9 * scaleFactor),
            (int)(34 * scaleFactor), (int)(9 * scaleFactor), juce::Justification::left, false);
    }

    g.setColour(tc);
    g.setFont(9.5f * scaleFactor);
    g.drawText("Waterfall",
        ha.getRight() - (int)(64 * scaleFactor), ha.getY() + (int)(3 * scaleFactor),
        (int)(60 * scaleFactor), (int)(11 * scaleFactor), juce::Justification::right, false);
}

// ============================================================================
// Resized
// ============================================================================
void SpectralCompareAudioProcessorEditor::resized()
{
    scaleFactor = (float)getWidth() / (float)kBaseWidth;

    const bool vis = sidebarVisible;
    generalLabel.setVisible(vis);   visualsLabel.setVisible(vis);
    audioLabel.setVisible(vis);     morphingLabel.setVisible(vis);
    SmoothingLabel.setVisible(vis);

    fftSizeCombo.setVisible(vis); freqFromKnob.setVisible(vis); freqToKnob.setVisible(vis);
    fftSizeLabel.setVisible(vis); freqFromLabel.setVisible(vis); freqToLabel.setVisible(vis);

    showMainButton.setVisible(vis);      freezeMainButton.setVisible(vis);
    showSidechainButton.setVisible(vis); freezeSidechainButton.setVisible(vis);
    showDeltaButton.setVisible(vis);     freezeDeltaButton.setVisible(vis);
    showMorphButton.setVisible(vis);     freezeMorphButton.setVisible(vis);
    showOutputButton.setVisible(vis);    freezeOutputButton.setVisible(vis);

    mainSmoothSlider.setVisible(vis);     sidechainSmoothSlider.setVisible(vis);
    deltaSmoothSlider.setVisible(vis);    morphSmoothSlider.setVisible(vis);
    outputSmoothSlider.setVisible(vis);

    bgColorInput.setVisible(vis);     gridColorInput.setVisible(vis);
    sidebarColorInput.setVisible(vis); mainColorInput.setVisible(vis);
    sidechainColorInput.setVisible(vis); deltaColorInput.setVisible(vis);
    morphColorInput.setVisible(vis);  outputColorInput.setVisible(vis);
    textColorInput.setVisible(vis);

    for (auto& lbl : colorCatLabels) lbl.setVisible(vis);

    resetVisualsButton.setVisible(vis); resetColorsButton.setVisible(vis);
    interpolateButton.setVisible(vis);

    hearDeltaButton.setVisible(vis);     monitorSideButton.setVisible(vis);
    reassignButton.setVisible(vis);
    gateEnableButton.setVisible(vis);
    gateLowHzLabel.setVisible(vis);
    gateHighHzLabel.setVisible(vis);     gateBinStartKnob.setVisible(vis);
    gateBinEndKnob.setVisible(vis);

    enhanceEnableButton.setVisible(vis); enhanceModeButton.setVisible(vis);
    enhLowHzLabel.setVisible(vis);       enhHighHzLabel.setVisible(vis);
    enhanceBinStartKnob.setVisible(vis); enhanceBinEndKnob.setVisible(vis);

    morphSlider.setVisible(vis);       morphAmtLabel.setVisible(vis);
    claritySlider.setVisible(vis);     clarityAmtLabel.setVisible(vis);
    morphSmoothToggle.setVisible(vis); drawTargetCombo.setVisible(vis);
    resetDrawsButton.setVisible(vis);  heatmapToggleButton.setVisible(vis);

    heatmapImage = juce::Image();  // rebuild at new dimensions

    const int buttonW = (int)(100 * scaleFactor);
    const int buttonH = (int)(kRowH * scaleFactor);
    const int buttonPad = (int)(kPad * scaleFactor);

    if (!sidebarVisible)
    {
        toggleSidebarButton.setBounds(getWidth() - buttonW - buttonPad, buttonPad, buttonW, buttonH);
        return;
    }

    const int scaledSidebarW = (int)(kSidebarW * scaleFactor);
    const int scaledPad = (int)(kPad * scaleFactor);
    const int scaledColGap = (int)(kColGap * scaleFactor);
    const int scaledRowH = (int)(kRowH * scaleFactor);
    const int scaledRowGap = (int)(kRowGap * scaleFactor);
    const int scaledSectionGap = (int)(kSectionGap * scaleFactor);
    const int scaledHeaderH = (int)(kHeaderH * scaleFactor);
    const int scaledUsableW = scaledSidebarW - 2 * scaledPad;
    const int scaledColW = (scaledUsableW - 2 * scaledColGap) / 3;

    const int sx = getWidth() - scaledSidebarW + scaledPad;
    int y = scaledPad;

    auto cell = [&](int col) -> juce::Rectangle<int> {
        return { sx + col * (scaledColW + scaledColGap), y, scaledColW, scaledRowH };
        };
    auto nextRow = [&]() { y += scaledRowH + scaledRowGap; };
    auto sectionHeader = [&](juce::Label& lbl) {
        lbl.setBounds(sx, y, scaledUsableW, scaledHeaderH);
        y += scaledHeaderH + scaledRowGap;
        };
    auto spacer = [&]() { y += scaledSectionGap; };
    auto fullRow = [&](juce::Component& c) { c.setBounds(sx, y, scaledUsableW, scaledRowH); nextRow(); };
    const int scaledSliderLblW = (int)(62 * scaleFactor);
    auto labeledRow = [&](juce::Label& lbl, juce::Slider& s) {
        lbl.setBounds(sx, y, scaledSliderLblW, scaledRowH);
        s.setBounds(sx + scaledSliderLblW + 2, y, scaledUsableW - scaledSliderLblW - 2, scaledRowH);
        nextRow();
        };
    const int scaledColorLabelH = (int)(14 * scaleFactor);
    auto colorLabelRow = [&](int labelIdx0) {
        colorCatLabels[labelIdx0 + 0].setBounds(sx + 0 * (scaledColW + scaledColGap), y, scaledColW, scaledColorLabelH);
        colorCatLabels[labelIdx0 + 1].setBounds(sx + 1 * (scaledColW + scaledColGap), y, scaledColW, scaledColorLabelH);
        colorCatLabels[labelIdx0 + 2].setBounds(sx + 2 * (scaledColW + scaledColGap), y, scaledColW, scaledColorLabelH);
        y += scaledColorLabelH + 2;
        };

    // General Controls header row
    {
        const int rowH = juce::jmax(buttonH, scaledHeaderH);
        const int labelW = scaledUsableW - buttonW - scaledColGap;
        const int labelYOff = (rowH - scaledHeaderH) / 2;
        const int btnYOff = (rowH - buttonH) / 2;
        generalLabel.setBounds(sx, y + labelYOff, labelW, scaledHeaderH);
        heatmapToggleButton.setBounds(cell(1));
        toggleSidebarButton.setBounds(cell(2));
        y += rowH + scaledRowGap;
    }

    fftSizeLabel.setBounds(cell(0)); freqFromLabel.setBounds(cell(1)); freqToLabel.setBounds(cell(2)); nextRow();
    fftSizeCombo.setBounds(cell(0)); freqFromKnob.setBounds(cell(1));  freqToKnob.setBounds(cell(2));  nextRow();
    spacer();

    visualsLabel.setBounds(sx, y, scaledColW, scaledHeaderH);
    SmoothingLabel.setBounds(sx + 1 * (scaledColW + scaledColGap), y, scaledColW * 2 + scaledColGap, scaledHeaderH);
    y += scaledHeaderH + scaledRowGap;

    showMainButton.setBounds(cell(0));      mainSmoothSlider.setBounds(cell(1));      freezeMainButton.setBounds(cell(2));      nextRow();
    showSidechainButton.setBounds(cell(0)); sidechainSmoothSlider.setBounds(cell(1)); freezeSidechainButton.setBounds(cell(2)); nextRow();
    showDeltaButton.setBounds(cell(0));     deltaSmoothSlider.setBounds(cell(1));     freezeDeltaButton.setBounds(cell(2));     nextRow();
    showMorphButton.setBounds(cell(0));     morphSmoothSlider.setBounds(cell(1));     freezeMorphButton.setBounds(cell(2));     nextRow();
    showOutputButton.setBounds(cell(0));    outputSmoothSlider.setBounds(cell(1));    freezeOutputButton.setBounds(cell(2));    nextRow();

    y += kRowGap;
    colorLabelRow(0); bgColorInput.setBounds(cell(0)); gridColorInput.setBounds(cell(1)); sidebarColorInput.setBounds(cell(2)); nextRow();
    colorLabelRow(3); mainColorInput.setBounds(cell(0)); sidechainColorInput.setBounds(cell(1)); deltaColorInput.setBounds(cell(2)); nextRow();
    colorLabelRow(6); morphColorInput.setBounds(cell(0)); outputColorInput.setBounds(cell(1)); textColorInput.setBounds(cell(2)); nextRow();

    y += kRowGap;
    resetVisualsButton.setBounds(cell(0)); resetColorsButton.setBounds(cell(1)); interpolateButton.setBounds(cell(2)); nextRow();
    spacer();

    sectionHeader(audioLabel);
    hearDeltaButton.setBounds(cell(0)); monitorSideButton.setBounds(cell(1)); reassignButton.setBounds(cell(2)); nextRow();
    gateEnableButton.setBounds(cell(0)); nextRow();
    labeledRow(gateLowHzLabel, gateBinStartKnob);
    labeledRow(gateHighHzLabel, gateBinEndKnob);
    enhanceEnableButton.setBounds(cell(0)); enhanceModeButton.setBounds(cell(1)); nextRow();
    labeledRow(enhLowHzLabel, enhanceBinStartKnob);
    labeledRow(enhHighHzLabel, enhanceBinEndKnob);
    spacer();

    sectionHeader(morphingLabel);
    labeledRow(morphAmtLabel, morphSlider);
    labeledRow(clarityAmtLabel, claritySlider);
    morphSmoothToggle.setBounds(cell(0)); drawTargetCombo.setBounds(cell(1)); resetDrawsButton.setBounds(cell(2));
}

// ============================================================================
// Color appliers
// ============================================================================
void SpectralCompareAudioProcessorEditor::applyBgColor()
{
    juce::Colour c;
    if (!parseHexColour(bgColorInput.getText(), c)) return;
    audioProcessor.setBackgroundColor(c);
    bgColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applyGridColor()
{
    juce::Colour c;
    if (!parseHexColour(gridColorInput.getText(), c)) return;
    audioProcessor.setGridColor(c);
    gridColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applyMainColor()
{
    juce::Colour c;
    if (!parseHexColour(mainColorInput.getText(), c)) return;
    audioProcessor.setMainSpectrumColor(c);
    mainColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applySidechainColor()
{
    juce::Colour c;
    if (!parseHexColour(sidechainColorInput.getText(), c)) return;
    audioProcessor.setSidechainSpectrumColor(c);
    sidechainColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applyDeltaColor()
{
    juce::Colour c;
    if (!parseHexColour(deltaColorInput.getText(), c)) return;
    audioProcessor.setDeltaColor(c);
    deltaColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applyMorphColor()
{
    juce::Colour c;
    if (!parseHexColour(morphColorInput.getText(), c)) return;
    audioProcessor.setMorphColor(c);
    morphColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applyOutputColor()
{
    juce::Colour c;
    if (!parseHexColour(outputColorInput.getText(), c)) return;
    audioProcessor.setOutputSpectrumColor(c);
    outputColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applySidebarColor()
{
    juce::Colour c;
    if (!parseHexColour(sidebarColorInput.getText(), c)) return;
    audioProcessor.setSidebarColor(c);
    sidebarColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}
void SpectralCompareAudioProcessorEditor::applyTextColor()
{
    juce::Colour c;
    if (!parseHexColour(textColorInput.getText(), c)) return;
    audioProcessor.setTextColor(c);
    textColorInput.setText(colourToHex(c), juce::dontSendNotification);
    refreshUIColors();
}