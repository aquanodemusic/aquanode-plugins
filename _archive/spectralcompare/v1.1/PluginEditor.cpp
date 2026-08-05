#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
// Style helpers
// ============================================================================
static void styleLabel(juce::Label& l)
{
    l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    l.setJustificationType(juce::Justification::centredRight);
}

static void styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a2a));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff55eedd));
    b.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
}

static void styleTextEditor(juce::TextEditor& e)
{
    e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    e.setColour(juce::TextEditor::textColourId,       juce::Colours::lightgrey);
    e.setColour(juce::TextEditor::outlineColourId,    juce::Colour(0xff444444));
    e.setFont(11.0f);
}

static void styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    c.setColour(juce::ComboBox::textColourId,       juce::Colours::lightgrey);
    c.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff444444));
    c.setColour(juce::ComboBox::arrowColourId,      juce::Colours::lightgrey);
}

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

static juce::String colourToHex(juce::Colour c)
{
    return c.toDisplayString(false).toUpperCase();
}

// ============================================================================
// Constructor
// ============================================================================
SpectralCompareAudioProcessorEditor::SpectralCompareAudioProcessorEditor(
    SpectralCompareAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(1000, 600);

    // ---- FFT Size ----
    styleLabel(fftSizeLabel);
    fftSizeLabel.setText("FFT Size:", juce::dontSendNotification);
    addAndMakeVisible(fftSizeLabel);

    fftSizeCombo.addItem("1024", 1);
    fftSizeCombo.addItem("2048", 2);
    fftSizeCombo.addItem("4096", 3);
    fftSizeCombo.addItem("8192", 4);
    styleCombo(fftSizeCombo);
    // Note: ComboBoxAttachment below will set the selected item from APVTS state.
    addAndMakeVisible(fftSizeCombo);

    // ---- Morph slider ----
    styleLabel(morphLabel);
    morphLabel.setText("Morph:", juce::dontSendNotification);
    addAndMakeVisible(morphLabel);

    morphSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    morphSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    // Range is set by SliderAttachment from APVTS parameter definition.
    morphSlider.setTooltip("0 = dry  |  1 = full envelope transfer  |  >1 = hyper-morph: exaggerates spectral contrast beyond the sidechain's own shape");
    addAndMakeVisible(morphSlider);

    // ---- Clarity (envelope width) slider ----
    // Controls the log-scaled half-width fraction of the spectral envelope filter.
    // Low = narrow window (fine harmonic detail bleeds into the envelope ratio,
    //       crisper/grittier morph). High = wide window (smooth timbral transfer,
    //       closer to a "vocal formant" style). Mirrors EB Morph's "Clarity" param.
    styleLabel(clarityLabel);
    clarityLabel.setText("Clarity:", juce::dontSendNotification);
    clarityLabel.setTooltip("Envelope smoothing bandwidth: low = detailed, high = broad/smooth");
    addAndMakeVisible(clarityLabel);

    claritySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    claritySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    // Range is set by SliderAttachment from APVTS parameter definition.
    claritySlider.setTooltip("0.05 = narrow (~1/20 oct, crisp)  |  0.25 = default (~1/4 oct)  |  0.5 = wide (~1/2 oct, smooth)");
    addAndMakeVisible(claritySlider);

    // ---- Show/Freeze button pairs ----
    // Helper lambdas capture by reference
    auto makeShowFreeze = [&](juce::TextButton& showBtn,  const juce::String& showLabel,
                               juce::TextButton& freezeBtn, const juce::String& freezeLabel,
                               bool& showFlag, bool& frozenFlag,
                               std::array<float, maxDisplayBins>& liveData,
                               std::array<float, maxDisplayBins>& frozenData)
    {
        styleButton(showBtn);
        showBtn.setButtonText(showLabel);
        showBtn.setClickingTogglesState(true);
        showBtn.setToggleState(showFlag, juce::dontSendNotification);
        showBtn.onClick = [&, showLabel]() {
            showFlag = showBtn.getToggleState();
            showBtn.setButtonText(showFlag ? showLabel + ": ON" : showLabel);
            repaint(canvasArea());
        };
        addAndMakeVisible(showBtn);

        styleButton(freezeBtn);
        freezeBtn.setButtonText(freezeLabel);
        freezeBtn.setClickingTogglesState(true);
        freezeBtn.onClick = [&, freezeLabel]() {
            frozenFlag = freezeBtn.getToggleState();
            if (frozenFlag) frozenData = liveData;
            freezeBtn.setButtonText(frozenFlag ? freezeLabel + ": ON" : freezeLabel);
        };
        addAndMakeVisible(freezeBtn);
    };

    makeShowFreeze(showMainButton,      "Show Main",  freezeMainButton,      "Freeze Main",
                   showMain,      mainIsFrozen,      mainDisplayData,      frozenMainDisplay);
    makeShowFreeze(showSidechainButton, "Show Side",  freezeSidechainButton, "Freeze Side",
                   showSidechain, sidechainIsFrozen, sidechainDisplayData, frozenSidechainDisplay);
    makeShowFreeze(showDeltaButton,     "Show Delta", freezeDeltaButton,     "Freeze Delta",
                   showDelta,     deltaIsFrozen,     smoothedDeltaDisplay, frozenDeltaDisplay);
    makeShowFreeze(showMorphButton,     "Show Morph", freezeMorphButton,     "Freeze Morph",
                   showMorph,     morphIsFrozen,     morphDisplayData,     frozenMorphDisplay);

    // initialise toggle states
    showMainButton.setToggleState(true,  juce::dontSendNotification);
    showSidechainButton.setToggleState(true,  juce::dontSendNotification);
    showDeltaButton.setToggleState(false, juce::dontSendNotification);
    showMorphButton.setToggleState(true,  juce::dontSendNotification);

    // ---- Interpolate ----
    styleButton(interpolateButton);
    interpolateButton.setButtonText("Interpolate: ON");
    interpolateButton.setClickingTogglesState(true);
    interpolateButton.setToggleState(true, juce::dontSendNotification);
    interpolateButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff888888));
    interpolateButton.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
    interpolateButton.onClick = [this]() {
        interpolate = interpolateButton.getToggleState();
        interpolateButton.setButtonText(interpolate ? "Interpolate: ON" : "Interpolate: OFF");
    };
    addAndMakeVisible(interpolateButton);

    // ---- Smooth sliders helper ----
    // Note: onValueChange callbacks are NOT set here — SliderAttachments below
    // handle writing to the processor parameters.  The deltaSmoothSlider value
    // is read directly in timerCallback (editor-side) so no callback is needed.
    auto makeSmooth = [&](juce::Slider& sl, juce::Label& lbl,
                          const juce::String& labelText)
    {
        styleLabel(lbl);
        lbl.setText(labelText, juce::dontSendNotification);
        addAndMakeVisible(lbl);

        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        // Range is set by SliderAttachment from APVTS parameter definition.
        sl.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a2a));
        addAndMakeVisible(sl);
    };

    makeSmooth(mainSmoothSlider,      mainSmoothLabel,      "Smooth Main:");
    makeSmooth(sidechainSmoothSlider, sidechainSmoothLabel, "Smooth Side:");
    makeSmooth(deltaSmoothSlider,     deltaSmoothLabel,     "Smooth Delta:");
    makeSmooth(morphSmoothSlider,     morphSmoothLabel,     "Smooth Morph:");

    // ---- Color picker rows ----
    auto setupColorRow = [&](juce::Label& lbl, const juce::String& text,
                              juce::TextEditor& inp, juce::Colour initialColor,
                              std::function<void()> onReturn)
    {
        styleLabel(lbl);
        lbl.setText(text, juce::dontSendNotification);
        addAndMakeVisible(lbl);

        styleTextEditor(inp);
        inp.setText(colourToHex(initialColor), juce::dontSendNotification);
        inp.onReturnKey = onReturn;
        inp.onFocusLost = onReturn;
        addAndMakeVisible(inp);
    };

    setupColorRow(bgColorLabel,        "BG:",       bgColorInput,        audioProcessor.getBackgroundColor(),        [this]{ applyBgColor(); });
    setupColorRow(gridColorLabel,      "Grid:",     gridColorInput,      audioProcessor.getGridColor(),              [this]{ applyGridColor(); });
    setupColorRow(mainColorLabel,      "Main:",     mainColorInput,      audioProcessor.getMainSpectrumColor(),      [this]{ applyMainColor(); });
    setupColorRow(sidechainColorLabel, "Sidechain:",sidechainColorInput, audioProcessor.getSidechainSpectrumColor(), [this]{ applySidechainColor(); });
    setupColorRow(deltaColorLabel,     "Delta:",    deltaColorInput,     audioProcessor.getDeltaColor(),             [this]{ applyDeltaColor(); });
    setupColorRow(morphColorLabel,     "Morph:",    morphColorInput,     audioProcessor.getMorphColor(),             [this]{ applyMorphColor(); });
    setupColorRow(sidebarColorLabel,   "Sidebar:",  sidebarColorInput,   audioProcessor.getSidebarColor(),           [this]{ applySidebarColor(); });

    // ---- Reset Colors ----
    styleButton(resetColorsButton);
    resetColorsButton.setButtonText("Reset Colors");
    resetColorsButton.onClick = [this]() {
        audioProcessor.resetColors();
        bgColorInput.setText(colourToHex(audioProcessor.getBackgroundColor()),        juce::dontSendNotification);
        gridColorInput.setText(colourToHex(audioProcessor.getGridColor()),            juce::dontSendNotification);
        mainColorInput.setText(colourToHex(audioProcessor.getMainSpectrumColor()),    juce::dontSendNotification);
        sidechainColorInput.setText(colourToHex(audioProcessor.getSidechainSpectrumColor()), juce::dontSendNotification);
        deltaColorInput.setText(colourToHex(audioProcessor.getDeltaColor()),          juce::dontSendNotification);
        morphColorInput.setText(colourToHex(audioProcessor.getMorphColor()),          juce::dontSendNotification);
        sidebarColorInput.setText(colourToHex(audioProcessor.getSidebarColor()),      juce::dontSendNotification);
        refreshUIColors();
    };
    addAndMakeVisible(resetColorsButton);

    // ---- Freq knobs ----
    freqKnobLF = std::make_unique<juce::LookAndFeel_V4>();
    freqKnobLF->setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xff888888));
    freqKnobLF->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
    freqKnobLF->setColour(juce::Slider::thumbColourId,               juce::Colour(0xffaaaaaa));
    freqKnobLF->setColour(juce::Slider::textBoxTextColourId,         juce::Colours::lightgrey);
    freqKnobLF->setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(0xff1a1a1a));
    freqKnobLF->setColour(juce::Slider::textBoxOutlineColourId,      juce::Colour(0xff333333));
    freqKnobLF->setColour(juce::Label::textColourId,                 juce::Colours::lightgrey);

    auto setupFreqKnob = [&](juce::Slider& knob, juce::Label& lbl,
                              const juce::String& labelText)
    {
        styleLabel(lbl);
        lbl.setText(labelText, juce::dontSendNotification);
        lbl.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(lbl);

        knob.setLookAndFeel(freqKnobLF.get());
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
        // Range + skew are set by SliderAttachment from the APVTS parameter definition.
        knob.setTextValueSuffix(" Hz");
        addAndMakeVisible(knob);
    };

    setupFreqKnob(freqFromKnob, freqFromLabel, "From");
    setupFreqKnob(freqToKnob,   freqToLabel,   "To");

    // The freq knobs still need onValueChange to update the editor-side
    // viewFreqMin/Max that drive the log-scale display mapping.
    // The attachment keeps the knob value in sync with APVTS — onValueChange
    // fires both on user drag and on DAW automation/preset recall.
    freqFromKnob.onValueChange = [this]() { viewFreqMin = (float)freqFromKnob.getValue(); repaint(canvasArea()); };
    freqToKnob.onValueChange   = [this]() { viewFreqMax = (float)freqToKnob.getValue();   repaint(canvasArea()); };

    // =========================================================================
    // APVTS Attachments
    // Must be created AFTER widgets are added to the component hierarchy
    // and AFTER any onValueChange callbacks are set up.
    // The attachment constructor immediately sets the slider/combo to the
    // current APVTS parameter value, so no manual setValue calls are needed.
    // =========================================================================

    fftSizeAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.apvts, "fftSize", fftSizeCombo);

    morphAttachment   = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "morph", morphSlider);

    clarityAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "clarity", claritySlider);

    mainSmoothAttachment      = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "smoothMain", mainSmoothSlider);
    sidechainSmoothAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "smoothSidechain", sidechainSmoothSlider);
    deltaSmoothAttachment     = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "smoothDelta", deltaSmoothSlider);
    morphSmoothAttachment     = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "smoothMorph", morphSmoothSlider);

    freqFromAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "freqFrom", freqFromKnob);
    freqToAttachment   = std::make_unique<SliderAttachment>(
        audioProcessor.apvts, "freqTo", freqToKnob);

    // Initialise display range from whatever the attachments just restored.
    viewFreqMin = (float)freqFromKnob.getValue();
    viewFreqMax = (float)freqToKnob.getValue();

    // Apply colours to all dynamic UI elements once everything is constructed
    refreshUIColors();

    startTimerHz(60);
}

