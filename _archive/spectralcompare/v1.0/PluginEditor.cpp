#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
// Helpers
// ============================================================================
static void styleLabel(juce::Label& l)
{
    l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    l.setJustificationType(juce::Justification::centredRight);
}

static void styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff55eedd));
    b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
}

static void styleTextEditor(juce::TextEditor& e)
{
    e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    e.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    e.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff444444));
    e.setFont(11.0f);
}

static void styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    c.setColour(juce::ComboBox::textColourId, juce::Colours::lightgrey);
    c.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff444444));
    c.setColour(juce::ComboBox::arrowColourId, juce::Colours::lightgrey);
}

// Parse a 6- or 8-digit hex colour string (optionally prefixed with #)
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
    setSize(1000, 530);

    mainDisplayData.fill(0.0f);
    sidechainDisplayData.fill(0.0f);
    frozenMainDisplay.fill(0.0f);
    frozenSidechainDisplay.fill(0.0f);

    // ---- FFT Size ----
    styleLabel(fftSizeLabel);
    fftSizeLabel.setText("FFT Size:", juce::dontSendNotification);
    addAndMakeVisible(fftSizeLabel);

    fftSizeCombo.addItem("1024", 1);
    fftSizeCombo.addItem("2048", 2);
    fftSizeCombo.addItem("4096", 3);
    fftSizeCombo.addItem("8192", 4);
    styleCombo(fftSizeCombo);
    {
        int sel = (audioProcessor.fftSize == 1024) ? 1
            : (audioProcessor.fftSize == 2048) ? 2
            : (audioProcessor.fftSize == 4096) ? 3 : 4;
        fftSizeCombo.setSelectedId(sel, juce::dontSendNotification);
    }
    fftSizeCombo.onChange = [this]()
        {
            int id = fftSizeCombo.getSelectedId();
            int newSize = (id == 1) ? 1024 : (id == 2) ? 2048 : (id == 3) ? 4096 : 8192;
            audioProcessor.setFFTSize(newSize);
        };
    addAndMakeVisible(fftSizeCombo);

    // ---- Freeze Main button ----
    styleButton(freezeMainButton);
    freezeMainButton.setButtonText("Freeze Main");
    freezeMainButton.setClickingTogglesState(true);
    freezeMainButton.onClick = [this]()
        {
            mainIsFrozen = freezeMainButton.getToggleState();
            if (mainIsFrozen)
                frozenMainDisplay = mainDisplayData;
            freezeMainButton.setButtonText(mainIsFrozen ? "Main: Frozen" : "Freeze Main");
        };
    addAndMakeVisible(freezeMainButton);

    // ---- Freeze Sidechain button ----
    styleButton(freezeSidechainButton);
    freezeSidechainButton.setButtonText("Freeze Sidechain");
    freezeSidechainButton.setClickingTogglesState(true);
    freezeSidechainButton.onClick = [this]()
        {
            sidechainIsFrozen = freezeSidechainButton.getToggleState();
            if (sidechainIsFrozen)
                frozenSidechainDisplay = sidechainDisplayData;
            freezeSidechainButton.setButtonText(sidechainIsFrozen ? "Side: Frozen" : "Freeze Sidechain");
        };
    addAndMakeVisible(freezeSidechainButton);

    // ---- Delta Mode button ----
    styleButton(deltaButton);
    deltaButton.setButtonText("Delta Mode");
    deltaButton.setClickingTogglesState(true);
    deltaButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffff00));
    deltaButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    deltaButton.onClick = [this]()
        {
            deltaMode = deltaButton.getToggleState();
            deltaButton.setButtonText(deltaMode ? "Delta: ON" : "Delta Mode");
            // Modes are mutually exclusive
            if (deltaMode && diffMode)
            {
                diffMode = false;
                diffButton.setToggleState(false, juce::dontSendNotification);
                diffButton.setButtonText("Diff Mode");
            }
        };
    addAndMakeVisible(deltaButton);

    // ---- Difference Mode button ----
    styleButton(diffButton);
    diffButton.setButtonText("Diff Mode");
    diffButton.setClickingTogglesState(true);
    diffButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00ccff));
    diffButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    diffButton.onClick = [this]()
        {
            diffMode = diffButton.getToggleState();
            diffButton.setButtonText(diffMode ? "Diff: ON" : "Diff Mode");
            // Modes are mutually exclusive
            if (diffMode && deltaMode)
            {
                deltaMode = false;
                deltaButton.setToggleState(false, juce::dontSendNotification);
                deltaButton.setButtonText("Delta Mode");
            }
        };
    addAndMakeVisible(diffButton);

    // ---- Interpolate toggle ----
    styleButton(interpolateButton);
    interpolateButton.setButtonText("Interpolate: ON");
    interpolateButton.setClickingTogglesState(true);
    interpolateButton.setToggleState(true, juce::dontSendNotification);
    interpolateButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff888888));
    interpolateButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    interpolateButton.onClick = [this]()
        {
            interpolate = interpolateButton.getToggleState();
            interpolateButton.setButtonText(interpolate ? "Interpolate: ON" : "Interpolate: OFF");
        };
    addAndMakeVisible(interpolateButton);
    styleLabel(mainSmoothLabel);
    mainSmoothLabel.setText("Smooth Main:", juce::dontSendNotification);
    addAndMakeVisible(mainSmoothLabel);

    mainSmoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    mainSmoothSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mainSmoothSlider.setRange(0.0, 0.99, 0.01);
    mainSmoothSlider.setValue(0.7, juce::dontSendNotification);
    mainSmoothSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff00ff00));
    mainSmoothSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00ff00).withAlpha(0.4f));
    mainSmoothSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a2a));
    mainSmoothSlider.onValueChange = [this]()
        {
            audioProcessor.smoothMain.store((float)mainSmoothSlider.getValue(),
                std::memory_order_relaxed);
        };
    addAndMakeVisible(mainSmoothSlider);

    // ---- Smooth Sidechain slider ----
    styleLabel(sidechainSmoothLabel);
    sidechainSmoothLabel.setText("Smooth Side:", juce::dontSendNotification);
    addAndMakeVisible(sidechainSmoothLabel);

    sidechainSmoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sidechainSmoothSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sidechainSmoothSlider.setRange(0.0, 0.99, 0.01);
    sidechainSmoothSlider.setValue(0.7, juce::dontSendNotification);
    sidechainSmoothSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffff00ff));
    sidechainSmoothSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xffff00ff).withAlpha(0.4f));
    sidechainSmoothSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a2a));
    sidechainSmoothSlider.onValueChange = [this]()
        {
            audioProcessor.smoothSidechain.store((float)sidechainSmoothSlider.getValue(),
                std::memory_order_relaxed);
        };
    addAndMakeVisible(sidechainSmoothSlider);

    // ---- Color controls ----
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

    setupColorRow(bgColorLabel, "BG:", bgColorInput,
        audioProcessor.getBackgroundColor(),
        [this] { applyBgColor(); });
    setupColorRow(gridColorLabel, "Grid:", gridColorInput,
        audioProcessor.getGridColor(),
        [this] { applyGridColor(); });
    setupColorRow(mainColorLabel, "Main:", mainColorInput,
        audioProcessor.getMainSpectrumColor(),
        [this] { applyMainColor(); });
    setupColorRow(sidechainColorLabel, "Sidechain:", sidechainColorInput,
        audioProcessor.getSidechainSpectrumColor(),
        [this] { applySidechainColor(); });
    setupColorRow(deltaColorLabel, "Delta:", deltaColorInput,
        audioProcessor.getDeltaColor(),
        [this] { applyDeltaColor(); });
    setupColorRow(diffColorLabel, "Diff:", diffColorInput,
        audioProcessor.getDiffColor(),
        [this] { applyDiffColor(); });
    setupColorRow(sidebarColorLabel, "Sidebar:", sidebarColorInput,
        audioProcessor.getSidebarColor(),
        [this] { applySidebarColor(); });

    styleButton(resetColorsButton);
    resetColorsButton.setButtonText("Reset Colors");
    resetColorsButton.onClick = [this]()
        {
            audioProcessor.resetColors();
            bgColorInput.setText(colourToHex(audioProcessor.getBackgroundColor()), juce::dontSendNotification);
            gridColorInput.setText(colourToHex(audioProcessor.getGridColor()), juce::dontSendNotification);
            mainColorInput.setText(colourToHex(audioProcessor.getMainSpectrumColor()), juce::dontSendNotification);
            sidechainColorInput.setText(colourToHex(audioProcessor.getSidechainSpectrumColor()), juce::dontSendNotification);
            deltaColorInput.setText(colourToHex(audioProcessor.getDeltaColor()), juce::dontSendNotification);
            diffColorInput.setText(colourToHex(audioProcessor.getDiffColor()), juce::dontSendNotification);
            sidebarColorInput.setText(colourToHex(audioProcessor.getSidebarColor()), juce::dontSendNotification);
            repaint();
        };
    addAndMakeVisible(resetColorsButton);

    // ---- Freq range knobs ----
    // Shared LookAndFeel: grey accent, same rotary style as strength knobs
    freqKnobLF = std::make_unique<juce::LookAndFeel_V4>();
    freqKnobLF->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff888888));
    freqKnobLF->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
    freqKnobLF->setColour(juce::Slider::thumbColourId, juce::Colour(0xffaaaaaa));
    freqKnobLF->setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
    freqKnobLF->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1a1a1a));
    freqKnobLF->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff333333));
    freqKnobLF->setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    auto setupFreqKnob = [&](juce::Slider& knob, juce::Label& lbl,
        const juce::String& labelText,
        double rangeMin, double rangeMax, double initVal)
        {
            styleLabel(lbl);
            lbl.setText(labelText, juce::dontSendNotification);
            lbl.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(lbl);

            knob.setLookAndFeel(freqKnobLF.get());
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
            knob.setRange(rangeMin, rangeMax, 1.0);
            knob.setSkewFactorFromMidPoint(1000.0);   // log feel — low freqs get more travel
            knob.setValue(initVal, juce::dontSendNotification);
            knob.setTextValueSuffix(" Hz");
            addAndMakeVisible(knob);
        };

    setupFreqKnob(freqFromKnob, freqFromLabel, "From", 20.0, 19800.0, 20.0);
    setupFreqKnob(freqToKnob, freqToLabel, "To", 220.0, 22000.0, 20000.0);

    // Independent — no coupling; if window < 200 Hz the display just shows low resolution
    freqFromKnob.onValueChange = [this]()
        {
            viewFreqMin = (float)freqFromKnob.getValue();
            repaint(canvasArea());
        };
    freqToKnob.onValueChange = [this]()
        {
            viewFreqMax = (float)freqToKnob.getValue();
            repaint(canvasArea());
        };

    startTimerHz(60);
}

