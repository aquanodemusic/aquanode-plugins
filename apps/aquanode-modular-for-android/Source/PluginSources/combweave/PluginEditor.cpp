#include "PluginEditor.h"
#include "PluginProcessor.h"

// ============================================================
//  Constructor
// ============================================================
CombWeaveAudioProcessorEditor::CombWeaveAudioProcessorEditor(CombWeaveAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    // ── Title ────────────────────────────────────────────────────────────
    titleLabel.setText("CombWeave", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(26.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("additive synth  //  by aquanode", juce::dontSendNotification);
    subtitleLabel.setFont(juce::Font(12.0f, juce::Font::italic));
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    addAndMakeVisible(subtitleLabel);

    // ── OSC section labels ───────────────────────────────────────────────
    auto styleOscLabel = [this](juce::Label& lbl, const juce::String& text, juce::Colour col)
        {
            lbl.setText(text, juce::dontSendNotification);
            lbl.setFont(juce::Font(13.0f, juce::Font::bold));
            lbl.setJustificationType(juce::Justification::centredLeft);
            lbl.setColour(juce::Label::textColourId, col);
            addAndMakeVisible(lbl);
        };
    styleOscLabel(osc1Label, "OSC 1", juce::Colour(0xff00f5d4));
    styleOscLabel(osc2Label, "OSC 2", juce::Colours::white.withAlpha(0.80f));

    // ════════════════════════════════════════════════════════════════════
    //  OSC 1 — knobs
    // ════════════════════════════════════════════════════════════════════
    setupKnob(attackSlider, attackLabel, "Attack (ms)");
    setupKnob(releaseSlider, releaseLabel, "Release (ms)");
    setupKnob(hpFreqSlider, hpFreqLabel, "HP Cutoff");
    setupKnob(volumeSlider, volumeLabel, "Volume (dB)");
    setupKnob(amountSlider, amountLabel, "Harmonics");
    amountSlider.setRange(0, 128, 1);
    setupKnob(spreadSlider, spreadLabel, "Spread (Hz)");
    setupKnob(rolloffSlider, rolloffLabel, "Rolloff");
    setupKnob(tuneSlider, tuneLabel, "Fine Tune");

    // OSC 1 — buttons
    setupBtn(bidirBtn, true);
    bidirBtn.onClick = [this]()
        {
            bidirBtn.setButtonText(bidirBtn.getToggleState() ? "Bidirectional" : "Onedirectional");
        };
    setupBtn(noteLockBtn, true);

    // OSC 1 — combos
    setupCombo(spreadModeBox, spreadModeLabel, "Spread Mode");
    spreadModeBox.addItem("Linear", 1);
    spreadModeBox.addItem("Exponential", 2);
    spreadModeBox.addItem("Harmonics", 3);
    spreadModeBox.onChange = [this]() { syncSpreadAttachment(spreadModeBox.getSelectedId() - 1); };

    setupCombo(freqModeBox, freqModeLabel, "Freq Mode");
    freqModeBox.addItem("Regular", 1);
    freqModeBox.addItem("Wrap", 2);
    freqModeBox.addItem("Mirror", 3);

    setupCombo(harmonicFilterBox, harmonicFilterLabel, "Harmonic Filter");
    harmonicFilterBox.addItem("All", 1);
    harmonicFilterBox.addItem("Evens Only", 2);
    harmonicFilterBox.addItem("Odds Only", 3);

    // OSC 1 — attachments
    amountAtt = std::make_unique<SliderAtt>(proc.apvts, "amount", amountSlider);
    hpFreqAtt = std::make_unique<SliderAtt>(proc.apvts, "hpFreq", hpFreqSlider);
    volumeAtt = std::make_unique<SliderAtt>(proc.apvts, "volume", volumeSlider);
    attackAtt = std::make_unique<SliderAtt>(proc.apvts, "attack", attackSlider);
    releaseAtt = std::make_unique<SliderAtt>(proc.apvts, "release", releaseSlider);
    rolloffAtt = std::make_unique<SliderAtt>(proc.apvts, "rolloff", rolloffSlider);
    tuneAtt = std::make_unique<SliderAtt>(proc.apvts, "tune", tuneSlider);
    bidirAtt = std::make_unique<ButtonAtt>(proc.apvts, "bidirectional", bidirBtn);
    noteLockAtt = std::make_unique<ButtonAtt>(proc.apvts, "noteLock", noteLockBtn);
    spreadModeAtt = std::make_unique<ComboAtt>(proc.apvts, "spreadMode", spreadModeBox);
    freqModeAtt = std::make_unique<ComboAtt>(proc.apvts, "freqMode", freqModeBox);
    harmonicFilterAtt = std::make_unique<ComboAtt>(proc.apvts, "harmonicFilter", harmonicFilterBox);

    // OSC 1 — initial sync
    {
        const int mode = (int)proc.apvts.getRawParameterValue("spreadMode")->load();
        syncSpreadAttachment(mode);

        const bool bidir = proc.apvts.getRawParameterValue("bidirectional")->load() > 0.5f;
        bidirBtn.setToggleState(bidir, juce::dontSendNotification);
        bidirBtn.setButtonText(bidir ? "Bidirectional" : "Onedirectional");
        lastBidirState = bidir;
    }

    // ════════════════════════════════════════════════════════════════════
    //  OSC 2 — knobs
    // ════════════════════════════════════════════════════════════════════
    setupKnob(attackSlider2, attackLabel2, "Attack (ms)");
    setupKnob(releaseSlider2, releaseLabel2, "Release (ms)");
    setupKnob(hpFreqSlider2, hpFreqLabel2, "HP Cutoff");
    setupKnob(volumeSlider2, volumeLabel2, "Volume (dB)");
    setupKnob(amountSlider2, amountLabel2, "Harmonics");
    amountSlider2.setRange(0, 128, 1);
    setupKnob(spreadSlider2, spreadLabel2, "Spread (Hz)");
    setupKnob(rolloffSlider2, rolloffLabel2, "Rolloff");
    setupKnob(tuneSlider2, tuneLabel2, "Fine Tune");

    // Make osc2 knobs slightly dimmer to distinguish from osc1
    auto dimOsc2Knob = [](juce::Slider& s) {
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::white.withAlpha(0.75f));
        };
    dimOsc2Knob(attackSlider2);  dimOsc2Knob(releaseSlider2); dimOsc2Knob(hpFreqSlider2);
    dimOsc2Knob(volumeSlider2);  dimOsc2Knob(amountSlider2);  dimOsc2Knob(spreadSlider2);
    dimOsc2Knob(rolloffSlider2); dimOsc2Knob(tuneSlider2);

    // OSC 2 — buttons
    setupBtn(bidirBtn2, true);
    bidirBtn2.onClick = [this]()
        {
            bidirBtn2.setButtonText(bidirBtn2.getToggleState() ? "Bidirectional" : "Onedirectional");
        };
    setupBtn(noteLockBtn2, true);

    // OSC 2 — combos
    setupCombo(spreadModeBox2, spreadModeLabel2, "Spread Mode");
    spreadModeBox2.addItem("Linear", 1);
    spreadModeBox2.addItem("Exponential", 2);
    spreadModeBox2.addItem("Harmonics", 3);
    spreadModeBox2.onChange = [this]() { syncSpreadAttachment2(spreadModeBox2.getSelectedId() - 1); };

    setupCombo(freqModeBox2, freqModeLabel2, "Freq Mode");
    freqModeBox2.addItem("Regular", 1);
    freqModeBox2.addItem("Wrap", 2);
    freqModeBox2.addItem("Mirror", 3);

    setupCombo(harmonicFilterBox2, harmonicFilterLabel2, "Harmonic Filter");
    harmonicFilterBox2.addItem("All", 1);
    harmonicFilterBox2.addItem("Evens Only", 2);
    harmonicFilterBox2.addItem("Odds Only", 3);

    // OSC 2 — attachments
    amountAtt2 = std::make_unique<SliderAtt>(proc.apvts, "amount2", amountSlider2);
    hpFreqAtt2 = std::make_unique<SliderAtt>(proc.apvts, "hpFreq2", hpFreqSlider2);
    volumeAtt2 = std::make_unique<SliderAtt>(proc.apvts, "volume2", volumeSlider2);
    attackAtt2 = std::make_unique<SliderAtt>(proc.apvts, "attack2", attackSlider2);
    releaseAtt2 = std::make_unique<SliderAtt>(proc.apvts, "release2", releaseSlider2);
    rolloffAtt2 = std::make_unique<SliderAtt>(proc.apvts, "rolloff2", rolloffSlider2);
    tuneAtt2 = std::make_unique<SliderAtt>(proc.apvts, "tune2", tuneSlider2);
    bidirAtt2 = std::make_unique<ButtonAtt>(proc.apvts, "bidirectional2", bidirBtn2);
    noteLockAtt2 = std::make_unique<ButtonAtt>(proc.apvts, "noteLock2", noteLockBtn2);
    spreadModeAtt2 = std::make_unique<ComboAtt>(proc.apvts, "spreadMode2", spreadModeBox2);
    freqModeAtt2 = std::make_unique<ComboAtt>(proc.apvts, "freqMode2", freqModeBox2);
    harmonicFilterAtt2 = std::make_unique<ComboAtt>(proc.apvts, "harmonicFilter2", harmonicFilterBox2);

    // OSC 2 — initial sync
    {
        const int mode = (int)proc.apvts.getRawParameterValue("spreadMode2")->load();
        syncSpreadAttachment2(mode);

        const bool bidir = proc.apvts.getRawParameterValue("bidirectional2")->load() > 0.5f;
        bidirBtn2.setToggleState(bidir, juce::dontSendNotification);
        bidirBtn2.setButtonText(bidir ? "Bidirectional" : "Onedirectional");
        lastBidirState2 = bidir;
    }

    // ── Window size ──────────────────────────────────────────────────────
    // 2*kPad (reduced margins) + title + pad + display + pad
    // + (osc label + 2 rows + pad) * 2 oscillators + final pad
    setSize(700,
        2 * kPad
        + kTitleH + kPad
        + kDispH + kPad
        + kOscLabelH + kRowH * 2 + kPad   // osc1 section
        + kPad
        + kOscLabelH + kRowH * 2 + kPad   // osc2 section
        + kPad);

    startTimerHz(30);
}

CombWeaveAudioProcessorEditor::~CombWeaveAudioProcessorEditor()
{
    stopTimer();
}

// ============================================================
//  Helpers
// ============================================================
void CombWeaveAudioProcessorEditor::setupKnob(juce::Slider& s,
    juce::Label& l,
    const juce::String& name)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00f5d4));
    s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::rotarySliderOutlineColourId,
        juce::Colours::white.withAlpha(0.12f));
    addAndMakeVisible(s);

    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(12.5f, juce::Font::plain));
    l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    addAndMakeVisible(l);
}

