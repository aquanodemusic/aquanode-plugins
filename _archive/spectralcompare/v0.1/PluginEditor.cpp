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
    b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a2a));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    b.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff55eedd));
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
    setSize(860, 540);

    mainDisplayData.fill(0.0f);
    sidechainDisplayData.fill(0.0f);
    frozenMainDisplay.fill(0.0f);

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

    // ---- Freeze button ----
    styleButton(freezeButton);
    freezeButton.setButtonText("Freeze Main");
    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this]()
    {
        mainIsFrozen = freezeButton.getToggleState();
        if (mainIsFrozen)
        {
            // Capture current live data as the frozen snapshot
            frozenMainDisplay = mainDisplayData;
        }
        freezeButton.setButtonText(mainIsFrozen ? "Frozen" : "Freeze Main");
    };
    addAndMakeVisible(freezeButton);

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

    setupColorRow(bgColorLabel,        "BG:",       bgColorInput,
                  audioProcessor.getBackgroundColor(),
                  [this] { applyBgColor(); });
    setupColorRow(gridColorLabel,      "Grid:",     gridColorInput,
                  audioProcessor.getGridColor(),
                  [this] { applyGridColor(); });
    setupColorRow(mainColorLabel,      "Main:",     mainColorInput,
                  audioProcessor.getMainSpectrumColor(),
                  [this] { applyMainColor(); });
    setupColorRow(sidechainColorLabel, "Sidechain:",sidechainColorInput,
                  audioProcessor.getSidechainSpectrumColor(),
                  [this] { applySidechainColor(); });
    setupColorRow(sidebarColorLabel,   "Sidebar:",  sidebarColorInput,
                  audioProcessor.getSidebarColor(),
                  [this] { applySidebarColor(); });

    styleButton(resetColorsButton);
    resetColorsButton.setButtonText("Reset Colors");
    resetColorsButton.onClick = [this]()
    {
        audioProcessor.resetColors();
        bgColorInput       .setText(colourToHex(audioProcessor.getBackgroundColor()),        juce::dontSendNotification);
        gridColorInput     .setText(colourToHex(audioProcessor.getGridColor()),              juce::dontSendNotification);
        mainColorInput     .setText(colourToHex(audioProcessor.getMainSpectrumColor()),      juce::dontSendNotification);
        sidechainColorInput.setText(colourToHex(audioProcessor.getSidechainSpectrumColor()), juce::dontSendNotification);
        sidebarColorInput  .setText(colourToHex(audioProcessor.getSidebarColor()),           juce::dontSendNotification);
        repaint();
    };
    addAndMakeVisible(resetColorsButton);

    startTimerHz(60);
}

SpectralCompareAudioProcessorEditor::~SpectralCompareAudioProcessorEditor()
{
    stopTimer();
}

// ============================================================================
void SpectralCompareAudioProcessorEditor::timerCallback()
{
    // Always fetch sidechain live
    audioProcessor.getSidechainFFTData(sidechainDisplayData.data(), audioProcessor.numBins);

    // For main: only update the display array if not frozen
    if (!mainIsFrozen)
        audioProcessor.getMainFFTData(mainDisplayData.data(), audioProcessor.numBins);
    // If frozen, mainDisplayData stays as the snapshot taken at freeze time

    repaint(canvasArea());
}

// ============================================================================
// Layout constants
// ============================================================================
static constexpr int kPanelW  = 160;  // right panel width
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
    // Log frequency mapping
    float logMin = std::log10(20.0f);
    float logMax = std::log10(nyquist());
    float binHz  = bin * nyquist() / (audioProcessor.numBins - 1);
    binHz = juce::jmax(binHz, 20.0f);
    float t = (std::log10(binHz) - logMin) / (logMax - logMin);
    return ca.getX() + t * ca.getWidth();
}

int SpectralCompareAudioProcessorEditor::xToBin(float x) const
{
    auto ca = canvasArea();
    float logMin = std::log10(20.0f);
    float logMax = std::log10(nyquist());
    float t = (x - ca.getX()) / (float)ca.getWidth();
    float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
    int bin  = juce::roundToInt(hz * (audioProcessor.numBins - 1) / nyquist());
    return juce::jlimit(0, audioProcessor.numBins - 1, bin);
}

