#include "PluginEditor.h"

// Mode-toggle button — doubled in size vs the original 96×18.
static constexpr float kModeButtonW = 192.f;
static constexpr float kModeButtonH = 36.f;
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
// loadBandVolumeCurve: reads directly from proc.bandVolumeCurve[], which is
// authoritatively restored by VocodeAudioProcessor::setStateInformation().
// This ensures DAW state recall works correctly.
//==============================================================================
void VocoderDisplay::loadBandVolumeCurve() noexcept
{
    // Restore editing-mode flag (stored on the apvts state node).
    isEditingBandVolume =
        (int)proc.apvts.state.getProperty("bandVolumeMode", 0) != 0;

    // Mirror proc's authoritative curve array into the GUI-thread copy.
    for (int i = 0; i < VocodeAudioProcessor::kMaxBands; ++i)
        bandVolumeCurve[i] = proc.bandVolumeCurve[i];
}

//==============================================================================
// saveBandVolumeCurve: persists only the mode flag.
// The curve values are already live in proc.bandVolumeCurve[] and will be
// serialised by VocodeAudioProcessor::getStateInformation() on DAW save.
//==============================================================================
void VocoderDisplay::saveBandVolumeCurve() noexcept
{
    proc.apvts.state.setProperty(
        "bandVolumeMode", isEditingBandVolume ? 1 : 0, nullptr);
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

    // ── Band-Vol unity guide line (always shown as a subtle reference) ────────
    {
        const float unityY = (1.f - 1.f / 1.2f) * h;
        g.setColour(P.accentDim.withAlpha(0.22f));
        g.drawHorizontalLine(juce::roundToInt(unityY), 0.f, w);

        g.setColour(P.textDim.withAlpha(0.40f));
        g.setFont(juce::Font(8.f, juce::Font::plain));
        g.drawText("1.0x",
            juce::Rectangle<float>(w - 32.f, unityY - 9.f, 28.f, 10.f),
            juce::Justification::centredRight, false);
    }

    // ── Band data (bars) ──────────────────────────────────────────────────────
    const int   nb    = getNumBands();
    const float slotW = w / (float)nb;

    for (int b = 0; b < nb; ++b)
    {
        const float level   = proc.bandLevels[b].load(std::memory_order_relaxed);
        const float bwScale = juce::jlimit(0.f, 1.f, proc.bandwidthCurve[b]);

        const float bandCountScale = std::sqrt(static_cast<float>(nb) / 16.f);
        const float bvMult         = bandVolumeCurve[b];
        const float barHeightRatio =
            juce::jlimit(0.f, 1.f, level * 40.f * bandCountScale * bvMult);

        const float barH = barHeightRatio * h;
        const float barW = juce::jmax(1.f, bwScale * slotW * 0.82f);
        const float xC   = (b + 0.5f) * slotW;

        if (barH < 1.f) continue;

        {
            juce::ColourGradient grad(P.accentDim.withAlpha(0.75f),
                xC, h,
                P.barFill.withAlpha(0.90f),
                xC, h - barH,
                false);
            g.setGradientFill(grad);
            g.fillRect(xC - barW * 0.5f, h - barH, barW, barH);
        }

        if (barH >= 2.f)
        {
            const float capH = juce::jmin(3.f, barH);
            g.setColour(P.barTop.withAlpha(0.92f));
            g.fillRect(xC - barW * 0.5f, h - barH, barW, capH);
        }
    }

    // ── Both curves always visible — inactive one is dimmed ───────────────────
    if (nb > 1)
    {
        // ── Bandwidth curve ───────────────────────────────────────────────────
        {
            const bool  active    = !isEditingBandVolume;
            const float lineAlpha = active ? 0.80f : 0.28f;
            const float glowAlpha = active ? 0.25f : 0.07f;

            juce::Path line;
            for (int b = 0; b < nb; ++b)
            {
                const float bwScale = juce::jlimit(0.f, 1.f, proc.bandwidthCurve[b]);
                const float xC = (b + 0.5f) * slotW;
                const float yP = (1.f - bwScale) * h;
                if (b == 0) line.startNewSubPath(xC, yP);
                else        line.lineTo(xC, yP);
            }

            g.setColour(P.accentDim.withAlpha(glowAlpha));
            g.strokePath(line, juce::PathStrokeType(4.f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(P.accent.withAlpha(lineAlpha));
            g.strokePath(line, juce::PathStrokeType(1.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (active && slotW >= 3.5f)
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

        // ── Band Volume curve ─────────────────────────────────────────────────
        {
            const bool  active    = isEditingBandVolume;
            const float lineAlpha = active ? 0.85f : 0.28f;
            const float glowAlpha = active ? 0.18f : 0.06f;

            juce::Path line;
            for (int b = 0; b < nb; ++b)
            {
                const float bvMult = juce::jlimit(0.f, 1.2f, bandVolumeCurve[b]);
                const float xC = (b + 0.5f) * slotW;
                const float yP = (1.f - bvMult / 1.2f) * h;
                if (b == 0) line.startNewSubPath(xC, yP);
                else        line.lineTo(xC, yP);
            }

            g.setColour(P.accentBright.withAlpha(glowAlpha));
            g.strokePath(line, juce::PathStrokeType(5.f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(P.accentBright.withAlpha(lineAlpha));
            g.strokePath(line, juce::PathStrokeType(1.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (active && slotW >= 3.5f)
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

    // ── Mode-toggle button (top-left) — doubled in size (192 × 36) ───────────
    {
        const juce::Rectangle<float> btn{ kModeButtonX, kModeButtonY,
                                           kModeButtonW, kModeButtonH };

        const juce::Colour btnBg = isEditingBandVolume
            ? P.accentBright.withAlpha(0.88f)
            : P.accentDark.withAlpha(0.82f);
        const juce::Colour btnFg = isEditingBandVolume
            ? P.accentDark
            : P.accentBright;

        g.setColour(btnBg);
        g.fillRoundedRectangle(btn, 6.f);
        g.setColour(P.accentDim.withAlpha(0.55f));
        g.drawRoundedRectangle(btn, 6.f, 0.8f);

        g.setColour(btnFg);
        g.setFont(juce::Font(13.f, juce::Font::bold));
        g.drawText(isEditingBandVolume ? "BAND VOL" : "BANDWIDTH",
            btn.withTrimmedBottom(btn.getHeight() * 0.35f),
            juce::Justification::centredBottom, false);

        g.setColour(btnFg.withAlpha(0.60f));
        g.setFont(juce::Font(8.5f, juce::Font::plain));
        g.drawText("click to change mode",
            btn.withTrimmedTop(btn.getHeight() * 0.60f),
            juce::Justification::centredTop, false);
    }

    // ── Border ────────────────────────────────────────────────────────────────
    g.setColour(P.accentDark.withAlpha(0.90f));
    g.drawRect(getLocalBounds().toFloat(), 1.f);
}

//==============================================================================
void VocoderDisplay::mouseDown(const juce::MouseEvent& e)
{
    const juce::Rectangle<float> btn{ kModeButtonX, kModeButtonY,
                                       kModeButtonW, kModeButtonH };
    if (btn.contains(e.position))
    {
        isEditingBandVolume = !isEditingBandVolume;
        saveBandVolumeCurve();
        repaint();
        return;
    }

    const int   nb    = getNumBands();
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

    const int   nb    = getNumBands();
    const float slotW = (float)getWidth() / (float)nb;
    const int   newIdx = juce::jlimit(0, nb - 1, (int)(e.position.x / slotW));
    applyBrushAt(e, dragIndex, newIdx);
    dragIndex = newIdx;
}

void VocoderDisplay::mouseUp(const juce::MouseEvent&)
{
    if (dragIndex >= 0 && isEditingBandVolume)
        saveBandVolumeCurve();

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
            bandVolumeCurve[i]      = yNorm * 1.2f;
            proc.bandVolumeCurve[i] = bandVolumeCurve[i];
        }
        else
        {
            proc.bandwidthCurve[i] = yNorm;
        }
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
    default: return &greenWhitePalette();
    }
}

//==============================================================================
VocodeAudioProcessorEditor::VocodeAudioProcessorEditor(VocodeAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , display(p, activePalette)
{
    // ── Rotary knobs ──────────────────────────────────────────────────────────
    setupKnob(bandsSlider,       bandsLabel,       "BANDS");
    setupKnob(attackSlider,      attackLabel,      "ATTACK");
    setupKnob(releaseSlider,     releaseLabel,     "RELEASE");
    setupKnob(orderSlider,       orderLabel,       "BAND ORDER");
    setupKnob(compressionSlider, compressionLabel, "COMP");
    setupKnob(dryWetSlider,      dryWetLabel,      "DRY / WET");
    setupKnob(gainSlider,        gainLabel,        "GAIN");

    bandsAttach       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "numBands",    bandsSlider);
    attackAttach      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "attack",      attackSlider);
    releaseAttach     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "release",     releaseSlider);
    orderAttach       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "bandOrder",   orderSlider);
    compressionAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "compression", compressionSlider);
    dryWetAttach      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "dryWet",      dryWetSlider);
    gainAttach        = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "outputGain",  gainSlider);

    // ── Frequency Start slider (horizontal, sits above COMP knob) ────────────
    freqStartSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    freqStartSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 16);
    freqStartSlider.setTextValueSuffix(" Hz");
    addAndMakeVisible(freqStartSlider);

    freqStartLabel.setText("FREQ START", juce::dontSendNotification);
    freqStartLabel.setJustificationType(juce::Justification::centredLeft);
    freqStartLabel.setFont(juce::Font(9.f, juce::Font::bold));
    addAndMakeVisible(freqStartLabel);

    freqStartAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "freqStart", freqStartSlider);

    // ── Frequency End slider (horizontal, sits above DRY/WET knob) ───────────
    freqEndSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    freqEndSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 16);
    freqEndSlider.setTextValueSuffix(" Hz");
    addAndMakeVisible(freqEndSlider);

    freqEndLabel.setText("FREQ END", juce::dontSendNotification);
    freqEndLabel.setJustificationType(juce::Justification::centredLeft);
    freqEndLabel.setFont(juce::Font(9.f, juce::Font::bold));
    addAndMakeVisible(freqEndLabel);

    freqEndAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "freqEnd", freqEndSlider);

    // ── Normalize slider (horizontal, above Release knob, col 2) ─────────────
    normalizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    normalizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 16);
    addAndMakeVisible(normalizeSlider);

    normalizeLabel.setText("NORMALIZE", juce::dontSendNotification);
    normalizeLabel.setJustificationType(juce::Justification::centredLeft);
    normalizeLabel.setFont(juce::Font(9.f, juce::Font::bold));
    addAndMakeVisible(normalizeLabel);

    normalizeAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "normalize", normalizeSlider);

    // ── Palette combo ─────────────────────────────────────────────────────────
    paletteCombo.addItem("Green Black", 1);
    paletteCombo.addItem("Green White", 2);
    paletteCombo.addItem("Brown White", 3);
    paletteCombo.addItem("Blue White",  4);

    const int savedId = juce::jlimit(1, 4, (int)audioProcessor.apvts.state.getProperty("paletteIndex", 2));
    paletteCombo.setSelectedId(savedId, juce::dontSendNotification);
    activePalette = getPaletteForIndex(savedId);

    paletteCombo.onChange = [this] {
        const int id = paletteCombo.getSelectedId();
        audioProcessor.apvts.state.setProperty("paletteIndex", id, nullptr);
        activePalette = getPaletteForIndex(id);
        applyPaletteToKnobs();
        repaint();
        display.repaint();
    };
    addAndMakeVisible(paletteCombo);

    // ── Col 0: Save Preset (row 1) + Load Preset (row 2) ─────────────────────
    savePresetBtn.onClick = [this] { savePreset(); };
    loadPresetBtn.onClick = [this] { loadPreset(); };
    addAndMakeVisible(savePresetBtn);
    addAndMakeVisible(loadPresetBtn);

    // ── Col 1: Randomize (row 1) ──────────────────────────────────────────────
    // 50% chance: standard random float per band.
    // 50% chance: each band independently snaps to 0 (off) or the default-on value.
    randomizeBtn.onClick = [this] {
        auto& rng = juce::Random::getSystemRandom();
        const int nb = juce::jlimit(1, VocodeAudioProcessor::kMaxBands,
            (int)*audioProcessor.apvts.getRawParameterValue("numBands"));

        for (int i = 0; i < nb; ++i)
        {
            // Bandwidth — default-on = 0.5
            if (rng.nextFloat() < 0.5f)
                audioProcessor.bandwidthCurve[i] = rng.nextFloat();
            else
                audioProcessor.bandwidthCurve[i] = rng.nextBool() ? 0.f : 0.5f;

            // Volume — default-on = 1.0
            if (rng.nextFloat() < 0.5f)
                audioProcessor.bandVolumeCurve[i] = rng.nextFloat() * 1.2f;
            else
                audioProcessor.bandVolumeCurve[i] = rng.nextBool() ? 0.f : 1.0f;
        }
        display.loadBandVolumeCurve();
        display.repaint();
    };
    addAndMakeVisible(randomizeBtn);

    // ── Col 1: Smooth (row 2) ─────────────────────────────────────────────────
    // For every interior band point, replace with the mean of its two neighbours.
    smoothBtn.onClick = [this] {
        const int nb = juce::jlimit(1, VocodeAudioProcessor::kMaxBands,
            (int)*audioProcessor.apvts.getRawParameterValue("numBands"));

        if (nb < 3) return; // need at least one interior point

        // Smooth bandwidth curve (snapshot neighbours first)
        {
            float tmp[VocodeAudioProcessor::kMaxBands];
            for (int i = 0; i < nb; ++i) tmp[i] = audioProcessor.bandwidthCurve[i];
            for (int i = 1; i < nb - 1; ++i)
                audioProcessor.bandwidthCurve[i] = (tmp[i - 1] + tmp[i + 1]) * 0.5f;
        }

        // Smooth band-volume curve (snapshot neighbours first)
        {
            float tmp[VocodeAudioProcessor::kMaxBands];
            for (int i = 0; i < nb; ++i) tmp[i] = audioProcessor.bandVolumeCurve[i];
            for (int i = 1; i < nb - 1; ++i)
                audioProcessor.bandVolumeCurve[i] = (tmp[i - 1] + tmp[i + 1]) * 0.5f;
        }

        display.loadBandVolumeCurve();
        display.repaint();
    };
    addAndMakeVisible(smoothBtn);

    // ── Col 2: Chromatic Lock easter egg (row 1) ──────────────────────────────
    chromaticLockBtn.setClickingTogglesState(true);
    addAndMakeVisible(chromaticLockBtn);
    chromaticAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "chromaticLock", chromaticLockBtn);

    // Register listener for hiding/showing the Chromatic button
    audioProcessor.apvts.addParameterListener("numBands", this);
    const float initBands = *audioProcessor.apvts.getRawParameterValue("numBands");
    chromaticLockBtn.setVisible(std::abs(initBands - 133.f) < 0.1f);

    addAndMakeVisible(display);
    applyPaletteToKnobs();

    // ── State-restore sync ────────────────────────────────────────────────────
    // When the DAW restores state while the editor is already open (e.g. preset
    // recall), the processor fires this so we can re-sync palette + display.
    {
        juce::Component::SafePointer<VocodeAudioProcessorEditor> safeThis(this);
        audioProcessor.onStateRestored = [safeThis]()
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                auto* ed = safeThis.getComponent();
                if (ed == nullptr) return;

                const int id = juce::jlimit(1, 4,
                    (int)ed->audioProcessor.apvts.state.getProperty("paletteIndex", 2));
                ed->activePalette = getPaletteForIndex(id);
                ed->paletteCombo.setSelectedId(id, juce::dontSendNotification);
                ed->applyPaletteToKnobs();
                ed->display.loadBandVolumeCurve();
                ed->repaint();
                ed->display.repaint();
            });
        };
    }

    setSize(980, 520);
    setResizable(true, true);
    setResizeLimits(700, 380, 1600, 1000);
}