void CombWeaveAudioProcessorEditor::setupBtn(juce::TextButton& b, bool toggleable)
{
    b.setClickingTogglesState(toggleable);
    b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a1a2e));
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00f5d4));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(b);
}

void CombWeaveAudioProcessorEditor::setupCombo(juce::ComboBox& b,
    juce::Label& l,
    const juce::String& name)
{
    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(13.0f));
    l.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(l);

    b.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff00aa77));
    b.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    b.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    b.setColour(juce::ComboBox::arrowColourId, juce::Colours::white);
    addAndMakeVisible(b);
}

// ── Spread attachment sync ─────────────────────────────────────────────
void CombWeaveAudioProcessorEditor::syncSpreadAttachment(int mode)
{
    spreadHzAtt.reset();
    spreadRatioAtt.reset();

    if (mode == 1)
    {
        spreadRatioAtt = std::make_unique<SliderAtt>(proc.apvts, "spreadRatio", spreadSlider);
        spreadLabel.setText("Spread (Ratio)", juce::dontSendNotification);
        spreadSlider.setEnabled(true);
    }
    else if (mode == 2)
    {
        spreadLabel.setText("Spread (N/A)", juce::dontSendNotification);
        spreadSlider.setEnabled(false);
    }
    else
    {
        spreadHzAtt = std::make_unique<SliderAtt>(proc.apvts, "spreadHz", spreadSlider);
        spreadLabel.setText("Spread (Hz)", juce::dontSendNotification);
        spreadSlider.setEnabled(true);
    }
    lastSpreadMode = mode;
}

