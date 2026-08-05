#include "PluginEditor.h"

// Width of the mode-toggle button drawn in the top-left of the display.
static constexpr float kModeButtonW = 96.f;
static constexpr float kModeButtonH = 18.f;
static constexpr float kModeButtonX = 4.f;
static constexpr float kModeButtonY = 4.f;

//==============================================================================
VocoderDisplay::VocoderDisplay(VocodeAudioProcessor& p, const ColorSet*& palRef)
    : proc(p), pal(palRef)
{
    std::fill(std::begin(bandVolumeCurve), std::end(bandVolumeCurve), 1.f);
    loadBandVolumeCurve();

    setOpaque(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(30);
}

VocoderDisplay::~VocoderDisplay() { stopTimer(); }

//==============================================================================
void VocoderDisplay::loadBandVolumeCurve() noexcept
{
    // Restore editing-mode flag
    isEditingBandVolume = (int)proc.apvts.state.getProperty("bandVolumeMode", 0) != 0;

    // Restore the curve itself (stored as a comma-separated float string)
    const juce::String saved =
        proc.apvts.state.getProperty("bandVolumeCurve", "").toString();

    if (saved.isEmpty())
    {
        std::fill(std::begin(bandVolumeCurve), std::end(bandVolumeCurve), 1.f);
        return;
    }

    const juce::StringArray tokens = juce::StringArray::fromTokens(saved, ",", "");
    const int n = VocodeAudioProcessor::kMaxBands;
    for (int i = 0; i < n; ++i)
    {
        bandVolumeCurve[i] = (i < tokens.size())
            ? juce::jlimit(0.f, 1.2f, tokens[i].getFloatValue())
            : 1.f;
        proc.bandVolumeCurve[i] = bandVolumeCurve[i];
    }
}

void VocoderDisplay::saveBandVolumeCurve() noexcept
{
    // Persist mode flag
    proc.apvts.state.setProperty("bandVolumeMode",
        isEditingBandVolume ? 1 : 0, nullptr);

    // Serialise curve (256 values, 4 decimal places each → ~2 KB string, negligible)
    juce::String s;
    s.preallocateBytes(VocodeAudioProcessor::kMaxBands * 7);
    for (int i = 0; i < VocodeAudioProcessor::kMaxBands; ++i)
    {
        if (i > 0) s += ",";
        s += juce::String(bandVolumeCurve[i], 4);
    }
    proc.apvts.state.setProperty("bandVolumeCurve", s, nullptr);
}

//==============================================================================
int VocoderDisplay::getNumBands() const noexcept
{
    return juce::jlimit(1, VocodeAudioProcessor::kMaxBands,
        (int)*proc.apvts.getRawParameterValue("numBands"));
}

//==============================================================================
void VocoderDisplay::paint(juce::Graphics& g)
{
    const ColorSet& P = *pal;
    const float w = (float)getWidth();
    const float h = (float)getHeight();

    // ── Background ────────────────────────────────────────────────────────────
    g.fillAll(P.displayBg);

    // Subtle horizontal grid lines
    g.setColour(P.gridLine);
    for (int i = 1; i < 8; ++i)
        g.drawHorizontalLine(juce::roundToInt(h * (float)i / 8.f), 0.f, w);

    // ── Band Vol unity guide line (only in band-vol editing mode) ─────────────
    // Drawn early so bars paint on top of it.
    if (isEditingBandVolume)
    {
        // y-position where multiplier == 1.0 (i.e. no change)
        const float unityY = (1.f - 1.f / 1.2f) * h;
        g.setColour(P.accentDim.withAlpha(0.35f));
        g.drawHorizontalLine(juce::roundToInt(unityY), 0.f, w);

        // Small "1.0x" label on the right edge
        g.setColour(P.textDim.withAlpha(0.60f));
        g.setFont(juce::Font(8.f, juce::Font::plain));
        g.drawText("1.0x",
            juce::Rectangle<float>(w - 32.f, unityY - 9.f, 28.f, 10.f),
            juce::Justification::centredRight, false);
    }

    // ── Band data (bars) ──────────────────────────────────────────────────────
    const int   nb = getNumBands();
    const float slotW = w / (float)nb;

    for (int b = 0; b < nb; ++b)
    {
        const float level = proc.bandLevels[b].load(std::memory_order_relaxed);
        const float bwScale = juce::jlimit(0.f, 1.f, proc.bandwidthCurve[b]);

        // Band-count compensation + band-volume multiplier (always active)
        const float bandCountScale = std::sqrt(static_cast<float>(nb) / 16.f);
        const float bvMult = bandVolumeCurve[b]; // 0.0 – 1.2
        const float barHeightRatio =
            juce::jlimit(0.f, 1.f, level * 40.f * bandCountScale * bvMult);

        const float barH = barHeightRatio * h;
        const float barW = juce::jmax(1.f, bwScale * slotW * 0.82f);
        const float xC = (b + 0.5f) * slotW;

        if (barH < 1.f) continue;

        // Main bar — gradient bottom (dim) to top (fill)
        {
            juce::ColourGradient grad(P.accentDim.withAlpha(0.75f),
                xC, h,
                P.barFill.withAlpha(0.90f),
                xC, h - barH,
                false);
            g.setGradientFill(grad);
            g.fillRect(xC - barW * 0.5f, h - barH, barW, barH);
        }

        // Bright cap
        if (barH >= 2.f)
        {
            const float capH = juce::jmin(3.f, barH);
            g.setColour(P.barTop.withAlpha(0.92f));
            g.fillRect(xC - barW * 0.5f, h - barH, barW, capH);
        }
    }

    // ── Active editable curve ─────────────────────────────────────────────────
    if (nb > 1)
    {
        if (!isEditingBandVolume)
        {
            // ── Bandwidth curve (original) ────────────────────────────────────
            juce::Path line;
            for (int b = 0; b < nb; ++b)
            {
                const float bwScale = juce::jlimit(0.f, 1.f, proc.bandwidthCurve[b]);
                const float xC = (b + 0.5f) * slotW;
                const float yP = (1.f - bwScale) * h;
                if (b == 0) line.startNewSubPath(xC, yP);
                else        line.lineTo(xC, yP);
            }

            g.setColour(P.accentDim.withAlpha(0.25f));
            g.strokePath(line, juce::PathStrokeType(4.f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(P.accent.withAlpha(0.80f));
            g.strokePath(line, juce::PathStrokeType(1.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (slotW >= 3.5f)
            {
                const float ptR = juce::jlimit(2.f, 6.f, slotW * 0.26f);
                for (int b = 0; b < nb; ++b)
                {
                    const float bwScale = juce::jlimit(0.f, 1.f, proc.bandwidthCurve[b]);
                    const float xC = (b + 0.5f) * slotW;
                    const float yP = (1.f - bwScale) * h;
                    g.setColour(P.accentBright.withAlpha(0.90f));
                    g.fillEllipse(xC - ptR, yP - ptR, ptR * 2.f, ptR * 2.f);
                    g.setColour(P.accentDark.withAlpha(0.70f));
                    g.drawEllipse(xC - ptR, yP - ptR, ptR * 2.f, ptR * 2.f, 0.8f);
                }
            }
        }
        else
        {
            // ── Band Volume curve ─────────────────────────────────────────────
            // y=top → 1.2×   y=bottom → 0.0×
            // yP = (1 - bvMult/1.2) * h
            juce::Path line;
            for (int b = 0; b < nb; ++b)
            {
                const float bvMult = juce::jlimit(0.f, 1.2f, bandVolumeCurve[b]);
                const float xC = (b + 0.5f) * slotW;
                const float yP = (1.f - bvMult / 1.2f) * h;
                if (b == 0) line.startNewSubPath(xC, yP);
                else        line.lineTo(xC, yP);
            }

            // Glow pass (use accentBright so it's visually distinct from bandwidth)
            g.setColour(P.accentBright.withAlpha(0.18f));
            g.strokePath(line, juce::PathStrokeType(5.f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Main line
            g.setColour(P.accentBright.withAlpha(0.85f));
            g.strokePath(line, juce::PathStrokeType(1.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (slotW >= 3.5f)
            {
                const float ptR = juce::jlimit(2.f, 6.f, slotW * 0.26f);
                for (int b = 0; b < nb; ++b)
                {
                    const float bvMult = juce::jlimit(0.f, 1.2f, bandVolumeCurve[b]);
                    const float xC = (b + 0.5f) * slotW;
                    const float yP = (1.f - bvMult / 1.2f) * h;
                    g.setColour(P.barTop.withAlpha(0.92f));
                    g.fillEllipse(xC - ptR, yP - ptR, ptR * 2.f, ptR * 2.f);
                    g.setColour(P.accentDark.withAlpha(0.70f));
                    g.drawEllipse(xC - ptR, yP - ptR, ptR * 2.f, ptR * 2.f, 0.8f);
                }
            }
        }
    }

    // ── AUTO-VOCODE badge (top-right) ─────────────────────────────────────────
    if (proc.isAutoVocoding.load(std::memory_order_relaxed))
    {
        constexpr float bW = 108.f, bH = 18.f;
        const float bX = w - bW - 6.f;
        const float bY = 6.f;
        g.setColour(P.autoBadgeBg.withAlpha(0.88f));
        g.fillRoundedRectangle(bX, bY, bW, bH, 4.f);
        g.setColour(P.accentDark.withAlpha(0.60f));
        g.drawRoundedRectangle(bX, bY, bW, bH, 4.f, 1.f);
        g.setColour(P.autoBadgeFg);
        g.setFont(juce::Font(10.f, juce::Font::bold));
        g.drawText("[AUTO-VOCODE]",
            juce::Rectangle<float>(bX, bY, bW, bH),
            juce::Justification::centred, false);
    }

    // ── Mode-toggle button (top-left) ─────────────────────────────────────────
    {
        const juce::Rectangle<float> btn{ kModeButtonX, kModeButtonY,
                                           kModeButtonW, kModeButtonH };

        // Background: use accentDark for active (band-vol) mode so it stands out
        const juce::Colour btnBg = isEditingBandVolume
            ? P.accentBright.withAlpha(0.88f)
            : P.accentDark.withAlpha(0.82f);
        const juce::Colour btnFg = isEditingBandVolume
            ? P.accentDark
            : P.accentBright;

        g.setColour(btnBg);
        g.fillRoundedRectangle(btn, 4.f);
        g.setColour(P.accentDim.withAlpha(0.55f));
        g.drawRoundedRectangle(btn, 4.f, 0.8f);

        g.setColour(btnFg);
        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.drawText(isEditingBandVolume ? "BAND VOL" : "BANDWIDTH",
            btn, juce::Justification::centred, false);
    }

    // ── Border ────────────────────────────────────────────────────────────────
    g.setColour(P.accentDark.withAlpha(0.90f));
    g.drawRect(getLocalBounds().toFloat(), 1.f);
}

//==============================================================================
void VocoderDisplay::mouseDown(const juce::MouseEvent& e)
{
    // Mode-toggle button hit test (top-left corner)
    const juce::Rectangle<float> btn{ kModeButtonX, kModeButtonY,
                                       kModeButtonW, kModeButtonH };
    if (btn.contains(e.position))
    {
        isEditingBandVolume = !isEditingBandVolume;
        saveBandVolumeCurve();   // persist mode flag immediately
        repaint();
        return;                  // don't start a drag
    }

    // Normal curve editing
    const int   nb = getNumBands();
    const float slotW = (float)getWidth() / (float)nb;
    dragIndex = juce::jlimit(0, nb - 1, (int)(e.position.x / slotW));

    if (isEditingBandVolume)
    {
        const float yNorm = 1.f - juce::jlimit(0.f, 1.f,
            e.position.y / (float)getHeight());
        bandVolumeCurve[dragIndex] = yNorm * 1.2f;
        proc.bandVolumeCurve[dragIndex] = bandVolumeCurve[dragIndex];
    }
    else
    {
        proc.bandwidthCurve[dragIndex] =
            1.f - juce::jlimit(0.f, 1.f, e.position.y / (float)getHeight());
    }
}

void VocoderDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIndex < 0) return;
    const int   nb = getNumBands();
    const float slotW = (float)getWidth() / (float)nb;
    const int   newIdx = juce::jlimit(0, nb - 1, (int)(e.position.x / slotW));
    applyBrushAt(e, dragIndex, newIdx);
    dragIndex = newIdx;
}

void VocoderDisplay::mouseUp(const juce::MouseEvent&)
{
    if (dragIndex >= 0 && isEditingBandVolume)
        saveBandVolumeCurve();   // flush curve + mode to apvts.state

    dragIndex = -1;
}

void VocoderDisplay::applyBrushAt(const juce::MouseEvent& e,
    int fromIndex, int toIndex)
{
    const float yNorm = 1.f - juce::jlimit(0.f, 1.f,
        e.position.y / (float)getHeight());

    for (int i = juce::jmin(fromIndex, toIndex);
        i <= juce::jmax(fromIndex, toIndex); ++i)
    {
        if (isEditingBandVolume)
        {
            bandVolumeCurve[i] = yNorm * 1.2f;    // 0.0 – 1.2
            proc.bandVolumeCurve[i] = bandVolumeCurve[i];
        }
        else
            proc.bandwidthCurve[i] = yNorm;         // 0.0 – 1.0
    }
}

//==============================================================================
const ColorSet* VocodeAudioProcessorEditor::getPaletteForIndex(int id) noexcept
{
    switch (id)
    {
    case 1:  return &greenBlackPalette();
    case 2:  return &greenWhitePalette();
    case 3:  return &brownWhitePalette();
    case 4:  return &blueWhitePalette();
    default: return &brownWhitePalette();
    }
}

//==============================================================================
VocodeAudioProcessorEditor::VocodeAudioProcessorEditor(VocodeAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , display(p, activePalette)
{
    setupKnob(bandsSlider, bandsLabel, "BANDS");
    setupKnob(attackSlider, attackLabel, "ATTACK");
    setupKnob(releaseSlider, releaseLabel, "RELEASE");
    setupKnob(orderSlider, orderLabel, "BAND ORDER");
    setupKnob(compressionSlider, compressionLabel, "COMP");
    setupKnob(dryWetSlider, dryWetLabel, "DRY / WET");
    setupKnob(gainSlider, gainLabel, "GAIN");

    bandsAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (p.apvts, "numBands", bandsSlider);
    attackAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (p.apvts, "attack", attackSlider);
    releaseAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (p.apvts, "release", releaseSlider);
    orderAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (p.apvts, "bandOrder", orderSlider);
    compressionAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (p.apvts, "compression", compressionSlider);
    dryWetAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (p.apvts, "dryWet", dryWetSlider);
    gainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (p.apvts, "outputGain", gainSlider);

    // Palette ComboBox
    paletteCombo.addItem("Green Black", 1);
    paletteCombo.addItem("Green White", 2);
    paletteCombo.addItem("Brown White", 3);
    paletteCombo.addItem("Blue White", 4);

    const int savedId = juce::jlimit(1, 4,
        (int)audioProcessor.apvts.state.getProperty("paletteIndex", 3));
    paletteCombo.setSelectedId(savedId, juce::dontSendNotification);
    activePalette = getPaletteForIndex(savedId);

    paletteCombo.onChange = [this]
        {
            const int id = paletteCombo.getSelectedId();
            audioProcessor.apvts.state.setProperty("paletteIndex", id, nullptr);
            activePalette = getPaletteForIndex(id);
            applyPaletteToKnobs();
            repaint();
            display.repaint();
        };

    addAndMakeVisible(paletteCombo);
    addAndMakeVisible(display);

    applyPaletteToKnobs();

    setSize(980, 520);
    setResizable(true, true);
    setResizeLimits(700, 380, 1600, 1000);
}

VocodeAudioProcessorEditor::~VocodeAudioProcessorEditor() {}

//==============================================================================
void VocodeAudioProcessorEditor::setupKnob(juce::Slider& s,
    juce::Label& l,
    const juce::String& text)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 16);
    addAndMakeVisible(s);

    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(10.f, juce::Font::bold));
    addAndMakeVisible(l);
}

void VocodeAudioProcessorEditor::applyPaletteToKnobs()
{
    const ColorSet& P = *activePalette;

    juce::Slider* sliders[] = { &bandsSlider, &attackSlider, &releaseSlider,
                                 &orderSlider, &compressionSlider,
                                 &dryWetSlider, &gainSlider };
    juce::Label* labels[] = { &bandsLabel,  &attackLabel,  &releaseLabel,
                                 &orderLabel,  &compressionLabel,
                                 &dryWetLabel, &gainLabel };

    constexpr int kN = 7;
    for (int i = 0; i < kN; ++i)
    {
        sliders[i]->setColour(juce::Slider::rotarySliderFillColourId, P.accent);
        sliders[i]->setColour(juce::Slider::rotarySliderOutlineColourId, P.accentDark);
        sliders[i]->setColour(juce::Slider::thumbColourId, P.accentBright);
        sliders[i]->setColour(juce::Slider::trackColourId, P.accentDim);
        sliders[i]->setColour(juce::Slider::textBoxTextColourId, P.textCol);
        sliders[i]->setColour(juce::Slider::textBoxBackgroundColourId, P.panelBg);
        sliders[i]->setColour(juce::Slider::textBoxOutlineColourId,
            juce::Colours::transparentBlack);
        sliders[i]->setColour(juce::Slider::textBoxHighlightColourId, P.accentDark);
        sliders[i]->repaint();

        labels[i]->setColour(juce::Label::textColourId, P.textDim);
        labels[i]->repaint();
    }

    paletteCombo.setColour(juce::ComboBox::backgroundColourId, P.accentDark);
    paletteCombo.setColour(juce::ComboBox::textColourId, P.accentBright);
    paletteCombo.setColour(juce::ComboBox::outlineColourId, P.accentDim);
    paletteCombo.setColour(juce::ComboBox::arrowColourId, P.accentBright);
    paletteCombo.setColour(juce::ComboBox::buttonColourId, P.accentDark);
    paletteCombo.setColour(juce::PopupMenu::backgroundColourId, P.panelBg);
    paletteCombo.setColour(juce::PopupMenu::textColourId, P.textCol);
    paletteCombo.setColour(juce::PopupMenu::highlightedBackgroundColourId, P.accentDark);
    paletteCombo.setColour(juce::PopupMenu::highlightedTextColourId, P.accentBright);
    paletteCombo.repaint();
}

//==============================================================================
void VocodeAudioProcessorEditor::paint(juce::Graphics& g)
{
    const ColorSet& P = *activePalette;
    const int w = getWidth();
    const int h = getHeight();

    g.fillAll(P.background);

    {
        juce::ColourGradient grad(P.panelBg, 0.f, 0.f,
            P.background, 0.f, 165.f, false);
        g.setGradientFill(grad);
        g.fillRect(0, 0, w, 165);
    }

    constexpr int kNumKnobs = 7;
    const int knobAreaW = w / kNumKnobs;
    g.setColour(P.accentDark.withAlpha(0.40f));
    for (int i = 1; i < kNumKnobs; ++i)
        g.drawVerticalLine(i * knobAreaW, 2.f, 158.f);

    g.setColour(P.accentBright);
    g.setFont(juce::Font(22.f, juce::Font::bold));
    g.drawText("VOCODE", 0, 5, w, 26, juce::Justification::centred, false);

    g.setColour(P.textDim);
    g.setFont(juce::Font(9.5f, juce::Font::plain));
    g.drawText("phase vocoder  -  up to 256 bands", 0, 27, w, 14,
        juce::Justification::centred, false);

    {
        juce::ColourGradient sep(P.accentDim.withAlpha(0.f), 0.f, 160.f,
            P.accentDim.withAlpha(0.7f), w * 0.5f, 160.f, true);
        g.setGradientFill(sep);
        g.fillRect(4, 160, w - 8, 2);
    }

    g.setColour(P.panelBg.withAlpha(0.50f));
    g.fillRect(0, h - 18, w, 18);
    g.setColour(P.textDim.withAlpha(0.55f));
    g.setFont(juce::Font(8.5f, juce::Font::plain));
    g.drawText("drag bandwidth curve  -  low = narrow  -  high = wide",
        0, h - 17, w, 16, juce::Justification::centred, false);
}

void VocodeAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    constexpr int kComboW = 120, kComboH = 22;
    paletteCombo.setBounds(w - kComboW - 6, 5, kComboW, kComboH);

    constexpr int kNumKnobs = 7;
    const int controlY = 42;
    const int controlH = 118;
    const int labelH = 15;
    const int knobAreaW = w / kNumKnobs;

    juce::Slider* sliders[] = { &bandsSlider, &attackSlider, &releaseSlider,
                                 &orderSlider, &compressionSlider,
                                 &dryWetSlider, &gainSlider };
    juce::Label* labels[] = { &bandsLabel,  &attackLabel,  &releaseLabel,
                                 &orderLabel,  &compressionLabel,
                                 &dryWetLabel, &gainLabel };

    for (int i = 0; i < kNumKnobs; ++i)
    {
        const int x = i * knobAreaW;
        labels[i]->setBounds(x, controlY, knobAreaW, labelH);
        sliders[i]->setBounds(x, controlY + labelH, knobAreaW, controlH - labelH);
    }

    const int displayTop = 163;
    const int displayBottom = h - 19;
    display.setBounds(4, displayTop, w - 8, displayBottom - displayTop);
}