SpectralCompareAudioProcessorEditor::~SpectralCompareAudioProcessorEditor()
{
    stopTimer();
    freqFromKnob.setLookAndFeel(nullptr);
    freqToKnob.setLookAndFeel(nullptr);
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::timerCallback()
{
    // On first valid tick, clamp freqToKnob range/value to actual nyquist
    const float nq = nyquist();
    if (nq > 1.0f && viewFreqMax > nq)
    {
        viewFreqMax = nq;
        freqToKnob.setRange(220.0, (double)nq, 1.0);
        freqToKnob.setValue(nq, juce::dontSendNotification);
    }

    if (!mainIsFrozen)
        audioProcessor.getMainFFTData(mainDisplayData.data(), audioProcessor.numBins);

    if (!sidechainIsFrozen)
        audioProcessor.getSidechainFFTData(sidechainDisplayData.data(), audioProcessor.numBins);

    repaint(canvasArea());
}

// ============================================================================
// Layout constants
// ============================================================================
static constexpr int kPanelW = 180;  // right panel width
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

    float t = (x - ca.getX()) / (float)ca.getWidth();
    float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
    int   bin = juce::roundToInt(hz * (audioProcessor.numBins - 1) / nyquist());
    return juce::jlimit(0, audioProcessor.numBins - 1, bin);
}