SpectralCompareAudioProcessorEditor::~SpectralCompareAudioProcessorEditor()
{
    stopTimer();

    // Destroy attachments before their widgets to avoid dangling-reference callbacks.
    fftSizeAttachment.reset();
    morphAttachment.reset();
    clarityAttachment.reset();
    mainSmoothAttachment.reset();
    sidechainSmoothAttachment.reset();
    deltaSmoothAttachment.reset();
    morphSmoothAttachment.reset();
    freqFromAttachment.reset();
    freqToAttachment.reset();

    freqFromKnob.setLookAndFeel(nullptr);
    freqToKnob.setLookAndFeel(nullptr);
}

// ============================================================================
// refreshUIColors — propagate processor colours to all coloured UI elements
// ============================================================================
void SpectralCompareAudioProcessorEditor::refreshUIColors()
{
    const juce::Colour mainCol  = audioProcessor.getMainSpectrumColor();
    const juce::Colour sideCol  = audioProcessor.getSidechainSpectrumColor();
    const juce::Colour deltaCol = audioProcessor.getDeltaColor();
    const juce::Colour morphCol = audioProcessor.getMorphColor();

    // Helper: choose legible text colour for a given background
    auto textFor = [](juce::Colour bg) {
        return bg.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white;
    };

    // --- Main ---
    mainSmoothSlider.setColour(juce::Slider::thumbColourId,  mainCol);
    mainSmoothSlider.setColour(juce::Slider::trackColourId,  mainCol.withAlpha(0.45f));
    showMainButton.setColour(juce::TextButton::buttonOnColourId, mainCol);
    showMainButton.setColour(juce::TextButton::textColourOnId,   textFor(mainCol));
    freezeMainButton.setColour(juce::TextButton::buttonOnColourId, mainCol.darker(0.3f));
    freezeMainButton.setColour(juce::TextButton::textColourOnId,   textFor(mainCol.darker(0.3f)));

    // --- Sidechain ---
    sidechainSmoothSlider.setColour(juce::Slider::thumbColourId, sideCol);
    sidechainSmoothSlider.setColour(juce::Slider::trackColourId, sideCol.withAlpha(0.45f));
    showSidechainButton.setColour(juce::TextButton::buttonOnColourId, sideCol);
    showSidechainButton.setColour(juce::TextButton::textColourOnId,   textFor(sideCol));
    freezeSidechainButton.setColour(juce::TextButton::buttonOnColourId, sideCol.darker(0.3f));
    freezeSidechainButton.setColour(juce::TextButton::textColourOnId,   textFor(sideCol.darker(0.3f)));

    // --- Delta ---
    deltaSmoothSlider.setColour(juce::Slider::thumbColourId, deltaCol);
    deltaSmoothSlider.setColour(juce::Slider::trackColourId, deltaCol.withAlpha(0.45f));
    showDeltaButton.setColour(juce::TextButton::buttonOnColourId, deltaCol);
    showDeltaButton.setColour(juce::TextButton::textColourOnId,   textFor(deltaCol));
    freezeDeltaButton.setColour(juce::TextButton::buttonOnColourId, deltaCol.darker(0.3f));
    freezeDeltaButton.setColour(juce::TextButton::textColourOnId,   textFor(deltaCol.darker(0.3f)));

    // --- Morph ---
    morphSmoothSlider.setColour(juce::Slider::thumbColourId, morphCol);
    morphSmoothSlider.setColour(juce::Slider::trackColourId, morphCol.withAlpha(0.45f));
    morphSlider.setColour(juce::Slider::thumbColourId, morphCol);
    morphSlider.setColour(juce::Slider::trackColourId, morphCol.withAlpha(0.50f));
    claritySlider.setColour(juce::Slider::thumbColourId, morphCol.brighter(0.2f));
    claritySlider.setColour(juce::Slider::trackColourId, morphCol.withAlpha(0.35f));
    showMorphButton.setColour(juce::TextButton::buttonOnColourId, morphCol);
    showMorphButton.setColour(juce::TextButton::textColourOnId,   textFor(morphCol));
    freezeMorphButton.setColour(juce::TextButton::buttonOnColourId, morphCol.darker(0.3f));
    freezeMorphButton.setColour(juce::TextButton::textColourOnId,   textFor(morphCol.darker(0.3f)));

    repaint();
}