void CombWeaveAudioProcessorEditor::syncSpreadAttachment2(int mode)
{
    spreadHzAtt2.reset();
    spreadRatioAtt2.reset();

    if (mode == 1)
    {
        spreadRatioAtt2 = std::make_unique<SliderAtt>(proc.apvts, "spreadRatio2", spreadSlider2);
        spreadLabel2.setText("Spread (Ratio)", juce::dontSendNotification);
        spreadSlider2.setEnabled(true);
    }
    else if (mode == 2)
    {
        spreadLabel2.setText("Spread (N/A)", juce::dontSendNotification);
        spreadSlider2.setEnabled(false);
    }
    else
    {
        spreadHzAtt2 = std::make_unique<SliderAtt>(proc.apvts, "spreadHz2", spreadSlider2);
        spreadLabel2.setText("Spread (Hz)", juce::dontSendNotification);
        spreadSlider2.setEnabled(true);
    }
    lastSpreadMode2 = mode;
}

// ============================================================
//  Display (dual harmonic spectrum visualiser)
// ============================================================
void CombWeaveAudioProcessorEditor::drawDisplay(juce::Graphics& g,
    juce::Rectangle<int> area)
{
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;

    // ── Background ───────────────────────────────────────────────────────
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    // Log-frequency → pixel X
    auto freqToX = [&](float f) -> float
        {
            const float norm = std::log10(f / minFreq) / std::log10(maxFreq / minFreq);
            return juce::jmap(norm, 0.0f, 1.0f,
                (float)area.getX(), (float)area.getRight());
        };

    // ── Decade grid ──────────────────────────────────────────────────────
    const struct { float freq; const char* label; } grid[] =
    {
        {  100.0f, "100" },
        { 1000.0f, "1k"  },
        {10000.0f, "10k" }
    };

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    for (auto& e : grid)
        g.drawVerticalLine(juce::roundToInt(freqToX(e.freq)),
            (float)area.getY(), (float)area.getBottom());

    g.setFont(10.0f);
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    for (auto& e : grid)
        g.drawText(e.label,
            juce::roundToInt(freqToX(e.freq)) + 3, area.getBottom() - 15,
            32, 13, juce::Justification::left);

    // ── Boundary markers at 20 Hz / 20 kHz ──────────────────────────────
    g.setColour(juce::Colour(0xff00f5d4).withAlpha(0.18f));
    g.drawVerticalLine(juce::roundToInt(freqToX(20.0f)),
        (float)area.getY(), (float)area.getBottom());
    g.drawVerticalLine(juce::roundToInt(freqToX(20000.0f)),
        (float)area.getY(), (float)area.getBottom());

    const float fundamental = proc.displayFundamental.load();
    if (fundamental <= 0.0f) return;

    const float areaH = (float)area.getHeight();
    const float areaBot = (float)area.getBottom();

    // ── Helper: draw one oscillator's bars ───────────────────────────────
    auto drawOscBars = [&](int oscIndex,
        juce::Colour fundamentalCol,
        juce::Colour harmonicCol,
        float glowAlpha)
        {
            const auto  freqs = proc.computeHarmonicFreqsVec(fundamental, oscIndex);
            const int   nFreqs = (int)freqs.size();
            if (nFreqs == 0) return;

            const float rolloff = proc.apvts.getRawParameterValue(
                oscIndex == 0 ? "rolloff" : "rolloff2")->load();

            // Pre-compute normalised gains (same formula as processor)
            float energy = 0.0f;
            std::array<float, kMaxOscs> gains{};
            for (int i = 0; i < nFreqs; ++i)
            {
                gains[i] = std::pow(1.0f - rolloff * 0.95f, (float)i);
                energy += gains[i] * gains[i];
            }
            const float norm = (energy > 1e-9f) ? 1.0f / std::sqrt(energy) : 0.0f;

            for (int i = 0; i < nFreqs; ++i)
            {
                const float f = freqs[i];
                if (f < minFreq || f > maxFreq) continue;

                const float gainNorm = gains[i] * norm;
                const float lineH = juce::jlimit(2.0f, areaH, gainNorm * areaH * 2.5f);

                const bool  isFund = (i == 0);
                const float alpha = isFund ? 1.0f : 0.70f;

                g.setColour((isFund ? fundamentalCol : harmonicCol).withAlpha(alpha));

                const int px = juce::roundToInt(freqToX(f));
                g.drawVerticalLine(px, areaBot - lineH, areaBot);

                if (isFund && glowAlpha > 0.0f)
                {
                    g.setColour(fundamentalCol.withAlpha(glowAlpha));
                    g.fillRect((float)px - 1.5f, areaBot - lineH, 3.0f, lineH);
                }
            }

            return;  // nFreqs used in caller for info label
        };

    // Draw osc2 bars first (behind osc1) — white
    drawOscBars(1,
        juce::Colours::white,
        juce::Colours::white.withAlpha(0.65f),
        0.10f);

    // Draw osc1 bars on top — teal
    drawOscBars(0,
        juce::Colour(0xff00f5d4),
        juce::Colour(0xff00c4aa),
        0.18f);

    // ── Info label ───────────────────────────────────────────────────────
    const int n1 = (int)proc.computeHarmonicFreqsVec(fundamental, 0).size();
    const int n2 = (int)proc.computeHarmonicFreqsVec(fundamental, 1).size();
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.drawText(juce::String(fundamental, 1) + " Hz  //  "
        + juce::String(n1) + " + " + juce::String(n2) + " partials",
        area.getX() + 8, area.getY() + 6,
        280, 16, juce::Justification::left);
}

