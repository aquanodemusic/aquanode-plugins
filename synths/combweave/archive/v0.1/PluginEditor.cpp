#include "PluginEditor.h"
#include "PluginProcessor.h"

// ============================================================
//  Constructor / Destructor
// ============================================================
CombWeaveAudioProcessorEditor::CombWeaveAudioProcessorEditor(
    CombWeaveAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    // ── Title ────────────────────────────────────────────────────────
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

    // ── Row 1 ────────────────────────────────────────────────────────
    setupKnob(amountSlider, amountLabel, "Harmonics");
    amountSlider.setRange(0, 128, 1);

    setupKnob(hpFreqSlider, hpFreqLabel, "HP Cutoff");

    setupBtn(spreadModeBtn);
    spreadModeBtn.setButtonText("Linear");
    spreadModeBtn.onClick = [this]()
        {
            const bool isExp = spreadModeBtn.getToggleState();
            spreadModeBtn.setButtonText(isExp ? "Exponential" : "Linear");
            spreadLabel.setText(isExp ? "Spread (Ratio)" : "Spread (Hz)",
                juce::dontSendNotification);
            syncSpreadAttachment(isExp);
        };

    amountAtt = std::make_unique<SliderAtt>(proc.apvts, "amount", amountSlider);
    hpFreqAtt = std::make_unique<SliderAtt>(proc.apvts, "hpFreq", hpFreqSlider);
    spreadModeAtt = std::make_unique<ButtonAtt>(proc.apvts, "spreadMode", spreadModeBtn);

    // ── Row 2 ────────────────────────────────────────────────────────
    setupKnob(spreadSlider, spreadLabel, "Spread (Hz)");
    setupKnob(volumeSlider, volumeLabel, "Volume (dB)");
    setupBtn(bidirBtn);
    bidirBtn.setButtonText("Bidirectional");

    volumeAtt = std::make_unique<SliderAtt>(proc.apvts, "volume", volumeSlider);
    bidirAtt = std::make_unique<ButtonAtt>(proc.apvts, "bidirectional", bidirBtn);

    // Sync spread attachment to the parameter's stored state
    {
        const bool isExp = proc.apvts.getRawParameterValue("spreadMode")->load() > 0.5f;
        syncSpreadAttachment(isExp);
        if (isExp)
        {
            spreadModeBtn.setToggleState(true, juce::dontSendNotification);
            spreadModeBtn.setButtonText("Exponential");
            spreadLabel.setText("Spread (Ratio)", juce::dontSendNotification);
        }
    }

    // ── Row 3 ────────────────────────────────────────────────────────
    freqModeLabel.setText("Freq Mode", juce::dontSendNotification);
    freqModeLabel.setJustificationType(juce::Justification::centred);
    freqModeLabel.setFont(juce::Font(13.0f));
    freqModeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(freqModeLabel);

    freqModeBox.addItem("Regular", 1);
    freqModeBox.addItem("Wrap", 2);
    freqModeBox.addItem("Mirror", 3);
    freqModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff00aa77));
    freqModeBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    freqModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    freqModeBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::white);
    addAndMakeVisible(freqModeBox);
    freqModeAtt = std::make_unique<ComboAtt>(proc.apvts, "freqMode", freqModeBox);

    setupKnob(attackSlider, attackLabel, "Attack (ms)");
    setupKnob(releaseSlider, releaseLabel, "Release (ms)");
    setupKnob(rolloffSlider, rolloffLabel, "Rolloff");
    setupKnob(tuneSlider, tuneLabel, "Fine Tune");

    attackAtt = std::make_unique<SliderAtt>(proc.apvts, "attack", attackSlider);
    releaseAtt = std::make_unique<SliderAtt>(proc.apvts, "release", releaseSlider);
    rolloffAtt = std::make_unique<SliderAtt>(proc.apvts, "rolloff", rolloffSlider);
    tuneAtt = std::make_unique<SliderAtt>(proc.apvts, "tune", tuneSlider);

    // ── Window size ──────────────────────────────────────────────────
    // kPad + kTitleH + kPad + kDispH + kPad + 3*(kRowH + kPad)
    setSize(660, kPad + kTitleH + kPad + kDispH + kPad
        + kRowH + kPad + kRowH + kPad + kRowH + kPad);

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

