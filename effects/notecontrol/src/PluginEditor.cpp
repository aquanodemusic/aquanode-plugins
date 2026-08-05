#include "PluginEditor.h"
#include "PluginProcessor.h"

// ============================================================================
// Helpers
// ============================================================================
namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;

    // 12 perceptually distinct hues (one per note class C..B)
    const juce::Colour kNoteHues[12] =
    {
        juce::Colour::fromHSV(0.00f, 0.80f, 0.95f, 1.0f),  // C  — red
        juce::Colour::fromHSV(0.04f, 0.75f, 0.90f, 1.0f),  // C# — red-orange
        juce::Colour::fromHSV(0.09f, 0.80f, 0.95f, 1.0f),  // D  — orange
        juce::Colour::fromHSV(0.14f, 0.80f, 0.95f, 1.0f),  // D# — amber
        juce::Colour::fromHSV(0.20f, 0.80f, 0.95f, 1.0f),  // E  — yellow
        juce::Colour::fromHSV(0.33f, 0.75f, 0.88f, 1.0f),  // F  — green
        juce::Colour::fromHSV(0.42f, 0.75f, 0.88f, 1.0f),  // F# — cyan-green
        juce::Colour::fromHSV(0.55f, 0.80f, 0.95f, 1.0f),  // G  — cyan
        juce::Colour::fromHSV(0.60f, 0.80f, 0.95f, 1.0f),  // G# — sky blue
        juce::Colour::fromHSV(0.67f, 0.75f, 0.95f, 1.0f),  // A  — blue
        juce::Colour::fromHSV(0.75f, 0.80f, 0.90f, 1.0f),  // A# — indigo
        juce::Colour::fromHSV(0.83f, 0.75f, 0.90f, 1.0f),  // B  — violet
    };

    juce::Colour dimColour(juce::Colour c, float amount)
    {
        return c.withMultipliedBrightness(amount).withMultipliedSaturation(0.6f);
    }
}

// ============================================================================
// Constructor / Destructor
// ============================================================================
NoteControlAudioProcessorEditor::NoteControlAudioProcessorEditor(
    NoteControlAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    // ---- Dry/Wet slider ----
    dryWetSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    dryWetSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    dryWetSlider.setRange(0.0, 1.0, 0.01);
    addAndMakeVisible(dryWetSlider);

    dryWetLabel.setText("Dry/Wet", juce::dontSendNotification);
    dryWetLabel.setJustificationType(juce::Justification::centred);
    dryWetLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(dryWetLabel);

    dryWetAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, "dryWet", dryWetSlider);

    // ---- Reset button ----
    resetButton.setButtonText("Reset");
    resetButton.onClick = [this]
    {
        proc.resetRouting();
        proc.rebuildBinShiftTable();
        repaint();
    };
    addAndMakeVisible(resetButton);

    // ---- Enhance toggle ----
    enhanceButton.setButtonText("Enhance");
    enhanceButton.setClickingTogglesState(true);
    enhanceButton.setToggleState(proc.enhanceMode.load(), juce::dontSendNotification);
    enhanceButton.onClick = [this]
    {
        const bool on = enhanceButton.getToggleState();
        proc.enhanceMode.store(on);
        repaint(enhanceButton.getBounds().expanded(4));
    };
    addAndMakeVisible(enhanceButton);

    // ---- Pentatonic preset button ----
    pentaButton.setButtonText("Pentatonic");
    pentaButton.onClick = [this]
    {
        proc.applyPentatonicRouting();
        proc.rebuildBinShiftTable();
        repaint();
    };
    addAndMakeVisible(pentaButton);

    // ---- Shift buttons — rows (inputs) and columns (outputs) independently ----
    shiftUpButton.setButtonText("u");      // ↑
    shiftUpButton.onClick = [this]
    {
        proc.shiftInputs(-1);
        proc.rebuildBinShiftTable();
        repaint();
    };
    addAndMakeVisible(shiftUpButton);

    shiftDownButton.setButtonText("d");    // ↓
    shiftDownButton.onClick = [this]
    {
        proc.shiftInputs(+1);
        proc.rebuildBinShiftTable();
        repaint();
    };
    addAndMakeVisible(shiftDownButton);

    shiftLeftButton.setButtonText("l");    // ←
    shiftLeftButton.onClick = [this]
    {
        proc.shiftOutputs(-1);
        proc.rebuildBinShiftTable();
        repaint();
    };
    addAndMakeVisible(shiftLeftButton);

    shiftRightButton.setButtonText("r");   // →
    shiftRightButton.onClick = [this]
    {
        proc.shiftOutputs(+1);
        proc.rebuildBinShiftTable();
        repaint();
    };
    addAndMakeVisible(shiftRightButton);

    // ---- Size ----
    const int w = kLabelW + 12 * kCellSize + 6;
    const int h = kTopBar + kLabelW + 12 * kCellSize + kSpectrumH + kBottomPad + 6;
    setSize(w, h);

    startTimerHz(30);
}