// Map raw FFT magnitude to a Y pixel (log dB scale)
// We map dB range [minDB, maxDB] to [bottom, top] of canvas
float SpectralCompareAudioProcessorEditor::magToY(float mag) const
{
    auto ca = canvasArea();
    // Avoid log(0)
    const float minDB = -90.0f;
    const float maxDB = 0.0f;  // reference = 0 dBFS
    float dB = (mag > 0.0f) ? 20.0f * std::log10(mag) : minDB;
    dB = juce::jlimit(minDB, maxDB, dB);
    float t = (dB - maxDB) / (minDB - maxDB);  // 0 at top, 1 at bottom
    return ca.getY() + t * ca.getHeight();
}

// Map a delta-dB value to Y: 0 dB → vertical centre; +kDeltaRange → top; -kDeltaRange → bottom
float SpectralCompareAudioProcessorEditor::deltaDBtoY(float dB) const
{
    auto ca = canvasArea();
    dB = juce::jlimit(-kDeltaRange, kDeltaRange, dB);
    // t=0 at centre (+kDeltaRange above → top), t=1 at bottom (-kDeltaRange → bottom)
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

    if (deltaMode)
    {
        drawDelta(g);
    }
    else if (diffMode)
    {
        drawDifference(g);
    }
    else
    {
        drawSpectrum(g, sidechainDisplayData, audioProcessor.getSidechainSpectrumColor(), 0.7f);
        drawSpectrum(g, mainDisplayData, audioProcessor.getMainSpectrumColor(), 0.85f);
    }

    drawLabels(g);
    drawHoverInfo(g);

    // Right panel background
    auto panel = getLocalBounds().removeFromRight(kPanelW);
    g.setColour(audioProcessor.getSidebarColor());
    g.fillRect(panel);
    g.setColour(audioProcessor.getSidebarColor().darker(0.3f));
    g.fillRect(panel.getX(), panel.getY(), 1, panel.getHeight());
}