// ============================================================
//  paint
// ============================================================
void CombWeaveAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bg(juce::Colour(0xff00f5d4), 0, 0,
        juce::Colour(0xff0077b6), 0, (float)getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    auto area = getLocalBounds().reduced(kPad);
    area.removeFromTop(kTitleH + kPad);
    const auto dispArea = area.removeFromTop(kDispH);
    drawDisplay(g, dispArea);

    // Separator below display
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillRect(dispArea.getX(), dispArea.getBottom() + kPad / 2,
        dispArea.getWidth(), 1);

    // Separator between osc1 and osc2 sections
    area.removeFromTop(kPad);                      // gap after display
    area.removeFromTop(kOscLabelH);                // osc1 label
    area.removeFromTop(kRowH * 2 + kPad);          // osc1 rows
    area.removeFromTop(kPad / 2);                  // half of gap before separator

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.fillRect(getLocalBounds().reduced(kPad).getX(),
        area.getY(),
        getLocalBounds().reduced(kPad).getWidth(), 1);
}

// ============================================================
//  resized
// ============================================================
void CombWeaveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(kPad);

    // ── Title ────────────────────────────────────────────────────────────
    {
        auto titleArea = area.removeFromTop(kTitleH);
        titleLabel.setBounds(titleArea.removeFromTop(26));
        subtitleLabel.setBounds(titleArea);
    }
    area.removeFromTop(kPad);

    // ── Display (consume space, actual drawing in paint()) ───────────────
    area.removeFromTop(kDispH);
    area.removeFromTop(kPad);

    // ── Helper lambdas ───────────────────────────────────────────────────
    auto placeKnob = [](juce::Rectangle<int> cell,
        juce::Slider& s, juce::Label& l)
        {
            l.setBounds(cell.removeFromTop(16));
            s.setBounds(cell);
        };

    // layoutOscSection: places 8 knobs (2×4) + sidebar (5 slots) from `oscArea`
    auto layoutOscSection = [&](juce::Rectangle<int> oscArea,
        // Row 1 knobs
        juce::Slider& k1a, juce::Label& l1a,
        juce::Slider& k1b, juce::Label& l1b,
        juce::Slider& k1c, juce::Label& l1c,
        juce::Slider& k1d, juce::Label& l1d,
        // Row 2 knobs
        juce::Slider& k2a, juce::Label& l2a,
        juce::Slider& k2b, juce::Label& l2b,
        juce::Slider& k2c, juce::Label& l2c,
        juce::Slider& k2d, juce::Label& l2d,
        // Sidebar controls
        juce::TextButton& bidir,
        juce::TextButton& noteLock,
        juce::ComboBox& spreadMode, juce::Label& spreadModeLbl,
        juce::ComboBox& freqMode, juce::Label& freqModeLbl,
        juce::ComboBox& harmFilt, juce::Label& harmFiltLbl)
        {
            // Sidebar (right 140 px)
            auto sideArea = oscArea.removeFromRight(140);
            oscArea.removeFromRight(kPad);   // gap

            // Knob columns
            const int colW = oscArea.getWidth() / 4;

            // Row 1
            auto row1 = oscArea.removeFromTop(kRowH);
            placeKnob(row1.removeFromLeft(colW), k1a, l1a);
            placeKnob(row1.removeFromLeft(colW), k1b, l1b);
            placeKnob(row1.removeFromLeft(colW), k1c, l1c);
            placeKnob(row1, k1d, l1d);

            oscArea.removeFromTop(kPad);

            // Row 2
            auto row2 = oscArea;
            placeKnob(row2.removeFromLeft(colW), k2a, l2a);
            placeKnob(row2.removeFromLeft(colW), k2b, l2b);
            placeKnob(row2.removeFromLeft(colW), k2c, l2c);
            placeKnob(row2, k2d, l2d);

            // Sidebar: 5 equal slots
            const int totalSideH = kRowH * 2 + kPad;  // spans both rows + gap
            const int slotH = totalSideH / 5;

            bidir.setBounds(sideArea.removeFromTop(slotH).reduced(0, 5));
            noteLock.setBounds(sideArea.removeFromTop(slotH).reduced(0, 5));

            {
                auto cell = sideArea.removeFromTop(slotH);
                spreadModeLbl.setBounds(cell.removeFromTop(16));
                spreadMode.setBounds(cell.withSizeKeepingCentre(cell.getWidth() - 8, 24));
            }
            {
                auto cell = sideArea.removeFromTop(slotH);
                freqModeLbl.setBounds(cell.removeFromTop(16));
                freqMode.setBounds(cell.withSizeKeepingCentre(cell.getWidth() - 8, 24));
            }
            {
                auto cell = sideArea;   // remaining slot
                harmFiltLbl.setBounds(cell.removeFromTop(16));
                harmFilt.setBounds(cell.withSizeKeepingCentre(cell.getWidth() - 8, 24));
            }
        };

    // ── OSC 1 ────────────────────────────────────────────────────────────
    osc1Label.setBounds(area.removeFromTop(kOscLabelH));
    {
        auto osc1Area = area.removeFromTop(kRowH * 2 + kPad);
        layoutOscSection(osc1Area,
            attackSlider, attackLabel,
            releaseSlider, releaseLabel,
            hpFreqSlider, hpFreqLabel,
            volumeSlider, volumeLabel,
            amountSlider, amountLabel,
            spreadSlider, spreadLabel,
            rolloffSlider, rolloffLabel,
            tuneSlider, tuneLabel,
            bidirBtn, noteLockBtn,
            spreadModeBox, spreadModeLabel,
            freqModeBox, freqModeLabel,
            harmonicFilterBox, harmonicFilterLabel);
    }

    area.removeFromTop(kPad);

    // ── OSC 2 ────────────────────────────────────────────────────────────
    osc2Label.setBounds(area.removeFromTop(kOscLabelH));
    {
        auto osc2Area = area.removeFromTop(kRowH * 2 + kPad);
        layoutOscSection(osc2Area,
            attackSlider2, attackLabel2,
            releaseSlider2, releaseLabel2,
            hpFreqSlider2, hpFreqLabel2,
            volumeSlider2, volumeLabel2,
            amountSlider2, amountLabel2,
            spreadSlider2, spreadLabel2,
            rolloffSlider2, rolloffLabel2,
            tuneSlider2, tuneLabel2,
            bidirBtn2, noteLockBtn2,
            spreadModeBox2, spreadModeLabel2,
            freqModeBox2, freqModeLabel2,
            harmonicFilterBox2, harmonicFilterLabel2);
    }
}