// ============================================================================
// Timer
// ============================================================================
void SpectralCompareAudioProcessorEditor::timerCallback()
{
    // Clamp freqToKnob range to actual nyquist on first valid tick
    const float nq = nyquist();
    if (nq > 1.0f && viewFreqMax > nq)
    {
        viewFreqMax = nq;
        freqToKnob.setRange(220.0, (double)nq, 1.0);
        freqToKnob.setValue(nq, juce::dontSendNotification);
    }

    // Pull live data (skipped when frozen)
    if (!mainIsFrozen)
        audioProcessor.getMainFFTData(mainDisplayData.data(), audioProcessor.numBins);

    if (!sidechainIsFrozen)
        audioProcessor.getSidechainFFTData(sidechainDisplayData.data(), audioProcessor.numBins);

    if (!morphIsFrozen)
        audioProcessor.getMorphedFFTData(morphDisplayData.data(), audioProcessor.numBins);

    // Update smoothed delta display (editor-side IIR)
    if (!deltaIsFrozen)
    {
        const float coeff = (float)deltaSmoothSlider.getValue();
        const int   nb    = audioProcessor.numBins;
        for (int i = 0; i < nb; ++i)
        {
            const float mMag = mainDisplayData[i];
            const float sMag = sidechainDisplayData[i];
            const float dDB  = (mMag > kSilenceFloor && sMag > kSilenceFloor)
                               ? 20.0f * std::log10(mMag) - 20.0f * std::log10(sMag)
                               : 0.0f;
            smoothedDeltaDisplay[i] = smoothedDeltaDisplay[i] * coeff + dDB * (1.0f - coeff);
        }
    }

    repaint(canvasArea());
}