void SpectralCompareAudioProcessorEditor::drawBackground(juce::Graphics& g)
{
    g.fillAll(audioProcessor.getBackgroundColor());
}

void SpectralCompareAudioProcessorEditor::drawGrid(juce::Graphics& g)
{
    auto ca = canvasArea();
    juce::Colour gc = audioProcessor.getGridColor();

    // Frequency grid lines (fixed decade/sub-decade markers)
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

        bool isMajor = (f == 100 || f == 1000 || f == 10000 || f == 20);
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

    // dB grid lines
    static const float dbMarkers[] = { -6, -12, -24, -48, -72 };
    for (float db : dbMarkers)
    {
        float mag = std::pow(10.0f, db / 20.0f);
        float y = magToY(mag);
        if (y < ca.getY() || y > ca.getBottom()) continue;

        g.setColour(gc.withAlpha(0.25f));
        g.drawHorizontalLine((int)y, (float)ca.getX(), (float)ca.getRight());

        juce::String label = juce::String((int)db) + " dB";
        g.setColour(gc.withAlpha(0.6f));
        g.drawText(label, ca.getX() + 4, (int)y - 12, 50, 12,
            juce::Justification::left, false);
    }

    // Delta-mode overlay: centred zero line + symmetric ±dB markers
    if (deltaMode)
    {
        const float midY = (float)ca.getY() + ca.getHeight() * 0.5f;

        // Zero line (brighter)
        g.setColour(gc.withAlpha(0.7f));
        g.drawHorizontalLine((int)midY, (float)ca.getX(), (float)ca.getRight());
        g.drawText("0 dB", ca.getX() + 4, (int)midY - 12, 50, 12,
            juce::Justification::left, false);

        // ±6, ±12, ±24 dB delta markers
        static const float deltaMarkers[] = { 6.0f, 12.0f, 24.0f };
        for (float d : deltaMarkers)
        {
            for (int sign : { -1, 1 })
            {
                float y2 = deltaDBtoY((float)sign * d);
                if (y2 < ca.getY() || y2 > ca.getBottom()) continue;
                g.setColour(gc.withAlpha(0.2f));
                g.drawHorizontalLine((int)y2, (float)ca.getX(), (float)ca.getRight());
                juce::String lbl = (sign > 0 ? "+" : "-") + juce::String((int)d) + " dB";
                g.setColour(gc.withAlpha(0.5f));
                g.drawText(lbl, ca.getX() + 4, (int)y2 - 12, 55, 12,
                    juce::Justification::left, false);
            }
        }
    }
}

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
        // Smooth filled curve
        juce::Path path;
        bool started = false;

        for (int bin = 0; bin < nb; ++bin)
        {
            float x = binToX(bin);
            if (x < ca.getX() || x > ca.getRight()) continue;
            float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), magToY(data[bin]));
            if (!started)
            {
                path.startNewSubPath(x, (float)ca.getBottom());
                path.lineTo(x, y);
                started = true;
            }
            else
            {
                path.lineTo(x, y);
            }
        }
        if (started)
        {
            path.lineTo((float)ca.getRight(), (float)ca.getBottom());
            path.closeSubPath();
        }
        g.setColour(colour.withAlpha(alpha * 0.35f));
        g.fillPath(path);
        g.setColour(colour.withAlpha(alpha));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
    else
    {
        // Discrete vertical bars: one rectangle per bin, 1px gap between
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

void SpectralCompareAudioProcessorEditor::drawDelta(juce::Graphics& g)
{
    auto ca = canvasArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    juce::Colour col = audioProcessor.getDeltaColor();

    // We draw a polyline (not a filled path) because the delta is signed —
    // it floats above and below the centre line.  Segments where either
    // signal is silent are left as gaps so "no signal = nothing shown".

    juce::Path path;
    bool pathOpen = false;

    for (int bin = 0; bin < nb; ++bin)
    {
        float mainMag = mainDisplayData[bin];
        float sideMag = sidechainDisplayData[bin];

        // If either signal is below the silence floor → gap in the curve
        if (mainMag < kSilenceFloor || sideMag < kSilenceFloor)
        {
            pathOpen = false;
            continue;
        }

        float mainDB = 20.0f * std::log10(mainMag);
        float sideDB = 20.0f * std::log10(sideMag);
        float deltaDB = mainDB - sideDB;  // positive = main louder, negative = sidechain louder

        float x = binToX(bin);
        if (x < ca.getX() || x > ca.getRight())
        {
            pathOpen = false;
            continue;
        }

        float y = deltaDBtoY(deltaDB);
        y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), y);

        if (!pathOpen)
        {
            path.startNewSubPath(x, y);
            pathOpen = true;
        }
        else
        {
            path.lineTo(x, y);
        }
    }

    // Draw stroke only (no fill — the curve floats above/below centre)
    g.setColour(col.withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    // Also draw a thin filled band between the curve and the zero-line for readability
    // Re-iterate to build the filled shape in two halves (above/below centre separately)
    const float midY = (float)ca.getY() + ca.getHeight() * 0.5f;

    juce::Path fillPath;
    pathOpen = false;

    for (int bin = 0; bin < nb; ++bin)
    {
        float mainMag = mainDisplayData[bin];
        float sideMag = sidechainDisplayData[bin];

        if (mainMag < kSilenceFloor || sideMag < kSilenceFloor)
        {
            if (pathOpen)
            {
                // Close current segment back to the zero line
                float prevX = binToX(bin - 1);
                fillPath.lineTo(prevX, midY);
                fillPath.closeSubPath();
                pathOpen = false;
            }
            continue;
        }

        float mainDB = 20.0f * std::log10(mainMag);
        float sideDB = 20.0f * std::log10(sideMag);
        float deltaDB = mainDB - sideDB;

        float x = binToX(bin);
        if (x < ca.getX() || x > ca.getRight())
        {
            if (pathOpen) { fillPath.lineTo(x, midY); fillPath.closeSubPath(); pathOpen = false; }
            continue;
        }

        float y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), deltaDBtoY(deltaDB));

        if (!pathOpen)
        {
            fillPath.startNewSubPath(x, midY);
            fillPath.lineTo(x, y);
            pathOpen = true;
        }
        else
        {
            fillPath.lineTo(x, y);
        }
    }

    if (pathOpen)
    {
        float lastX = binToX(nb - 1);
        fillPath.lineTo(lastX, midY);
        fillPath.closeSubPath();
    }

    g.setColour(col.withAlpha(0.18f));
    g.fillPath(fillPath);
}

