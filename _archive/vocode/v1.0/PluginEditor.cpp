#include "PluginEditor.h"

//==============================================================================
VocoderDisplay::VocoderDisplay(VocodeAudioProcessor& p, const ColorSet*& palRef)
    : proc(p), pal(palRef)
{
    setOpaque(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(30);
}

VocoderDisplay::~VocoderDisplay() { stopTimer(); }

int VocoderDisplay::getNumBands() const noexcept
{
    return juce::jlimit(1, VocodeAudioProcessor::kMaxBands,
        (int)*proc.apvts.getRawParameterValue("numBands"));
}

void VocoderDisplay::paint(juce::Graphics& g)
{
    const ColorSet& P = *pal;   // active palette, updated by toggle

    const float w = (float)getWidth();
    const float h = (float)getHeight();

    // ── Background ────────────────────────────────────────────────────────────
    g.fillAll(P.displayBg);

    // Subtle horizontal grid lines (soil strata / scan lines)
    g.setColour(P.gridLine);
    for (int i = 1; i < 8; ++i)
        g.drawHorizontalLine(juce::roundToInt(h * (float)i / 8.f), 0.f, w);

    // ── Band data ─────────────────────────────────────────────────────────────
    const int   nb = getNumBands();
    const float slotW = w / (float)nb;

    for (int b = 0; b < nb; ++b)
    {
        const float level = proc.bandLevels[b].load(std::memory_order_relaxed);
        const float bwScale = juce::jlimit(0.f, 1.f, proc.bandwidthCurve[b]);

        const float barHeightRatio = juce::jlimit(0.f, 1.f, level * 5.5f);
        const float barH = barHeightRatio * h;
        const float barW = juce::jmax(1.f, bwScale * slotW * 0.82f);
        const float xC = (b + 0.5f) * slotW;

        if (barH < 1.f) continue;

        // Main bar -- gradient from dim (bottom) to fill colour (top)
        {
            juce::ColourGradient grad(P.accentDim.withAlpha(0.75f),
                xC, h,
                P.barFill.withAlpha(0.90f),
                xC, h - barH,
                false);
            g.setGradientFill(grad);
            g.fillRect(xC - barW * 0.5f, h - barH, barW, barH);
        }

        // Bright sand / phosphor cap
        if (barH >= 2.f)
        {
            const float capH = juce::jmin(3.f, barH);
            g.setColour(P.barTop.withAlpha(0.92f));
            g.fillRect(xC - barW * 0.5f, h - barH, barW, capH);
        }
    }

    // ── Bandwidth curve ───────────────────────────────────────────────────────
    if (nb > 1)
    {
        juce::Path line;
        for (int b = 0; b < nb; ++b)
        {
            const float bwScale = juce::jlimit(0.f, 1.f, proc.bandwidthCurve[b]);
            const float xC = (b + 0.5f) * slotW;
            const float yP = (1.f - bwScale) * h;
            if (b == 0) line.startNewSubPath(xC, yP);
            else        line.lineTo(xC, yP);
        }

        // Outer glow pass
        g.setColour(P.accentDim.withAlpha(0.25f));
        g.strokePath(line, juce::PathStrokeType(4.f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        // Main line pass
        g.setColour(P.accent.withAlpha(0.80f));
        g.strokePath(line, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        // Control-point dots
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

    // ── AUTO-VOCODE badge ─────────────────────────────────────────────────────
    // No hex/unicode escapes -- plain ASCII only.
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

    // ── Border ────────────────────────────────────────────────────────────────
    g.setColour(P.accentDark.withAlpha(0.90f));
    g.drawRect(getLocalBounds().toFloat(), 1.f);
}

void VocoderDisplay::mouseDown(const juce::MouseEvent& e)
{
    const int   nb = getNumBands();
    const float slotW = (float)getWidth() / (float)nb;
    dragIndex = juce::jlimit(0, nb - 1, (int)(e.position.x / slotW));
    proc.bandwidthCurve[dragIndex] =
        1.f - juce::jlimit(0.f, 1.f, e.position.y / (float)getHeight());
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

void VocoderDisplay::mouseUp(const juce::MouseEvent&) { dragIndex = -1; }

void VocoderDisplay::applyBrushAt(const juce::MouseEvent& e,
    int fromIndex, int toIndex)
{
    const float yNorm = 1.f - juce::jlimit(0.f, 1.f,
        e.position.y / (float)getHeight());
    for (int i = juce::jmin(fromIndex, toIndex);
        i <= juce::jmax(fromIndex, toIndex); ++i)
        proc.bandwidthCurve[i] = yNorm;
}

//==============================================================================
VocodeAudioProcessorEditor::VocodeAudioProcessorEditor(VocodeAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , display(p, activePalette)   // display holds a ref to activePalette
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

    // ── Palette toggle button ─────────────────────────────────────────────────
    paletteToggle.setButtonText("GREEN DESIGN");   // earth is default, so offer green
    paletteToggle.setClickingTogglesState(false);
    paletteToggle.onClick = [this]
        {
            const bool isNowGreen = (activePalette == &greenPalette());
            activePalette = isNowGreen ? &earthPalette() : &greenPalette();
            paletteToggle.setButtonText(isNowGreen ? "GREEN DESIGN" : "BROWN DESIGN");
            applyPaletteToKnobs();
            repaint();
            display.repaint();
        };
    addAndMakeVisible(paletteToggle);

    addAndMakeVisible(display);

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

    applyPaletteToKnobs();  // colour from activePalette at construction time
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
        sliders[i]->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        sliders[i]->setColour(juce::Slider::textBoxHighlightColourId, P.accentDark);
        sliders[i]->repaint();

        labels[i]->setColour(juce::Label::textColourId, P.textDim);
        labels[i]->repaint();
    }

    // Style the toggle button to match the active palette
    paletteToggle.setColour(juce::TextButton::buttonColourId, P.accentDark);
    paletteToggle.setColour(juce::TextButton::buttonOnColourId, P.accentDark);
    paletteToggle.setColour(juce::TextButton::textColourOffId, P.accentBright);
    paletteToggle.setColour(juce::TextButton::textColourOnId, P.accentBright);
    paletteToggle.repaint();
}

//==============================================================================
void VocodeAudioProcessorEditor::paint(juce::Graphics& g)
{
    const ColorSet& P = *activePalette;
    const int w = getWidth();
    const int h = getHeight();

    // ── Window background ─────────────────────────────────────────────────────
    g.fillAll(P.background);

    // ── Control panel top strip ───────────────────────────────────────────────
    {
        juce::ColourGradient grad(P.panelBg, 0.f, 0.f,
            P.background, 0.f, 165.f, false);
        g.setGradientFill(grad);
        g.fillRect(0, 0, w, 165);
    }

    // ── Thin dividers between knob slots ─────────────────────────────────────
    constexpr int kNumKnobs = 7;
    const int knobAreaW = w / kNumKnobs;
    g.setColour(P.accentDark.withAlpha(0.40f));
    for (int i = 1; i < kNumKnobs; ++i)
        g.drawVerticalLine(i * knobAreaW, 2.f, 158.f);

    // ── Title (single draw, no glow duplicates) ───────────────────────────────
    g.setColour(P.accentBright);
    g.setFont(juce::Font(22.f, juce::Font::bold));
    g.drawText("VOCODE", 0, 5, w, 26, juce::Justification::centred, false);

    // Subtitle
    g.setColour(P.textDim);
    g.setFont(juce::Font(9.5f, juce::Font::plain));
    g.drawText("phase vocoder  -  up to 256 bands", 0, 27, w, 14,
        juce::Justification::centred, false);

    // ── Separator ─────────────────────────────────────────────────────────────
    {
        juce::ColourGradient sep(P.accentDim.withAlpha(0.f), 0.f, 160.f,
            P.accentDim.withAlpha(0.7f), w * 0.5f, 160.f,
            true);
        g.setGradientFill(sep);
        g.fillRect(4, 160, w - 8, 2);
    }

    // ── Footer hint ───────────────────────────────────────────────────────────
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

    // ── Palette toggle -- top-right corner of the header strip ───────────────
    constexpr int kBtnW = 100, kBtnH = 20;
    paletteToggle.setBounds(w - kBtnW - 6, 6, kBtnW, kBtnH);

    // ── Knob row ──────────────────────────────────────────────────────────────
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

    // ── Display ───────────────────────────────────────────────────────────────
    const int displayTop = 163;
    const int displayBottom = h - 19;
    display.setBounds(4, displayTop, w - 8, displayBottom - displayTop);
}