// ============================================================================
// Layout constants
// ============================================================================
static constexpr int kPanelW  = 180;
static constexpr int kPadding = 8;

juce::Rectangle<int> SpectralCompareAudioProcessorEditor::canvasArea() const
{
    return getLocalBounds().withWidth(getWidth() - kPanelW);
}

// ============================================================================
// Coordinate helpers
// ============================================================================
float SpectralCompareAudioProcessorEditor::binToX(int bin) const
{
    auto ca = canvasArea();
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
    auto ca = canvasArea();
    const float logMin = std::log10(juce::jmax(viewFreqMin, 1.0f));
    const float logMax = std::log10(juce::jmax(viewFreqMax, viewFreqMin + 1.0f));

    float t   = (x - ca.getX()) / (float)ca.getWidth();
    float hz  = std::pow(10.0f, logMin + t * (logMax - logMin));
    int   bin = juce::roundToInt(hz * (audioProcessor.numBins - 1) / nyquist());
    return juce::jlimit(0, audioProcessor.numBins - 1, bin);
}

float SpectralCompareAudioProcessorEditor::magToY(float mag) const
{
    auto ca = canvasArea();
    const float minDB = -90.0f;
    const float maxDB =   0.0f;
    float dB = (mag > 0.0f) ? 20.0f * std::log10(mag) : minDB;
    dB = juce::jlimit(minDB, maxDB, dB);
    float t = (dB - maxDB) / (minDB - maxDB);
    return ca.getY() + t * ca.getHeight();
}