// ----------------------------------------------------------------------------
// Difference mode: naive per-bin magnitude subtraction  max(0, main - sidechain)
// Displayed on the normal absolute dB scale (same as the regular spectra).
// Bins where either signal is silent show nothing.
// ----------------------------------------------------------------------------
void SpectralCompareAudioProcessorEditor::drawDifference(juce::Graphics& g)
{
    auto ca = canvasArea();
    const int nb = audioProcessor.numBins;
    if (nb < 2) return;

    juce::Colour col = audioProcessor.getDiffColor();

    juce::Path path;
    bool started = false;

    for (int bin = 0; bin < nb; ++bin)
    {
        float mainMag = mainDisplayData[bin];
        float sideMag = sidechainDisplayData[bin];

        // Both must be above silence floor for the result to be meaningful
        if (mainMag < kSilenceFloor || sideMag < kSilenceFloor)
        {
            started = false;
            continue;
        }

        // Naive subtraction in the magnitude domain; clamp to zero (no negative energy)
        float diffMag = mainMag - sideMag;
        if (diffMag <= 0.0f)
        {
            started = false;
            continue;
        }

        float x = binToX(bin);
        if (x < ca.getX() || x > ca.getRight())
        {
            started = false;
            continue;
        }

        float y = magToY(diffMag);
        y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), y);

        if (!started)
        {
            path.startNewSubPath(x, (float)ca.getBottom());
            path.lineTo(x, y);
            started = true;
        }
        else
        {
            path.lineTo(x, y);
        }
    }

    if (started)
    {
        path.lineTo((float)ca.getRight(), (float)ca.getBottom());
        path.closeSubPath();
    }

    // Same filled-path style as normal spectra
    g.setColour(col.withAlpha(0.35f));
    g.fillPath(path);
    g.setColour(col.withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

void SpectralCompareAudioProcessorEditor::drawLabels(juce::Graphics& g)
{
    auto ca = canvasArea();

    // Legend in top-left corner
    g.setFont(11.0f);
    float lx = (float)ca.getX() + 8.0f, ly = (float)ca.getY() + 8.0f;

    if (deltaMode)
    {
        // Delta mode: single legend entry
        g.setColour(audioProcessor.getDeltaColor());
        g.fillRect(lx, ly + 2.0f, 12.0f, 8.0f);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Delta (Main - Side)", (int)lx + 16, (int)ly, 130, 12,
            juce::Justification::left, false);
    }
    else if (diffMode)
    {
        // Difference mode: single legend entry
        g.setColour(audioProcessor.getDiffColor());
        g.fillRect(lx, ly + 2.0f, 12.0f, 8.0f);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Diff (Main \xe2\x88\x92 Side mag)", (int)lx + 16, (int)ly, 140, 12,
            juce::Justification::left, false);
    }
    else
    {
        // Main
        g.setColour(audioProcessor.getMainSpectrumColor());
        g.fillRect(lx, ly + 2.0f, 12.0f, 8.0f);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Main", (int)lx + 16, (int)ly, 60, 12, juce::Justification::left, false);

        // Sidechain
        ly += 18.0f;
        g.setColour(audioProcessor.getSidechainSpectrumColor());
        g.fillRect(lx, ly + 2.0f, 12.0f, 8.0f);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Sidechain", (int)lx + 16, (int)ly, 70, 12, juce::Justification::left, false);
    }

    // Freeze indicators (always visible)
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    int indicatorX = ca.getRight() - 4;
    int indicatorY = ca.getY() + 6;
    if (mainIsFrozen)
    {
        juce::String tag = "[MAIN FROZEN]";
        int tw = 90;
        g.setColour(juce::Colour(0xffff5500).withAlpha(0.9f));
        g.drawText(tag, indicatorX - tw, indicatorY, tw, 14, juce::Justification::right, false);
        indicatorY += 16;
    }
    if (sidechainIsFrozen)
    {
        juce::String tag = "[SIDE FROZEN]";
        int tw = 90;
        g.setColour(juce::Colour(0xffaa00ff).withAlpha(0.9f));
        g.drawText(tag, indicatorX - tw, indicatorY, tw, 14, juce::Justification::right, false);
    }
}