// Map raw FFT magnitude to a Y pixel (log dB scale)
// We map dB range [minDB, maxDB] to [bottom, top] of canvas
float SpectralCompareAudioProcessorEditor::magToY(float mag) const
{
    auto ca = canvasArea();
    // Avoid log(0)
    const float minDB = -90.0f;
    const float maxDB =   0.0f;  // reference = 0 dBFS
    float dB = (mag > 0.0f) ? 20.0f * std::log10(mag) : minDB;
    dB = juce::jlimit(minDB, maxDB, dB);
    float t = (dB - maxDB) / (minDB - maxDB);  // 0 at top, 1 at bottom
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
    drawSpectrum(g, sidechainDisplayData, audioProcessor.getSidechainSpectrumColor(), 0.7f);
    drawSpectrum(g, mainDisplayData,      audioProcessor.getMainSpectrumColor(),      0.85f);
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

    // Build filled path from bottom
    juce::Path path;
    bool started = false;
    float firstX = 0.0f;

    for (int bin = 0; bin < nb; ++bin)
    {
        float x = binToX(bin);
        if (x < ca.getX() || x > ca.getRight()) continue;

        float y = magToY(data[bin]);
        y = juce::jlimit((float)ca.getY(), (float)ca.getBottom(), y);

        if (!started)
        {
            path.startNewSubPath(x, (float)ca.getBottom());
            path.lineTo(x, y);
            firstX = x;
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

    // Filled area
    g.setColour(colour.withAlpha(alpha * 0.35f));
    g.fillPath(path);

    // Stroke on top
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(1.5f));
}

void SpectralCompareAudioProcessorEditor::drawLabels(juce::Graphics& g)
{
    auto ca = canvasArea();

    // Legend in top-left corner
    g.setFont(11.0f);
    float lx = (float)ca.getX() + 8.0f, ly = (float)ca.getY() + 8.0f;

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

    // Freeze indicator
    if (mainIsFrozen)
    {
        g.setColour(juce::Colour(0xffff5500).withAlpha(0.85f));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText("FROZEN", ca.getRight() - 58, ca.getY() + 6, 52, 14,
                   juce::Justification::right, false);
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
    float mainDB  = (mainMag > 0) ? 20.0f * std::log10(mainMag) : -144.0f;

    float sideDB  = 0.0f;
    float sideMag = sidechainDisplayData[bin];
    sideDB        = (sideMag > 0) ? 20.0f * std::log10(sideMag) : -144.0f;

    juce::String info = freqStr
        + "  Main: " + juce::String(mainDB, 1) + " dB"
        + "  Side: " + juce::String(sideDB, 1) + " dB";

    // Tooltip box
    const int tw = 340, th = 18;
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

    int y = panel.getY();
    const int rowH = 22, gap = 6;

    // FFT size row
    fftSizeLabel.setBounds(panel.getX(), y, 60, rowH);
    fftSizeCombo .setBounds(panel.getX() + 62, y, panel.getWidth() - 62, rowH);
    y += rowH + gap;

    // Freeze
    freezeButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
    y += rowH + gap * 2;

    // Color rows — label on left, input on right
    auto colorRow = [&](juce::Label& lbl, juce::TextEditor& inp)
    {
        lbl.setBounds(panel.getX(), y, 65, rowH);
        inp.setBounds(panel.getX() + 67, y, panel.getWidth() - 67, rowH);
        y += rowH + gap;
    };

    colorRow(bgColorLabel,        bgColorInput);
    colorRow(gridColorLabel,      gridColorInput);
    colorRow(mainColorLabel,      mainColorInput);
    colorRow(sidechainColorLabel, sidechainColorInput);
    colorRow(sidebarColorLabel,   sidebarColorInput);

    y += gap;
    resetColorsButton.setBounds(panel.getX(), y, panel.getWidth(), rowH);
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