float SpectralCompareAudioProcessorEditor::deltaDBtoY(float dB) const
{
    auto ca = canvasArea();
    dB = juce::jlimit(-kDeltaRange, kDeltaRange, dB);
    float t = 0.5f - dB / (2.0f * kDeltaRange);
    return ca.getY() + t * ca.getHeight();
}

// ============================================================================
// Mouse
// ============================================================================
void SpectralCompareAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    hoverX = (float)e.x;
    repaint();
}

void SpectralCompareAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    hoverX = -1.0f;
    repaint();
}

// ============================================================================
// Paint
// ============================================================================
void SpectralCompareAudioProcessorEditor::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawGrid(g);

    if (showSidechain)
        drawSpectrum(g,
            sidechainIsFrozen ? frozenSidechainDisplay : sidechainDisplayData,
            audioProcessor.getSidechainSpectrumColor(), 0.7f);

    if (showMain)
        drawSpectrum(g,
            mainIsFrozen ? frozenMainDisplay : mainDisplayData,
            audioProcessor.getMainSpectrumColor(), 0.85f);

    if (showDelta)
        drawDelta(g);

    if (showMorph && audioProcessor.getMorphAmount() > 0.0f)
        drawMorphedSpectrum(g);

    drawLabels(g);
    drawHoverInfo(g);

    // Right panel background
    auto panel = getLocalBounds().removeFromRight(kPanelW);
    g.setColour(audioProcessor.getSidebarColor());
    g.fillRect(panel);
    g.setColour(audioProcessor.getSidebarColor().darker(0.3f));
    g.fillRect(panel.getX(), panel.getY(), 1, panel.getHeight());
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::drawBackground(juce::Graphics& g)
{
    g.fillAll(audioProcessor.getBackgroundColor());
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::drawGrid(juce::Graphics& g)
{
    auto ca = canvasArea();
    const juce::Colour gc = audioProcessor.getGridColor();

    static const float freqMarkers[] = {
        20, 30, 50, 100, 200, 300, 500,
        1000, 2000, 3000, 5000,
        10000, 20000
    };

    g.setFont(10.0f);
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
            juce::String label = (f >= 1000) ? juce::String(f / 1000, 0) + "k"
                                             : juce::String((int)f);
            g.setColour(gc.withAlpha(0.7f));
            g.drawText(label, (int)x + 2, ca.getBottom() - 16, 40, 14,
                juce::Justification::left, false);
        }
    }

    // dB grid lines (always visible)
    static const float dbMarkers[] = { -6, -12, -24, -48, -72 };
    for (float db : dbMarkers)
    {
        float mag = std::pow(10.0f, db / 20.0f);
        float y   = magToY(mag);
        if (y < ca.getY() || y > ca.getBottom()) continue;

        g.setColour(gc.withAlpha(0.25f));
        g.drawHorizontalLine((int)y, (float)ca.getX(), (float)ca.getRight());

        juce::String label = juce::String((int)db) + " dB";
        g.setColour(gc.withAlpha(0.6f));
        g.drawText(label, ca.getX() + 4, (int)y - 12, 50, 12,
            juce::Justification::left, false);
    }

    // Delta overlay grid — only when delta is visible
    if (showDelta)
    {
        const float midY = (float)ca.getY() + ca.getHeight() * 0.5f;
        g.setColour(gc.withAlpha(0.55f));
        g.drawHorizontalLine((int)midY, (float)ca.getX(), (float)ca.getRight());
        g.setColour(gc.withAlpha(0.6f));
        g.drawText("0 dB", ca.getX() + 4, (int)midY - 12, 50, 12,
            juce::Justification::left, false);

        static const float deltaMarkers[] = { 6.0f, 12.0f, 24.0f };
        for (float d : deltaMarkers)
        {
            for (int sign : { -1, 1 })
            {
                float y2 = deltaDBtoY((float)sign * d);
                if (y2 < ca.getY() || y2 > ca.getBottom()) continue;
                g.setColour(gc.withAlpha(0.18f));
                g.drawHorizontalLine((int)y2, (float)ca.getX(), (float)ca.getRight());
                juce::String lbl = (sign > 0 ? "+" : "-") + juce::String((int)d) + " dB";
                g.setColour(gc.withAlpha(0.45f));
                g.drawText(lbl, ca.getX() + 4, (int)y2 - 12, 55, 12,
                    juce::Justification::left, false);
            }
        }
    }
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::drawSpectrum(
    juce::Graphics& g,
    const std::array<float, maxDisplayBins>& data,
    juce::Colour colour,
    float alpha)
{
    auto ca = canvasArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    if (interpolate)
    {
        juce::Path path;
        bool started = false;
        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight()) continue;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));
            if (!started) { path.startNewSubPath(x, (float)ca.getBottom()); path.lineTo(x, y); started = true; }
            else           { path.lineTo(x, y); }
        }
        if (started) { path.lineTo((float)ca.getRight(), (float)ca.getBottom()); path.closeSubPath(); }
        g.setColour(colour.withAlpha(alpha * 0.35f));
        g.fillPath(path);
        g.setColour(colour.withAlpha(alpha));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
    else
    {
        g.setColour(colour.withAlpha(alpha * 0.85f));
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

// ============================================================================
void SpectralCompareAudioProcessorEditor::drawDelta(juce::Graphics& g)
{
    auto ca = canvasArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    const juce::Colour col  = audioProcessor.getDeltaColor();
    const float midY = (float)ca.getY() + ca.getHeight() * 0.5f;

    // Choose frozen or live delta data (already in dB)
    const auto& deltaData = deltaIsFrozen ? frozenDeltaDisplay : smoothedDeltaDisplay;

    if (!interpolate)
    {
        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight()) continue;

            // Skip bins where either source was silent when the smoothing ran
            const float mMag = mainIsFrozen ? frozenMainDisplay[bin] : mainDisplayData[bin];
            const float sMag = sidechainIsFrozen ? frozenSidechainDisplay[bin] : sidechainDisplayData[bin];
            if (mMag < kSilenceFloor || sMag < kSilenceFloor) continue;

            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(),
                                   deltaDBtoY(deltaData[bin]));
            float nextX = (bin + 1 < nb) ? binToX(bin + 1) : x + 1.0f;
            float w     = juce::jmax(1.0f, nextX - x - 1.0f);
            float top   = juce::jmin(y, midY);
            float h     = std::abs(midY - y);
            if (h < 1.0f) continue;

            g.setColour(col.withAlpha(0.75f));
            g.fillRect(x, top, w, h);
        }
        return;
    }

    // Interpolated stroke + fill band
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
        else             { stroke.lineTo(x, y); }

        if (!fillOpen)   { fill.startNewSubPath(x, midY); fill.lineTo(x, y); fillOpen = true; }
        else             { fill.lineTo(x, y); }
    }

    if (fillOpen) { fill.lineTo(binToX(nb - 1), midY); fill.closeSubPath(); }

    g.setColour(col.withAlpha(0.9f));
    g.strokePath(stroke, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.setColour(col.withAlpha(0.18f));
    g.fillPath(fill);
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::drawMorphedSpectrum(juce::Graphics& g)
{
    auto ca = canvasArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    const juce::Colour col = audioProcessor.getMorphColor();
    const auto& data = morphIsFrozen ? frozenMorphDisplay : morphDisplayData;

    if (!interpolate)
    {
        g.setColour(col.withAlpha(0.85f));
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

    juce::Path path;
    bool started = false;
    for (int bin = 0; bin < nb; ++bin)
    {
        float x = binToX(bin);
        if (x < ca.getX() || x > ca.getRight()) continue;
        float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));
        if (!started) { path.startNewSubPath(x, (float)ca.getBottom()); path.lineTo(x, y); started = true; }
        else           { path.lineTo(x, y); }
    }
    if (started) { path.lineTo((float)ca.getRight(), (float)ca.getBottom()); path.closeSubPath(); }

    g.setColour(col.withAlpha(0.12f));
    g.fillPath(path);
    g.setColour(col.withAlpha(0.95f));
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::drawLabels(juce::Graphics& g)
{
    auto ca = canvasArea();
    g.setFont(11.0f);
    float lx = (float)ca.getX() + 8.0f, ly = (float)ca.getY() + 8.0f;

    auto drawLegendEntry = [&](juce::Colour col, const juce::String& label)
    {
        g.setColour(col);
        g.fillRect(lx, ly + 2.0f, 12.0f, 8.0f);
        g.setColour(juce::Colours::lightgrey);
        g.drawText(label, (int)lx + 16, (int)ly, 160, 12, juce::Justification::left, false);
        ly += 16.0f;
    };

    if (showMain)
        drawLegendEntry(audioProcessor.getMainSpectrumColor(), "Main" + juce::String(mainIsFrozen ? " [frozen]" : ""));
    if (showSidechain)
        drawLegendEntry(audioProcessor.getSidechainSpectrumColor(), "Sidechain" + juce::String(sidechainIsFrozen ? " [frozen]" : ""));
    if (showDelta)
        drawLegendEntry(audioProcessor.getDeltaColor(), "Delta" + juce::String(deltaIsFrozen ? " [frozen]" : ""));
    if (showMorph && audioProcessor.getMorphAmount() > 0.0f)
    {
        const float pct = audioProcessor.getMorphAmount() * 100.0f;
        drawLegendEntry(audioProcessor.getMorphColor(),
            "Morph (" + juce::String((int)std::round(pct)) + "%)" + juce::String(morphIsFrozen ? " [frozen]" : ""));
    }
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::drawHoverInfo(juce::Graphics& g)
{
    auto ca = canvasArea();
    if (hoverX < ca.getX() || hoverX > ca.getRight()) return;

    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawVerticalLine((int)hoverX, (float)ca.getY(), (float)ca.getBottom());

    int bin = xToBin(hoverX);
    bin = juce::jlimit(0, audioProcessor.numBins - 1, bin);

    float hz = bin * nyquist() / (audioProcessor.numBins - 1);
    juce::String freqStr = (hz >= 1000.0f)
        ? (juce::String(hz / 1000.0f, 2) + " kHz")
        : (juce::String((int)hz) + " Hz");

    float mainMag = mainDisplayData[bin];
    float sideMag = sidechainDisplayData[bin];
    float mainDB  = (mainMag > 0) ? 20.0f * std::log10(mainMag) : -144.0f;
    float sideDB  = (sideMag > 0) ? 20.0f * std::log10(sideMag) : -144.0f;

    juce::String info = freqStr
        + "  Main: " + juce::String(mainDB, 1) + " dB"
        + "  Side: " + juce::String(sideDB, 1) + " dB";

    if (showDelta && mainMag > kSilenceFloor && sideMag > kSilenceFloor)
    {
        float deltaDB = mainDB - sideDB;
        info += "  Δ: " + (deltaDB >= 0 ? juce::String("+") : juce::String(""))
              + juce::String(deltaDB, 1) + " dB";
    }

    const float morph = audioProcessor.getMorphAmount();
    if (showMorph && morph > 0.0f)
    {
        float morphMag = morphDisplayData[bin];
        float morphDB  = (morphMag > 0.0f) ? 20.0f * std::log10(morphMag) : -144.0f;
        info += "  Morph: " + juce::String(morphDB, 1) + " dB";
    }

    const int tw = 520, th = 18;
    int tx = juce::jlimit(ca.getX(), ca.getRight() - tw, (int)hoverX + 8);
    int ty = ca.getBottom() - th - 4;

    g.setColour(juce::Colour(0xcc000000));
    g.fillRoundedRectangle((float)tx, (float)ty, (float)tw, (float)th, 3.0f);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(11.0f);
    g.drawText(info, tx + 4, ty, tw - 8, th, juce::Justification::centredLeft, false);
}

// ============================================================================
// Resized
// ============================================================================
void SpectralCompareAudioProcessorEditor::resized()
{
    auto panel = getLocalBounds().removeFromRight(kPanelW);
    panel.reduce(kPadding, kPadding);

    int y          = panel.getY();
    const int pw   = panel.getWidth();
    const int px   = panel.getX();
    const int rowH = 18;
    const int slH  = 14;
    const int gap  = 3;
    const int halfW = (pw - gap) / 2;

    // --- FFT Size ---
    fftSizeLabel.setBounds(px, y, 56, rowH);
    fftSizeCombo.setBounds(px + 58, y, pw - 58, rowH);
    y += rowH + gap;

    // --- Morph slider ---
    morphLabel.setBounds(px, y, pw, rowH);
    y += rowH;
    morphSlider.setBounds(px, y, pw, slH);
    y += slH + gap;

    // --- Clarity (envelope width) slider ---
    clarityLabel.setBounds(px, y, pw, rowH);
    y += rowH;
    claritySlider.setBounds(px, y, pw, slH);
    y += slH + gap + 2;

    // --- Show/Freeze pairs ---
    auto twoBtn = [&](juce::TextButton& a, juce::TextButton& b)
    {
        a.setBounds(px,            y, halfW, rowH);
        b.setBounds(px + halfW + gap, y, halfW, rowH);
        y += rowH + gap;
    };

    twoBtn(showMainButton,      freezeMainButton);
    twoBtn(showSidechainButton, freezeSidechainButton);
    twoBtn(showDeltaButton,     freezeDeltaButton);
    twoBtn(showMorphButton,     freezeMorphButton);

    y += gap;

    // --- Interpolate ---
    interpolateButton.setBounds(px, y, pw, rowH);
    y += rowH + gap + 2;

    // --- Smooth sliders ---
    auto smoothRow = [&](juce::Label& lbl, juce::Slider& sl)
    {
        lbl.setBounds(px, y, pw, rowH);
        y += rowH;
        sl.setBounds(px, y, pw, slH);
        y += slH + gap;
    };

    smoothRow(mainSmoothLabel,      mainSmoothSlider);
    smoothRow(sidechainSmoothLabel, sidechainSmoothSlider);
    smoothRow(deltaSmoothLabel,     deltaSmoothSlider);
    smoothRow(morphSmoothLabel,     morphSmoothSlider);

    y += gap;

    // --- Color pickers ---
    auto colorRow = [&](juce::Label& lbl, juce::TextEditor& inp)
    {
        lbl.setBounds(px, y, 62, rowH);
        inp.setBounds(px + 64, y, pw - 64, rowH);
        y += rowH + gap;
    };

    colorRow(bgColorLabel,        bgColorInput);
    colorRow(gridColorLabel,      gridColorInput);
    colorRow(mainColorLabel,      mainColorInput);
    colorRow(sidechainColorLabel, sidechainColorInput);
    colorRow(deltaColorLabel,     deltaColorInput);
    colorRow(morphColorLabel,     morphColorInput);
    colorRow(sidebarColorLabel,   sidebarColorInput);

    y += gap * 2;
    resetColorsButton.setBounds(px, y, pw, rowH);
    y += rowH + gap * 3;

    // --- Freq knobs (two side-by-side) ---
    const int knobAreaW = pw / 2 - 2;
    const int knobSize  = juce::jmin(knobAreaW, 48);
    const int lblH      = 14;
    const int tbH       = 16;

    int kx1 = px + (knobAreaW - knobSize) / 2;
    int kx2 = px + knobAreaW + 4 + (knobAreaW - knobSize) / 2;

    freqFromLabel.setBounds(px,             y, knobAreaW, lblH);
    freqToLabel.setBounds  (px + knobAreaW + 4, y, knobAreaW, lblH);
    y += lblH;

    freqFromKnob.setBounds(kx1, y, knobSize, knobSize + tbH);
    freqToKnob.setBounds  (kx2, y, knobSize, knobSize + tbH);
}

// ============================================================================
// Color appliers
// ============================================================================
void SpectralCompareAudioProcessorEditor::applyBgColor()
{
    juce::Colour c;
    if (parseHexColour(bgColorInput.getText(), c))
    {
        audioProcessor.setBackgroundColor(c);
        bgColorInput.setText(colourToHex(c), juce::dontSendNotification);
        refreshUIColors();
    }
}

void SpectralCompareAudioProcessorEditor::applyGridColor()
{
    juce::Colour c;
    if (parseHexColour(gridColorInput.getText(), c))
    {
        audioProcessor.setGridColor(c);
        gridColorInput.setText(colourToHex(c), juce::dontSendNotification);
        refreshUIColors();
    }
}

void SpectralCompareAudioProcessorEditor::applyMainColor()
{
    juce::Colour c;
    if (parseHexColour(mainColorInput.getText(), c))
    {
        audioProcessor.setMainSpectrumColor(c);
        mainColorInput.setText(colourToHex(c), juce::dontSendNotification);
        refreshUIColors();
    }
}

void SpectralCompareAudioProcessorEditor::applySidechainColor()
{
    juce::Colour c;
    if (parseHexColour(sidechainColorInput.getText(), c))
    {
        audioProcessor.setSidechainSpectrumColor(c);
        sidechainColorInput.setText(colourToHex(c), juce::dontSendNotification);
        refreshUIColors();
    }
}

void SpectralCompareAudioProcessorEditor::applyDeltaColor()
{
    juce::Colour c;
    if (parseHexColour(deltaColorInput.getText(), c))
    {
        audioProcessor.setDeltaColor(c);
        deltaColorInput.setText(colourToHex(c), juce::dontSendNotification);
        refreshUIColors();
    }
}

void SpectralCompareAudioProcessorEditor::applyMorphColor()
{
    juce::Colour c;
    if (parseHexColour(morphColorInput.getText(), c))
    {
        audioProcessor.setMorphColor(c);
        morphColorInput.setText(colourToHex(c), juce::dontSendNotification);
        refreshUIColors();
    }
}

void SpectralCompareAudioProcessorEditor::applySidebarColor()
{
    juce::Colour c;
    if (parseHexColour(sidebarColorInput.getText(), c))
    {
        audioProcessor.setSidebarColor(c);
        sidebarColorInput.setText(colourToHex(c), juce::dontSendNotification);
        refreshUIColors();
    }
}