void CombWeaveAudioProcessorEditor::syncSpreadAttachment(bool isExp)
{
    spreadHzAtt.reset();
    spreadRatioAtt.reset();

    if (isExp)
        spreadRatioAtt = std::make_unique<SliderAtt>(proc.apvts, "spreadRatio", spreadSlider);
    else
        spreadHzAtt = std::make_unique<SliderAtt>(proc.apvts, "spreadHz", spreadSlider);

    lastSpreadState = isExp;
}

// ============================================================
//  Display (harmonic spectrum visualiser)
// ============================================================
void CombWeaveAudioProcessorEditor::drawDisplay(juce::Graphics& g,
    juce::Rectangle<int> area)
{
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;

    // ── Background ───────────────────────────────────────────────────
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    // Log-frequency → pixel X
    auto freqToX = [&](float f) -> float
        {
            const float norm = std::log10(f / minFreq) / std::log10(maxFreq / minFreq);
            return juce::jmap(norm, 0.0f, 1.0f,
                (float)area.getX(), (float)area.getRight());
        };

    // ── Decade grid ──────────────────────────────────────────────────
    const struct { float freq; const char* label; } grid[] =
    {
        {  100.0f, "100" },
        { 1000.0f, "1k"  },
        {10000.0f, "10k" }
    };

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    for (auto& entry : grid)
    {
        const int x = juce::roundToInt(freqToX(entry.freq));
        g.drawVerticalLine(x, (float)area.getY(), (float)area.getBottom());
    }

    g.setFont(10.0f);
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    for (auto& entry : grid)
    {
        const int x = juce::roundToInt(freqToX(entry.freq));
        g.drawText(entry.label, x + 3, area.getBottom() - 15, 32, 13,
            juce::Justification::left);
    }

    // Boundary markers at 20 Hz / 20 kHz
    g.setColour(juce::Colour(0xff00f5d4).withAlpha(0.18f));
    g.drawVerticalLine(juce::roundToInt(freqToX(20.0f)),
        (float)area.getY(), (float)area.getBottom());
    g.drawVerticalLine(juce::roundToInt(freqToX(20000.0f)),
        (float)area.getY(), (float)area.getBottom());

    // ── Harmonic lines ───────────────────────────────────────────────
    const float fundamental = proc.displayFundamental.load();
    if (fundamental <= 0.0f) return;

    const auto freqs = proc.computeHarmonicFreqsVec(fundamental);
    const int  nFreqs = (int)freqs.size();
    if (nFreqs == 0) return;

    // Read rolloff so bar heights match audible gain weighting
    const float rolloff = proc.apvts.getRawParameterValue("rolloff")->load();
    const float areaH = (float)area.getHeight();
    const float areaBot = (float)area.getBottom();
    const float areaTop = (float)area.getY();

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

        const float gainNorm = gains[i] * norm;          // 0..1 (roughly)
        // Scale height – clamp so it stays within display
        const float lineH = juce::jlimit(2.0f, areaH, gainNorm * areaH * 2.5f);

        const bool isFundamental = (i == 0);
        const float alpha = isFundamental ? 1.0f : 0.70f;

        g.setColour(isFundamental
            ? juce::Colour(0xff00f5d4).withAlpha(alpha)
            : juce::Colour(0xff00c4aa).withAlpha(alpha));

        const int px = juce::roundToInt(freqToX(f));
        g.drawVerticalLine(px, areaBot - lineH, areaBot);

        // Glow: wider, translucent bar ±1 px around fundamental
        if (isFundamental)
        {
            g.setColour(juce::Colour(0xff00f5d4).withAlpha(0.18f));
            g.fillRect((float)px - 1.5f, areaBot - lineH, 3.0f, lineH);
        }
    }

    // ── Info label ───────────────────────────────────────────────────
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.drawText(juce::String(fundamental, 1) + " Hz  //  "
        + juce::String(nFreqs) + " partials",
        area.getX() + 8, area.getY() + 6,
        220, 16, juce::Justification::left);
}