void SpectralCompareAudioProcessorEditor::drawHoverInfo(juce::Graphics& g)
{
    auto ca = canvasArea();
    if (hoverX < ca.getX() || hoverX > ca.getRight()) return;

    // Vertical crosshair
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
    float mainDB = (mainMag > 0) ? 20.0f * std::log10(mainMag) : -144.0f;
    float sideDB = (sideMag > 0) ? 20.0f * std::log10(sideMag) : -144.0f;

    juce::String info;
    if (deltaMode)
    {
        bool mainSilent = mainMag < kSilenceFloor;
        bool sideSilent = sideMag < kSilenceFloor;
        if (mainSilent || sideSilent)
        {
            info = freqStr + "  Delta: — (signal absent)";
        }
        else
        {
            float deltaDB = mainDB - sideDB;
            juce::String sign = (deltaDB >= 0) ? "+" : "";
            info = freqStr + "  Delta: " + sign + juce::String(deltaDB, 1) + " dB"
                + "  (Main " + juce::String(mainDB, 1) + " / Side " + juce::String(sideDB, 1) + " dB)";
        }
    }
    else if (diffMode)
    {
        bool mainSilent = mainMag < kSilenceFloor;
        bool sideSilent = sideMag < kSilenceFloor;
        if (mainSilent || sideSilent)
        {
            info = freqStr + "  Diff: — (signal absent)";
        }
        else
        {
            float diffMag = mainMag - sideMag;
            if (diffMag <= 0.0f)
            {
                info = freqStr + "  Diff: — (sidechain >= main)";
            }
            else
            {
                float diffDB = 20.0f * std::log10(diffMag);
                info = freqStr + "  Diff: " + juce::String(diffDB, 1) + " dB"
                    + "  (Main " + juce::String(mainDB, 1) + " / Side " + juce::String(sideDB, 1) + " dB)";
            }
        }
    }
    else
    {
        info = freqStr
            + "  Main: " + juce::String(mainDB, 1) + " dB"
            + "  Side: " + juce::String(sideDB, 1) + " dB";
    }

    // Tooltip box
    const int tw = 380, th = 18;
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
    // ---- Right panel ----
    auto panel = getLocalBounds().removeFromRight(kPanelW);
    panel.reduce(kPadding, kPadding);

    int y = panel.getY();
    const int rowH = 20, gap = 4;

    fftSizeLabel.setBounds(panel.getX(), y, 58, rowH);
    fftSizeCombo.setBounds(panel.getX() + 60, y, panel.getWidth() - 60, rowH);
    y += rowH + gap;

    freezeMainButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH + gap;
    freezeSidechainButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH + gap;
    deltaButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH + gap;
    diffButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH + gap;
    interpolateButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH + gap + 2;

    const int sliderH = 16;
    mainSmoothLabel.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH;
    mainSmoothSlider.setBounds(panel.getX(), y, panel.getWidth(), sliderH);
    y += sliderH + gap;

    sidechainSmoothLabel.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH;
    sidechainSmoothSlider.setBounds(panel.getX(), y, panel.getWidth(), sliderH);
    y += sliderH + gap + 2;

    auto colorRow = [&](juce::Label& lbl, juce::TextEditor& inp)
        {
            lbl.setBounds(panel.getX(), y, 62, rowH);
            inp.setBounds(panel.getX() + 64, y, panel.getWidth() - 64, rowH);
            y += rowH + gap;
        };

    colorRow(bgColorLabel, bgColorInput);
    colorRow(gridColorLabel, gridColorInput);
    colorRow(mainColorLabel, mainColorInput);
    colorRow(sidechainColorLabel, sidechainColorInput);
    colorRow(deltaColorLabel, deltaColorInput);
    colorRow(diffColorLabel, diffColorInput);
    colorRow(sidebarColorLabel, sidebarColorInput);

    y += gap * 2;
    resetColorsButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH + gap * 2;

    // ---- Freq knobs: two round knobs side by side ----
    // Each knob gets half the panel width; label above, textbox below
    const int knobAreaW = panel.getWidth() / 2 - 2;
    const int knobSize = juce::jmin(knobAreaW, 52);
    const int labelH = 14;
    const int tbH = 16;
    const int knobTotalH = labelH + knobSize + tbH;

    // Centre each knob horizontally in its half
    int kx1 = panel.getX() + (knobAreaW - knobSize) / 2;
    int kx2 = panel.getX() + knobAreaW + 4 + (knobAreaW - knobSize) / 2;

    freqFromLabel.setBounds(panel.getX(), y, knobAreaW, labelH);
    freqToLabel.setBounds(panel.getX() + knobAreaW + 4, y, knobAreaW, labelH);
    y += labelH;

    freqFromKnob.setBounds(kx1, y, knobSize, knobSize + tbH);
    freqToKnob.setBounds(kx2, y, knobSize, knobSize + tbH);
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
        repaint();
    }
}