NoteControlAudioProcessorEditor::~NoteControlAudioProcessorEditor()
{
    stopTimer();
}

// ============================================================================
// Timer
// ============================================================================
void NoteControlAudioProcessorEditor::timerCallback()
{
    proc.getFFTData(spectrumData.data(), NoteControlAudioProcessor::numBins);
    repaint(spectrumArea());

    // Keep button in sync with processor state (e.g. after setStateInformation)
    const bool procEnhance = proc.enhanceMode.load();
    if (enhanceButton.getToggleState() != procEnhance)
    {
        enhanceButton.setToggleState(procEnhance, juce::dontSendNotification);
        repaint(enhanceButton.getBounds().expanded(10));
    }
}

// ============================================================================
// Layout
// ============================================================================
juce::Rectangle<int> NoteControlAudioProcessorEditor::gridArea() const
{
    const int x = kLabelW;
    const int y = kTopBar + kLabelW;
    return { x, y, 12 * kCellSize, 12 * kCellSize };
}

juce::Rectangle<int> NoteControlAudioProcessorEditor::spectrumArea() const
{
    const auto g = gridArea();
    return { 0, g.getBottom() + 4, getWidth(), kSpectrumH };
}

juce::Rectangle<int> NoteControlAudioProcessorEditor::cellRect(int row, int col) const
{
    const auto g = gridArea();
    return { g.getX() + col * kCellSize,
             g.getY() + row * kCellSize,
             kCellSize, kCellSize };
}

std::pair<int,int> NoteControlAudioProcessorEditor::cellAtPoint(juce::Point<int> p) const
{
    const auto g = gridArea();
    if (!g.contains(p)) return {-1, -1};
    const int col = (p.x - g.getX()) / kCellSize;
    const int row = (p.y - g.getY()) / kCellSize;
    if (col < 0 || col >= 12 || row < 0 || row >= 12) return {-1, -1};
    return {row, col};
}

juce::Colour NoteControlAudioProcessorEditor::noteColour(int nc, float alpha) const
{
    if (nc < 0 || nc >= 12) return juce::Colours::grey.withAlpha(alpha);
    return kNoteHues[nc].withAlpha(alpha);
}

// ============================================================================
// Gate coordinate helpers
// ============================================================================
int NoteControlAudioProcessorEditor::gatePosToX(float t) const
{
    const auto sa = spectrumArea();
    return sa.getX() + juce::roundToInt(t * static_cast<float>(sa.getWidth() - 1));
}

float NoteControlAudioProcessorEditor::xToGatePos(int x) const
{
    const auto sa = spectrumArea();
    return juce::jlimit(0.0f, 1.0f,
        static_cast<float>(x - sa.getX()) / static_cast<float>(sa.getWidth() - 1));
}

NoteControlAudioProcessorEditor::GateDrag
NoteControlAudioProcessorEditor::hitTestGate(int x) const
{
    constexpr int kGrab = 8;   // grab radius in pixels
    const int xs = gatePosToX(proc.gateStart.load());
    const int xe = gatePosToX(proc.gateEnd  .load());
    // Prefer whichever handle is closer when they're very near each other
    const int ds = std::abs(x - xs);
    const int de = std::abs(x - xe);
    if (ds <= kGrab && ds <= de) return GateDrag::Start;
    if (de <= kGrab)             return GateDrag::End;
    return GateDrag::None;
}