// ============================================================
//  timerCallback  — syncs state that can change via host automation
// ============================================================
void CombWeaveAudioProcessorEditor::timerCallback()
{
    // OSC 1: spread mode
    const int mode1 = (int)proc.apvts.getRawParameterValue("spreadMode")->load();
    if (mode1 != lastSpreadMode)
        syncSpreadAttachment(mode1);

    // OSC 1: bidir button text
    const bool bidir1 = proc.apvts.getRawParameterValue("bidirectional")->load() > 0.5f;
    if (bidir1 != lastBidirState)
    {
        bidirBtn.setToggleState(bidir1, juce::dontSendNotification);
        bidirBtn.setButtonText(bidir1 ? "Bidirectional" : "Onedirectional");
        lastBidirState = bidir1;
    }

    // OSC 2: spread mode
    const int mode2 = (int)proc.apvts.getRawParameterValue("spreadMode2")->load();
    if (mode2 != lastSpreadMode2)
        syncSpreadAttachment2(mode2);

    // OSC 2: bidir button text
    const bool bidir2 = proc.apvts.getRawParameterValue("bidirectional2")->load() > 0.5f;
    if (bidir2 != lastBidirState2)
    {
        bidirBtn2.setToggleState(bidir2, juce::dontSendNotification);
        bidirBtn2.setButtonText(bidir2 ? "Bidirectional" : "Onedirectional");
        lastBidirState2 = bidir2;
    }

    repaint();
}