// ============================================================
//  paint
// ============================================================
void CombWeaveAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Dark navy gradient background
    juce::ColourGradient bg(juce::Colour(0xff0d0d1f), 0.0f, 0.0f,
        juce::Colour(0xff091428), 0.0f, (float)getHeight(),
        false);
    g.setGradientFill(bg);
    g.fillAll();

    // Subtle horizontal separator below display
    auto area = getLocalBounds().reduced(kPad);
    area.removeFromTop(kTitleH + kPad);
    const auto dispArea = area.removeFromTop(kDispH);
    drawDisplay(g, dispArea);

    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillRect(dispArea.getX(), dispArea.getBottom() + kPad / 2,
        dispArea.getWidth(), 1);
}

// ============================================================
//  resized
// ============================================================
void CombWeaveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(kPad);

    // ── Title ────────────────────────────────────────────────────────
    {
        auto titleArea = area.removeFromTop(kTitleH);
        titleLabel.setBounds(titleArea.removeFromTop(26));
        subtitleLabel.setBounds(titleArea);
    }
    area.removeFromTop(kPad);

    // ── Display (painted; skip the rect) ────────────────────────────
    area.removeFromTop(kDispH);
    area.removeFromTop(kPad);

    // Convenience: split a row cell into label + knob/control
    auto placeKnob = [](juce::Rectangle<int> cell,
        juce::Slider& s, juce::Label& l)
        {
            l.setBounds(cell.removeFromTop(16));
            s.setBounds(cell);
        };

    // ── Row 1: Harmonics | HP Cutoff | Spread Mode ──────────────────
    {
        auto row = area.removeFromTop(kRowH);
        const int w = row.getWidth() / 3;

        placeKnob(row.removeFromLeft(w), amountSlider, amountLabel);
        placeKnob(row.removeFromLeft(w), hpFreqSlider, hpFreqLabel);

        // Spread mode button, centred vertically in remaining cell
        spreadModeBtn.setBounds(row.withSizeKeepingCentre(
            juce::jmin(row.getWidth() - 10, 130), 30));
    }
    area.removeFromTop(kPad);

    // ── Row 2: Spread | Volume | Bidirectional ───────────────────────
    {
        auto row = area.removeFromTop(kRowH);
        const int w = row.getWidth() / 3;

        placeKnob(row.removeFromLeft(w), spreadSlider, spreadLabel);
        placeKnob(row.removeFromLeft(w), volumeSlider, volumeLabel);

        bidirBtn.setBounds(row.withSizeKeepingCentre(
            juce::jmin(row.getWidth() - 10, 130), 30));
    }
    area.removeFromTop(kPad);

    // ── Row 3: FreqMode | Attack | Release | Rolloff | Tune ──────────
    {
        auto row = area.removeFromTop(kRowH);
        const int w = row.getWidth() / 5;

        // FreqMode: label above, combo below
        {
            auto cell = row.removeFromLeft(w);
            freqModeLabel.setBounds(cell.removeFromTop(16));
            freqModeBox.setBounds(
                cell.withSizeKeepingCentre(cell.getWidth() - 8, 26)
                .withY(cell.getY() + (cell.getHeight() - 26) / 2));
        }

        placeKnob(row.removeFromLeft(w), attackSlider, attackLabel);
        placeKnob(row.removeFromLeft(w), releaseSlider, releaseLabel);
        placeKnob(row.removeFromLeft(w), rolloffSlider, rolloffLabel);
        placeKnob(row, tuneSlider, tuneLabel);
    }
}

// ============================================================
//  timerCallback
// ============================================================
void CombWeaveAudioProcessorEditor::timerCallback()
{
    // Sync spread attachment if the parameter was changed externally
    // (e.g. host automation or preset load)
    const bool isExp = proc.apvts.getRawParameterValue("spreadMode")->load() > 0.5f;
    if (isExp != lastSpreadState)
    {
        spreadModeBtn.setToggleState(isExp, juce::dontSendNotification);
        spreadModeBtn.setButtonText(isExp ? "Exponential" : "Linear");
        spreadLabel.setText(isExp ? "Spread (Ratio)" : "Spread (Hz)",
            juce::dontSendNotification);
        syncSpreadAttachment(isExp);
    }

    repaint();
}