// ============================================================================
// Resized
// ============================================================================
void NoteControlAudioProcessorEditor::resized()
{
    const int ctrlH = kTopBar - 8;

    // Dry/wet knob: top-right
    dryWetLabel .setBounds(getWidth() - 78,  4, 70, 14);
    dryWetSlider.setBounds(getWidth() - 78, 18, 70, ctrlH - 14);

    // Button row right→left:  [Enhance] [Reset] [Penta] [↑][↓] [←][→]
    const int bY = 10, bH = 24, bGap = 2;
    resetButton  .setBounds(getWidth() - 160,       bY, 70, bH);
    enhanceButton.setBounds(getWidth() - 248,       bY, 80, bH);
    pentaButton  .setBounds(getWidth() - 322,       bY, 66, bH);

    // Up/Down pair (rows — inputs)
    const int udX = getWidth() - 393;
    shiftUpButton  .setBounds(udX,          bY, 28, bH);
    shiftDownButton.setBounds(udX + 28 + bGap, bY, 28, bH);

    // Left/Right pair (cols — outputs)
    const int lrX = getWidth() - 461;
    shiftLeftButton .setBounds(lrX,           bY, 28, bH);
    shiftRightButton.setBounds(lrX + 28 + bGap, bY, 28, bH);
}

// ============================================================================
// Mouse
// ============================================================================
void NoteControlAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // --- Gate handle hit test first ---
    if (spectrumArea().contains(e.getPosition()))
    {
        const auto hit = hitTestGate(e.x);
        if (hit != GateDrag::None)
        {
            activeDrag = hit;
            return;   // consume event — don't fall through to grid
        }
    }

    activeDrag = GateDrag::None;

    // --- Grid click ---
    auto [row, col] = cellAtPoint(e.getPosition());
    if (row < 0) return;

    if (e.mods.isRightButtonDown())
        proc.noteRouting[row] = -1;
    else if (proc.noteRouting[row] == col)
        proc.noteRouting[row] = row;
    else
        proc.noteRouting[row] = col;

    proc.rebuildBinShiftTable();
    repaint(gridArea());
}

void NoteControlAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (activeDrag == GateDrag::None) return;

    const float newPos = xToGatePos(e.x);

    if (activeDrag == GateDrag::Start)
    {
        // Don't let start overtake end
        const float clampedPos = std::min(newPos, proc.gateEnd.load() - 0.01f);
        proc.gateStart.store(juce::jlimit(0.0f, 1.0f, clampedPos));
    }
    else
    {
        // Don't let end overtake start
        const float clampedPos = std::max(newPos, proc.gateStart.load() + 0.01f);
        proc.gateEnd.store(juce::jlimit(0.0f, 1.0f, clampedPos));
    }

    repaint(spectrumArea().expanded(4));
}

void NoteControlAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    activeDrag = GateDrag::None;
}

void NoteControlAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    // Change cursor when hovering a gate handle
    if (spectrumArea().contains(e.getPosition()) &&
        hitTestGate(e.x) != GateDrag::None)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    // Grid hover highlight
    auto [row, col] = cellAtPoint(e.getPosition());
    if (row != hoverRow || col != hoverCol)
    {
        hoverRow = row;
        hoverCol = col;
        repaint(gridArea());
    }
}

// ============================================================================
// Paint
// ============================================================================
void NoteControlAudioProcessorEditor::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawNoteLabels(g);
    drawCells(g);
    drawGrid(g);
    drawHoverHighlight(g);
    drawSpectrumStrip(g);
    drawLegend(g);
}