void SpectralCompareAudioProcessorEditor::applyGridColor()
{
    juce::Colour c;
    if (parseHexColour(gridColorInput.getText(), c))
    {
        audioProcessor.setGridColor(c);
        gridColorInput.setText(colourToHex(c), juce::dontSendNotification);
        repaint();
    }
}

void SpectralCompareAudioProcessorEditor::applyMainColor()
{
    juce::Colour c;
    if (parseHexColour(mainColorInput.getText(), c))
    {
        audioProcessor.setMainSpectrumColor(c);
        mainColorInput.setText(colourToHex(c), juce::dontSendNotification);
        repaint();
    }
}

void SpectralCompareAudioProcessorEditor::applySidebarColor()
{
    juce::Colour c;
    if (parseHexColour(sidebarColorInput.getText(), c))
    {
        audioProcessor.setSidebarColor(c);
        sidebarColorInput.setText(colourToHex(c), juce::dontSendNotification);
        repaint();
    }
}

void SpectralCompareAudioProcessorEditor::applyDeltaColor()
{
    juce::Colour c;
    if (parseHexColour(deltaColorInput.getText(), c))
    {
        audioProcessor.setDeltaColor(c);
        deltaColorInput.setText(colourToHex(c), juce::dontSendNotification);
        repaint();
    }
}

void SpectralCompareAudioProcessorEditor::applyDiffColor()
{
    juce::Colour c;
    if (parseHexColour(diffColorInput.getText(), c))
    {
        audioProcessor.setDiffColor(c);
        diffColorInput.setText(colourToHex(c), juce::dontSendNotification);
        repaint();
    }
}

void SpectralCompareAudioProcessorEditor::applySidechainColor()
{
    juce::Colour c;
    if (parseHexColour(sidechainColorInput.getText(), c))
    {
        audioProcessor.setSidechainSpectrumColor(c);
        sidechainColorInput.setText(colourToHex(c), juce::dontSendNotification);
        repaint();
    }
}