VocodeAudioProcessorEditor::~VocodeAudioProcessorEditor()
{
    audioProcessor.apvts.removeParameterListener("numBands", this);
    audioProcessor.onStateRestored = nullptr;
}

void VocodeAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "numBands")
    {
        juce::MessageManager::callAsync([this, newValue] {
            chromaticLockBtn.setVisible(std::abs(newValue - 133.f) < 0.1f);
        });
    }
}

//==============================================================================
void VocodeAudioProcessorEditor::setupKnob(juce::Slider& s,
    juce::Label& l, const juce::String& text)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 16);
    addAndMakeVisible(s);

    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(10.f, juce::Font::bold));
    addAndMakeVisible(l);
}

//==============================================================================
void VocodeAudioProcessorEditor::applyPaletteToKnobs()
{
    const ColorSet& P = *activePalette;

    // ── Rotary knobs ──────────────────────────────────────────────────────────
    juce::Slider* sliders[] = {
        &bandsSlider, &attackSlider,      &releaseSlider,
        &orderSlider, &compressionSlider, &dryWetSlider,  &gainSlider };
    juce::Label* labels[] = {
        &bandsLabel,  &attackLabel,       &releaseLabel,
        &orderLabel,  &compressionLabel,  &dryWetLabel,   &gainLabel };

    constexpr int kN = 7;
    for (int i = 0; i < kN; ++i)
    {
        sliders[i]->setColour(juce::Slider::rotarySliderFillColourId,   P.accent);
        sliders[i]->setColour(juce::Slider::rotarySliderOutlineColourId, P.accentDark);
        sliders[i]->setColour(juce::Slider::thumbColourId,              P.accentBright);
        sliders[i]->setColour(juce::Slider::trackColourId,              P.accentDim);
        sliders[i]->setColour(juce::Slider::textBoxTextColourId,        P.textCol);
        sliders[i]->setColour(juce::Slider::textBoxBackgroundColourId,  P.panelBg);
        sliders[i]->setColour(juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
        sliders[i]->setColour(juce::Slider::textBoxHighlightColourId,   P.accentDark);
        sliders[i]->repaint();

        labels[i]->setColour(juce::Label::textColourId, P.textDim);
        labels[i]->repaint();
    }

    // ── Frequency horizontal sliders + Normalize ──────────────────────────────
    for (auto* sl : { &freqStartSlider, &freqEndSlider, &normalizeSlider })
    {
        sl->setColour(juce::Slider::backgroundColourId,       P.accentDark);
        sl->setColour(juce::Slider::trackColourId,            P.accentDim);
        sl->setColour(juce::Slider::thumbColourId,            P.accentBright);
        sl->setColour(juce::Slider::textBoxTextColourId,      P.textCol);
        sl->setColour(juce::Slider::textBoxBackgroundColourId, P.panelBg);
        sl->setColour(juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
        sl->repaint();
    }
    for (auto* lbl : { &freqStartLabel, &freqEndLabel, &normalizeLabel })
    {
        lbl->setColour(juce::Label::textColourId, P.textDim);
        lbl->repaint();
    }

    // ── ComboBox ──────────────────────────────────────────────────────────────
    paletteCombo.setColour(juce::ComboBox::backgroundColourId,                P.accentDark);
    paletteCombo.setColour(juce::ComboBox::textColourId,                      P.accentBright);
    paletteCombo.setColour(juce::ComboBox::outlineColourId,                   P.accentDim);
    paletteCombo.setColour(juce::ComboBox::arrowColourId,                     P.accentBright);
    paletteCombo.setColour(juce::ComboBox::buttonColourId,                    P.accentDark);
    paletteCombo.setColour(juce::PopupMenu::backgroundColourId,               P.panelBg);
    paletteCombo.setColour(juce::PopupMenu::textColourId,                     P.textCol);
    paletteCombo.setColour(juce::PopupMenu::highlightedBackgroundColourId,    P.accentDark);
    paletteCombo.setColour(juce::PopupMenu::highlightedTextColourId,          P.accentBright);
    paletteCombo.repaint();

    // ── All text buttons ──────────────────────────────────────────────────────
    for (auto* btn : { &savePresetBtn, &loadPresetBtn,
                       &randomizeBtn,  &smoothBtn,
                       &chromaticLockBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId,   P.accentDark);
        btn->setColour(juce::TextButton::buttonOnColourId, P.accent);
        btn->setColour(juce::TextButton::textColourOffId,  P.accentBright);
        btn->setColour(juce::TextButton::textColourOnId,   P.accentBright);
        btn->repaint();
    }
}

//==============================================================================
void VocodeAudioProcessorEditor::paint(juce::Graphics& g)
{
    const ColorSet& P = *activePalette;
    const int w = getWidth();
    const int h = getHeight();

    g.fillAll(P.background);

    // Panel gradient covers the full knob section (≈ 205 px tall)
    {
        juce::ColourGradient grad(P.panelBg, 0.f, 0.f,
            P.background, 0.f, 205.f, false);
        g.setGradientFill(grad);
        g.fillRect(0, 0, w, 170);
    }

    // Vertical dividers between knob columns
    constexpr int kNumKnobs = 7;
    const int knobAreaW = w / kNumKnobs;
    g.setColour(P.accentDark.withAlpha(0.40f));
    for (int i = 1; i < kNumKnobs; ++i)
        g.drawVerticalLine(i * knobAreaW, 2.f, 168.f);

    // Title
    g.setColour(P.accentBright);
    g.setFont(juce::Font(22.f, juce::Font::bold));
    g.drawText("VOCODE", 0, 5, w, 26, juce::Justification::centred, false);

    // Subtitle
    g.setColour(P.textDim);
    g.setFont(juce::Font(9.5f, juce::Font::plain));
    g.drawText("256 bands Vocoder", 0, 30, w, 14,
        juce::Justification::centred, false);

    // Separator line between knob section and display
    {
        juce::ColourGradient sep(P.accentDim.withAlpha(0.f), 0.f, 200.f,
            P.accentDim.withAlpha(0.7f), w * 0.5f, 200.f, true);
        g.setGradientFill(sep);
        g.fillRect(4, 168, w - 8, 2);
    }

    // Bottom status bar
    g.setColour(P.panelBg.withAlpha(0.50f));
    g.fillRect(0, h - 18, w, 18);
    g.setColour(P.textDim.withAlpha(0.55f));
    g.setFont(juce::Font(8.5f, juce::Font::plain));
    g.drawText(
        "BW = bandwidth | VOL = per-band output level",
        0, h - 17, w, 16, juce::Justification::centred, false);
}

//==============================================================================
void VocodeAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    constexpr int kComboW = 120, kComboH = 22;
    paletteCombo.setBounds(w - kComboW - 6, 5, kComboW, kComboH);

    constexpr int kNumKnobs = 7;
    const int knobAreaW = w / kNumKnobs;

    // ── Button / header rows ──────────────────────────────────────────────────
    constexpr int btnMargin = 6;
    constexpr int btnRowH = 19;
    constexpr int btnGap = 2;
    const int btnRow1Y = 4;
    const int btnRow2Y = btnRow1Y + btnRowH + btnGap;  // = 25

    // Col 0: Save (row 1) — Load (row 2)
    savePresetBtn.setBounds(0 * knobAreaW + btnMargin, btnRow1Y,
        knobAreaW - 2 * btnMargin, btnRowH);
    loadPresetBtn.setBounds(0 * knobAreaW + btnMargin, btnRow2Y,
        knobAreaW - 2 * btnMargin, btnRowH);

    // Col 1: Randomize (row 1) — Smooth (row 2)
    randomizeBtn.setBounds(1 * knobAreaW + btnMargin, btnRow1Y,
        knobAreaW - 2 * btnMargin, btnRowH);
    smoothBtn.setBounds(1 * knobAreaW + btnMargin, btnRow2Y,
        knobAreaW - 2 * btnMargin, btnRowH);

    // Col 2: Normalize — label in row 1, slider in row 2
    normalizeLabel.setBounds(2 * knobAreaW + 4, btnRow1Y,
        knobAreaW - 8, btnRowH);
    normalizeSlider.setBounds(2 * knobAreaW, btnRow2Y,
        knobAreaW, btnRowH);

    // Col 3: Chromatic Lock (easter egg — visible only @ 133 bands, moved here from col 2)
    chromaticLockBtn.setBounds(3 * knobAreaW + btnMargin, btnRow1Y,
        knobAreaW - 2 * btnMargin, btnRowH);

    // Col 4: Freq Start — label in row 1, slider in row 2
    freqStartLabel.setBounds(4 * knobAreaW + 4, btnRow1Y,
        knobAreaW - 8, btnRowH);
    freqStartSlider.setBounds(4 * knobAreaW, btnRow2Y,
        knobAreaW, btnRowH);

    // Col 5: Freq End — label in row 1, slider in row 2
    freqEndLabel.setBounds(5 * knobAreaW + 4, btnRow1Y,
        knobAreaW - 8, btnRowH);
    freqEndSlider.setBounds(5 * knobAreaW, btnRow2Y,
        knobAreaW, btnRowH);

    // ── Knob section — back to original dimensions ────────────────────────────
    const int controlY = btnRow2Y + btnRowH + 6;  // = 50
    const int controlH = 118;
    constexpr int labelH = 15;

    juce::Slider* sliders[] = { &bandsSlider, &attackSlider, &releaseSlider,
        &orderSlider, &compressionSlider, &dryWetSlider, &gainSlider };
    juce::Label* labels[] = { &bandsLabel,  &attackLabel,  &releaseLabel,
        &orderLabel,  &compressionLabel,  &dryWetLabel,  &gainLabel };

    for (int i = 0; i < kNumKnobs; ++i)
    {
        const int x = i * knobAreaW;
        labels[i]->setBounds(x, controlY, knobAreaW, labelH);
        sliders[i]->setBounds(x, controlY + labelH, knobAreaW, controlH - labelH);
    }

    // ── Vocoder display ───────────────────────────────────────────────────────
    const int displayTop = controlY + controlH + 3;  // = 171
    const int displayBottom = h - 19;
    display.setBounds(4, displayTop, w - 8, displayBottom - displayTop);
}

//==============================================================================
// Preset save / load
//==============================================================================
void VocodeAudioProcessorEditor::savePreset()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Vocode Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.vpreset");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result == juce::File{}) return;

            const auto f = result.withFileExtension("vpreset");

            auto state = audioProcessor.apvts.copyState();
            state.setProperty("paletteIndex", paletteCombo.getSelectedId(), nullptr);

            auto bwTree = state.getOrCreateChildWithName("BandwidthCurve", nullptr);
            for (int i = 0; i < VocodeAudioProcessor::kMaxBands; ++i)
                bwTree.setProperty("b" + juce::String(i),
                    audioProcessor.bandwidthCurve[i], nullptr);

            auto volTree = state.getOrCreateChildWithName("BandVolumeCurve", nullptr);
            for (int i = 0; i < VocodeAudioProcessor::kMaxBands; ++i)
                volTree.setProperty("v" + juce::String(i),
                    audioProcessor.bandVolumeCurve[i], nullptr);

            if (auto xml = std::unique_ptr<juce::XmlElement>(state.createXml()))
                f.replaceWithText(xml->toString());
        });
}

void VocodeAudioProcessorEditor::loadPreset()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Vocode Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.vpreset");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            const auto results = fc.getResults();
            if (results.isEmpty()) return;

            auto xml = juce::XmlDocument::parse(results[0]);
            if (!xml) return;

            auto state = juce::ValueTree::fromXml(*xml);
            if (!state.isValid()) return;

            if (auto curve = state.getChildWithName("BandwidthCurve"); curve.isValid())
                for (int i = 0; i < VocodeAudioProcessor::kMaxBands; ++i)
                    audioProcessor.bandwidthCurve[i] = (float)curve.getProperty(
                        "b" + juce::String(i), 0.5f);

            if (auto vol = state.getChildWithName("BandVolumeCurve"); vol.isValid())
                for (int i = 0; i < VocodeAudioProcessor::kMaxBands; ++i)
                    audioProcessor.bandVolumeCurve[i] = (float)vol.getProperty(
                        "v" + juce::String(i), 1.0f);

            const int paletteId = juce::jlimit(1, 4,
                (int)state.getProperty("paletteIndex", 2));
            paletteCombo.setSelectedId(paletteId, juce::sendNotification);

            audioProcessor.apvts.replaceState(state);

            display.loadBandVolumeCurve();
            display.repaint();
        });
}