void NoteControlAudioProcessorEditor::drawBackground(juce::Graphics& g)
{
    // ---- Lush natural palette ----
    const juce::Colour bgBase      { 0xff0f2a18 };  // rich forest green
    const juce::Colour bgTopBar    { 0xff0a1e11 };  // dark canopy
    const juce::Colour bgRail      { 0xff0c2214 };  // side rail
    const juce::Colour textPrimary { 0xffe8f5e2 };  // warm white-green
    const juce::Colour textMuted   { 0xff7aab8a };  // sage

    g.fillAll(bgBase);

    // Top bar
    const auto ga = gridArea();
    g.setColour(bgTopBar);
    g.fillRect(0, 0, getWidth(), kTopBar);

    // Left rail (row labels)
    g.setColour(bgRail);
    g.fillRect(0, kTopBar, kLabelW, ga.getHeight() + kLabelW);

    // Column header band (above grid)
    g.setColour(bgRail);
    g.fillRect(kLabelW, kTopBar, 12 * kCellSize, kLabelW);

    // Top-bar bottom edge line
    g.setColour(juce::Colour(0xff1e6a38));
    g.drawHorizontalLine(kTopBar - 1, 0.0f, (float)getWidth());

    // Title
    g.setColour(textPrimary);
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.drawText("NoteControl", 10, 6, 180, 26, juce::Justification::centredLeft);

    g.setFont(juce::Font(10.0f));
    g.setColour(textMuted);
    g.drawText("Row in, Column out, right click mute",
               10, 28, 340, 13, juce::Justification::centredLeft);

    // ---- Helper lambda: draw a pill button ----
    auto drawPill = [&](juce::TextButton& btn,
                        juce::Colour body, juce::Colour border, juce::Colour label,
                        bool glow = false, juce::Colour glowCol = juce::Colours::transparentBlack)
    {
        const auto eb = btn.getBounds().toFloat();
        if (glow)
            for (int r = 10; r >= 2; r -= 2)
                g.setColour(glowCol.withAlpha(0.034f * r)),
                g.fillRoundedRectangle(eb.expanded((float)r), 7.0f);
        g.setColour(body);
        g.fillRoundedRectangle(eb, 5.0f);
        g.setColour(border);
        g.drawRoundedRectangle(eb.reduced(0.5f), 5.0f, 1.5f);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.setColour(label);
        g.drawText(btn.getButtonText(), btn.getBounds(), juce::Justification::centred);
    };

    // ---- Enhance (glowing teal when on) ----
    {
        const bool on = enhanceButton.getToggleState();
        drawPill(enhanceButton,
                 on ? juce::Colour(0xff1a6644) : juce::Colour(0xff122a1e),
                 on ? juce::Colour(0xff44ffaa) : juce::Colour(0xff2a5a3a),
                 on ? juce::Colour(0xffc8ffe8) : juce::Colour(0xff7aab8a),
                 on, juce::Colour(0xff44ffaa));
        // LED dot
        const auto eb = enhanceButton.getBounds().toFloat();
        g.setColour(on ? juce::Colour(0xff44ffaa) : juce::Colour(0xff2a5a3a));
        g.fillEllipse(eb.getX() + 9.0f, eb.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(on ? juce::Colour(0xffc8ffe8) : juce::Colour(0xff7aab8a));
        g.drawText("Enhance",
                   enhanceButton.getX() + 18, enhanceButton.getY(),
                   enhanceButton.getWidth() - 18, enhanceButton.getHeight(),
                   juce::Justification::centredLeft);
    }

    // ---- Reset ----
    drawPill(resetButton,
             juce::Colour(0xff122a1e), juce::Colour(0xff2a5a3a), juce::Colour(0xff7aab8a));

    // ---- Penta (earthy gold) ----
    drawPill(pentaButton,
             juce::Colour(0xff1a2a12), juce::Colour(0xff5a8830), juce::Colour(0xffb8dd60));

    // ---- Up/Down pair with group label "IN" ----
    const juce::Colour arrowBody   { 0xff122a1e };
    const juce::Colour arrowBorder { 0xff2a6655 };
    const juce::Colour arrowLabel  { 0xff66ccaa };
    drawPill(shiftUpButton,   arrowBody, arrowBorder, arrowLabel);
    drawPill(shiftDownButton, arrowBody, arrowBorder, arrowLabel);

    // Small group label between the two pairs
    {
        const int midX = (shiftUpButton.getX() + shiftDownButton.getRight()) / 2;
        g.setFont(juce::Font(8.5f));
        g.setColour(textMuted.withAlpha(0.7f));
        g.drawText("IN", midX - 10, shiftUpButton.getBottom() - 8, 20, 9,
                   juce::Justification::centred);
    }

    // ---- Left/Right pair with group label "OUT" ----
    drawPill(shiftLeftButton,  arrowBody, arrowBorder, arrowLabel);
    drawPill(shiftRightButton, arrowBody, arrowBorder, arrowLabel);

    {
        const int midX = (shiftLeftButton.getX() + shiftRightButton.getRight()) / 2;
        g.setFont(juce::Font(8.5f));
        g.setColour(textMuted.withAlpha(0.7f));
        g.drawText("OUT", midX - 12, shiftLeftButton.getBottom() - 8, 24, 9,
                   juce::Justification::centred);
    }

    // Row/column axis labels
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(textMuted);
    g.drawText("IN v",  0, kTopBar,               kLabelW,                  kLabelW / 2,
               juce::Justification::centred);
    g.drawText("OUT >", 0, kTopBar + kLabelW / 2, kLabelW + 12 * kCellSize, kLabelW / 2,
               juce::Justification::centred);
}

void NoteControlAudioProcessorEditor::drawNoteLabels(juce::Graphics& g)
{
    const auto ga = gridArea();
    g.setFont(juce::Font(12.0f, juce::Font::bold));

    for (int n = 0; n < 12; ++n)
    {
        const juce::Colour c = noteColour(n, 0.9f);

        // Column header (OUTPUT)
        const int cx = ga.getX() + n * kCellSize;
        const int cy = kTopBar;
        g.setColour(c.withAlpha(0.18f));
        g.fillRect(cx, cy, kCellSize, kLabelW);
        g.setColour(c);
        g.drawText(kNoteNames[n], cx, cy, kCellSize, kLabelW, juce::Justification::centred);

        // Row header (INPUT)
        const int rx = 0;
        const int ry = ga.getY() + n * kCellSize;
        g.setColour(c.withAlpha(0.18f));
        g.fillRect(rx, ry, kLabelW, kCellSize);
        g.setColour(c);
        g.drawText(kNoteNames[n], rx, ry, kLabelW, kCellSize, juce::Justification::centred);
    }
}

void NoteControlAudioProcessorEditor::drawCells(juce::Graphics& g)
{
    for (int row = 0; row < 12; ++row)
    {
        const int tgt = proc.noteRouting[row];

        for (int col = 0; col < 12; ++col)
        {
            const auto cr = cellRect(row, col);

            // ---- Background ----
            if (row == col)
            {
                // Diagonal (identity) — slightly lighter forest
                g.setColour(juce::Colour(0xff1a3a22));
                g.fillRect(cr);
            }
            else
            {
                g.setColour(juce::Colour(0xff132d1a));
                g.fillRect(cr);
            }

            // ---- Active routing cell ----
            if (tgt == col)
            {
                const bool isMute   = (tgt < 0);  // never true here since tgt==col>=0
                const bool isIdent  = (tgt == row);

                if (isIdent)
                {
                    // Identity: deep forest tint
                    g.setColour(juce::Colour(0xff1a3022));
                    g.fillRect(cr);
                    g.setColour(noteColour(row, 0.60f));
                    g.fillRoundedRectangle(cr.toFloat().reduced(5.0f), 5.0f);
                }
                else
                {
                    // Re-mapped: bright output colour fill
                    g.setColour(noteColour(col, 1.0f).withAlpha(0.20f));
                    g.fillRect(cr);
                    g.setColour(noteColour(col, 1.0f));
                    g.fillRoundedRectangle(cr.toFloat().reduced(4.0f), 6.0f);

                    // Draw arrow  src note ──▶ dst note
                    const float ax = cr.getCentreX(), ay = cr.getCentreY();
                    g.setColour(juce::Colours::black.withAlpha(0.8f));
                    g.setFont(juce::Font(11.0f, juce::Font::bold));
                    g.drawText(juce::String(kNoteNames[row])
                               + juce::String::fromUTF8("\xe2\x86\x92")  // UTF-8 →
                               + juce::String(kNoteNames[col]),
                               cr, juce::Justification::centred);
                }
            }

            // ---- Muted row indicator ----
            if (tgt < 0 && col == row)
            {
                g.setColour(juce::Colours::red.withAlpha(0.30f));
                g.fillRect(cr);
                g.setColour(juce::Colours::red.brighter(0.3f));
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText("MUTE", cr, juce::Justification::centred);
            }
        }
    }
}

void NoteControlAudioProcessorEditor::drawGrid(juce::Graphics& g)
{
    const auto ga = gridArea();

    // Fine grid lines — forest
    g.setColour(juce::Colour(0xff1e4a2a));

    for (int col = 0; col <= 12; ++col)
        g.drawVerticalLine(ga.getX() + col * kCellSize,
                           (float)ga.getY(), (float)ga.getBottom());

    for (int row = 0; row <= 12; ++row)
        g.drawHorizontalLine(ga.getY() + row * kCellSize,
                             (float)ga.getX(), (float)ga.getRight());

    // Outer border — medium forest green
    g.setColour(juce::Colour(0xff2d6644));
    g.drawRect(ga.expanded(1), 2);
}

void NoteControlAudioProcessorEditor::drawHoverHighlight(juce::Graphics& g)
{
    if (hoverRow < 0 || hoverCol < 0) return;

    const auto ga = gridArea();

    // Highlight entire row
    g.setColour(noteColour(hoverRow, 0.08f));
    g.fillRect(ga.getX(), ga.getY() + hoverRow * kCellSize,
               ga.getWidth(), kCellSize);

    // Highlight entire column
    g.setColour(noteColour(hoverCol, 0.08f));
    g.fillRect(ga.getX() + hoverCol * kCellSize, ga.getY(),
               kCellSize, ga.getHeight());

    // Highlight the hovered cell itself
    const auto cr = cellRect(hoverRow, hoverCol);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.fillRect(cr);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawRect(cr, 1);

    // Tooltip
    const int tgt = proc.noteRouting[hoverRow];
    juce::String tip;
    if (tgt < 0)
        tip = juce::String(kNoteNames[hoverRow]) + " > MUTE";
    else if (tgt == hoverRow)
        tip = juce::String(kNoteNames[hoverRow]) + " > " + kNoteNames[hoverRow] + " (passthrough)";
    else
        tip = juce::String(kNoteNames[hoverRow]) + " > " + kNoteNames[tgt];

    if (hoverCol != tgt)
        tip += "   [click to route > " + juce::String(kNoteNames[hoverCol]) + "]";

    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.drawText(tip, ga.getX(), ga.getBottom() - 16, ga.getWidth(), 16,
               juce::Justification::centred);
}

void NoteControlAudioProcessorEditor::drawSpectrumStrip(juce::Graphics& g)
{
    const auto sa = spectrumArea();
    if (sa.isEmpty()) return;

    // Background
    g.setColour(juce::Colour(0xff0a1e11));
    g.fillRect(sa);
    g.setColour(juce::Colour(0xff1e6a38));
    g.drawRect(sa, 1);

    // Label
    g.setFont(juce::Font(10.0f));
    g.setColour(juce::Colour(0xff4a8860).withAlpha(0.7f));
    g.drawText("Spectrum  (drag bars to set effect range)",
               sa.getX() + 4, sa.getY() + 2, 260, 12,
               juce::Justification::centredLeft);

    const int displayBins = NoteControlAudioProcessor::numBins;
    const int w  = sa.getWidth();
    const int h  = sa.getHeight() - 4;
    const int y0 = sa.getBottom() - 2;

    // Find peak for normalisation
    float peak = 1e-9f;
    for (int b = 1; b < displayBins; ++b)
        peak = std::max(peak, spectrumData[b]);

    // Build spectrum path
    juce::Path specPath;
    bool firstPoint = true;
    for (int px = 0; px < w; ++px)
    {
        const float t      = static_cast<float>(px) / static_cast<float>(w - 1);
        const float logBin = std::exp(t * std::log(static_cast<float>(displayBins - 1)));
        const int   b      = juce::jlimit(1, displayBins - 1, static_cast<int>(logBin));
        const float db     = juce::Decibels::gainToDecibels(spectrumData[b] / peak);
        const float norm   = juce::jlimit(0.0f, 1.0f, (db + 80.0f) / 80.0f);
        const int   py     = y0 - static_cast<int>(norm * h);

        if (firstPoint) { specPath.startNewSubPath((float)(sa.getX() + px), (float)y0); firstPoint = false; }
        specPath.lineTo((float)(sa.getX() + px), (float)py);
    }
    specPath.lineTo((float)sa.getRight(), (float)y0);
    specPath.closeSubPath();

    // Fill spectrum — deep forest to bright leaf gradient
    juce::ColourGradient grad(juce::Colour(0x7722cc66), 0.0f, (float)sa.getY(),
                              juce::Colour(0xff004422), 0.0f, (float)y0, false);
    g.setGradientFill(grad);
    g.fillPath(specPath);
    g.setColour(juce::Colour(0xbb55ee99));
    g.strokePath(specPath, juce::PathStrokeType(1.0f));

    // Octave tick marks
    g.setColour(juce::Colour(0xff4a8860).withAlpha(0.4f));
    g.setFont(juce::Font(9.0f));
    const double sr = proc.currentSampleRate;
    for (int octave = 0; octave <= 9; ++octave)
    {
        const double freq = 55.0 * std::pow(2.0, octave - 1.0);
        if (freq >= sr * 0.49) break;
        const float binF = static_cast<float>(freq * NoteControlAudioProcessor::fftSize / sr);
        const float t    = std::log(binF) / std::log(static_cast<float>(displayBins - 1));
        if (t < 0.0f || t > 1.0f) continue;
        const int px = sa.getX() + static_cast<int>(t * (w - 1));
        g.drawVerticalLine(px, (float)sa.getY(), (float)sa.getBottom());
        g.drawText("A" + juce::String(octave + 1),
                   px + 2, sa.getY() + 2, 24, 10, juce::Justification::centredLeft);
    }

    // ---- Gate overlay ----
    const float gs = proc.gateStart.load();
    const float ge = proc.gateEnd  .load();
    const int   xS = gatePosToX(gs);
    const int   xE = gatePosToX(ge);

    // Dark tint outside the gate
    const juce::Colour outsideTint { 0xaa000000 };
    g.setColour(outsideTint);
    g.fillRect(sa.getX(), sa.getY(),         xS - sa.getX(),     sa.getHeight());  // left
    g.fillRect(xE,        sa.getY(),         sa.getRight() - xE, sa.getHeight());  // right

    // Active region highlight — faint amber tint
    g.setColour(juce::Colour(0x22ffdd88));
    g.fillRect(xS, sa.getY(), xE - xS, sa.getHeight());

    // Gate handle bars
    const bool draggingStart = (activeDrag == GateDrag::Start);
    const bool draggingEnd   = (activeDrag == GateDrag::End);

    auto drawHandle = [&](int x, bool active)
    {
        // Vertical line
        g.setColour(active ? juce::Colour(0xffffdd44)
                           : juce::Colour(0xccffcc66));
        g.drawVerticalLine(x, (float)sa.getY(), (float)sa.getBottom());

        // Wider grab line (for usability)
        g.setColour((active ? juce::Colour(0x55ffdd44)
                            : juce::Colour(0x33ffcc66)));
        g.fillRect(x - 2, sa.getY(), 5, sa.getHeight());

        // Circle grab knob at vertical centre
        const float cy = sa.getCentreY();
        const float r  = active ? 7.0f : 5.5f;
        g.setColour(active ? juce::Colour(0xffffdd44)
                           : juce::Colour(0xccffcc66));
        g.fillEllipse((float)x - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colour(0xff0a1e11));
        g.drawEllipse((float)x - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);

        // Frequency label above knob
        const float normT  = static_cast<float>(x - sa.getX())
                             / static_cast<float>(sa.getWidth() - 1);
        const float logBin = std::exp(normT * std::log(static_cast<float>(displayBins - 1)));
        const int   bin    = juce::jlimit(1, displayBins - 1, static_cast<int>(logBin));
        const float freq   = static_cast<float>(bin) * static_cast<float>(sr) /
                             static_cast<float>(NoteControlAudioProcessor::fftSize);
        juce::String label = freq < 1000.0f
                             ? juce::String((int)freq) + "Hz"
                             : juce::String(freq / 1000.0f, 1) + "k";
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.setColour(active ? juce::Colour(0xffffdd44) : juce::Colour(0xccffcc66));
        const int lx = (x + 14 < sa.getRight() - 32) ? x + 4 : x - 36;
        g.drawText(label, lx, sa.getY() + 14, 36, 10, juce::Justification::centredLeft);
    };

    drawHandle(xS, draggingStart);
    drawHandle(xE, draggingEnd);
}

void NoteControlAudioProcessorEditor::drawLegend(juce::Graphics& g)
{
    // Small colour legend for the 12 notes, shown above the spectrum
    const auto sa = spectrumArea();
    const int ly  = sa.getY() - 18;
    const int sw  = 20;

    g.setFont(juce::Font(9.0f));
    for (int n = 0; n < 12; ++n)
    {
        const int lx = kLabelW + n * kCellSize + (kCellSize - sw) / 2;
        g.setColour(noteColour(n, 0.7f));
        g.fillRoundedRectangle((float)lx, (float)ly, (float)sw, 12.0f, 3.0f);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawText(kNoteNames[n], lx, ly, sw, 12, juce::Justification::centred);
    }
}

// ============================================================================
// createEditor (called from processor)
// ============================================================================
juce::AudioProcessorEditor* NoteControlAudioProcessor::createEditor()
{
    return new NoteControlAudioProcessorEditor(*this);
}
