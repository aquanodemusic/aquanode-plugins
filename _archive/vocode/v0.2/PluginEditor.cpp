#include "PluginEditor.h"

// ============================================================================
// Colour palette
// ============================================================================
namespace VocodeColors
{
    const juce::Colour kBg{ 0xff07070e };
    const juce::Colour kPanel{ 0xff0c0c16 };
    const juce::Colour kGrid{ 0xff141e2a };
    const juce::Colour kGridBrt{ 0xff1c2d3e };
    const juce::Colour kGridTxt{ 0xff3a5060 };
    const juce::Colour kAccent{ 0xff0099cc };
    const juce::Colour kDivider{ 0xff131d28 };

    const juce::Colour kCarrier{ 0xff33ee88 };   // green  — main input
    const juce::Colour kMod{ 0xffdd44ff };   // violet — sidechain modulator
    const juce::Colour kOut{ 0xff00ddff };   // cyan   — morphed output

    // Analog mode badge colours
    const juce::Colour kAnalogOn{ 0xffff8800 };   // warm amber when active
    const juce::Colour kAnalogOff{ 0xff1a2d3e };   // dark when inactive
}

// Floor for dB axis
static constexpr float kFloorDB = -90.0f;

// ============================================================================
// Constructor
// ============================================================================
VocodeAudioProcessorEditor::VocodeAudioProcessorEditor(VocodeAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    // Window is wider and taller to accommodate two control rows
    setSize(700, 490);

    // ---- FFT size combo (row 1 left) ----
    fftSizeCombo.addItemList({ "32","64","128","256","512","1024","2048","4096","8192" }, 1);
    fftSizeAtt = std::make_unique<ComboAtt>(proc.apvts, "fftSize", fftSizeCombo);
    addAndMakeVisible(fftSizeCombo);

    fftLbl.setText("FFT SIZE", juce::dontSendNotification);
    fftLbl.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(fftLbl);

    // ---- Num bands combo (row 1, centre-left) ----
    for (int i = 0; i < VocodeAudioProcessor::kNumBandChoices; ++i)
        numBandsCombo.addItem(juce::String(VocodeAudioProcessor::kBandCounts[i]), i + 1);
    numBandsAtt = std::make_unique<ComboAtt>(proc.apvts, "numBands", numBandsCombo);
    addAndMakeVisible(numBandsCombo);

    bandsLbl.setText("BANDS", juce::dontSendNotification);
    bandsLbl.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(bandsLbl);

    // ---- Analog mode toggle (row 1, centre-right) ----
    analogModeBtn.setClickingTogglesState(true);
    analogModeBtn.setButtonText("ANALOG");
    addAndMakeVisible(analogModeBtn);
    analogModeAtt = std::make_unique<BtnAtt>(proc.apvts, "analogMode", analogModeBtn);

    // ---- Rotary knobs (row 2) ----
    auto initKnob = [&](juce::Slider& s, juce::Label& l, const juce::String& txt)
        {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
            addAndMakeVisible(s);
            l.setText(txt, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(l);
        };

    initKnob(morphKnob, morphLbl, "MORPH");
    initKnob(clarityKnob, clarityLbl, "CLARITY");
    initKnob(smoothingKnob, smoothingLbl, "SMOOTH");
    initKnob(gateKnob, gateLbl, "GATE");
    initKnob(formantKnob, formantLbl, "FORMANT");

    morphAtt = std::make_unique<SliderAtt>(proc.apvts, "morph", morphKnob);
    clarityAtt = std::make_unique<SliderAtt>(proc.apvts, "clarity", clarityKnob);
    smoothingAtt = std::make_unique<SliderAtt>(proc.apvts, "smoothing", smoothingKnob);
    gateAtt = std::make_unique<SliderAtt>(proc.apvts, "gate", gateKnob);
    formantAtt = std::make_unique<SliderAtt>(proc.apvts, "formant", formantKnob);

    // ---- Colour scheme ----
    using namespace VocodeColors;
    auto& lf = getLookAndFeel();
    lf.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    lf.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff1a2d3e));
    lf.setColour(juce::Slider::thumbColourId, kOut);
    lf.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff7aaabb));
    lf.setColour(juce::Slider::textBoxBackgroundColourId, kBg);
    lf.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    lf.setColour(juce::Label::textColourId, juce::Colour(0xff3d6070));
    lf.setColour(juce::ComboBox::backgroundColourId, kBg);
    lf.setColour(juce::ComboBox::textColourId, juce::Colour(0xff7aaabb));
    lf.setColour(juce::ComboBox::arrowColourId, kAccent);
    lf.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff1a2d3e));
    lf.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff090910));
    lf.setColour(juce::PopupMenu::textColourId, juce::Colour(0xff7aaabb));
    lf.setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff123040));
    lf.setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffeef6ff));

    // TextButton colours — will be overridden dynamically in paint()
    lf.setColour(juce::TextButton::buttonColourId, kAnalogOff);
    lf.setColour(juce::TextButton::buttonOnColourId, kAnalogOn);
    lf.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff3a5060));
    lf.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));

    startTimerHz(30);
}

VocodeAudioProcessorEditor::~VocodeAudioProcessorEditor()
{
    stopTimer();
}

// ============================================================================
// Timer callback
// ============================================================================
void VocodeAudioProcessorEditor::timerCallback()
{
    proc.getMainFFTData(mainData.data(), kMaxBins);
    proc.getSidechainFFTData(sideData.data(), kMaxBins);
    proc.getMorphedFFTData(morphData.data(), kMaxBins);
    repaint(spectrumBounds().toNearestInt());

    // Also repaint control panel if analog mode state changed (button glow)
    const int ctrlTop = getHeight() - 134;
    repaint(0, ctrlTop, getWidth(), 134);
}

// ============================================================================
// Layout helpers
// ============================================================================
juce::Rectangle<float> VocodeAudioProcessorEditor::spectrumBounds() const
{
    const float top = 26.0f;
    const float bottom = (float)getHeight() - 134.0f - 16.0f;
    return { 42.0f, top, (float)getWidth() - 52.0f, bottom - top };
}

float VocodeAudioProcessorEditor::binToX(int bin) const
{
    const auto  sb = spectrumBounds();
    const float nyq = (float)(proc.currentSampleRate * 0.5);
    const float hz = (float)bin * nyq / (float)(proc.numBins - 1);
    if (hz <= 20.0f)    return sb.getX();
    if (hz >= 20000.0f) return sb.getRight();
    return sb.getX() + (std::log10(hz / 20.0f) / 3.0f) * sb.getWidth();
}

float VocodeAudioProcessorEditor::magToY(float mag) const
{
    const auto  sb = spectrumBounds();
    const float dB = 20.0f * std::log10(std::max(mag, 1e-9f));
    const float norm = juce::jlimit(0.0f, 1.0f, (dB - kFloorDB) / -kFloorDB);
    return sb.getBottom() - norm * sb.getHeight();
}

// ============================================================================
// Grid
// ============================================================================
void VocodeAudioProcessorEditor::drawGrid(juce::Graphics& g)
{
    using namespace VocodeColors;
    const auto sb = spectrumBounds();

    const float freqs[] = { 50,100,200,500,1000,2000,5000,10000,20000 };
    const char* fLbls[] = { "50","100","200","500","1k","2k","5k","10k","20k" };
    const bool  fMajor[] = { false,true,false,false,true,false,false,true,true };

    for (int i = 0; i < 9; ++i)
    {
        const float t = std::log10(freqs[i] / 20.0f) / 3.0f;
        const float x = sb.getX() + t * sb.getWidth();
        g.setColour(fMajor[i] ? kGridBrt : kGrid);
        g.drawVerticalLine((int)x, sb.getY(), sb.getBottom());
    }

    const float dbs[] = { 0.0f,-20.0f,-40.0f,-60.0f,-80.0f };
    const char* dbLbls[] = { "0","-20","-40","-60","-80" };

    for (int i = 0; i < 5; ++i)
    {
        const float norm = (dbs[i] - kFloorDB) / -kFloorDB;
        const float y = sb.getBottom() - norm * sb.getHeight();
        g.setColour(i == 0 ? kGridBrt : kGrid);
        g.drawHorizontalLine((int)y, sb.getX(), sb.getRight());
    }

    g.setFont(10.0f);
    g.setColour(kGridTxt);
    for (int i = 0; i < 9; ++i)
    {
        const float t = std::log10(freqs[i] / 20.0f) / 3.0f;
        const float x = sb.getX() + t * sb.getWidth();
        g.drawText(fLbls[i], (int)x - 14, (int)sb.getBottom() + 3, 28, 12,
            juce::Justification::centred);
    }

    g.setColour(kGridTxt);
    for (int i = 0; i < 5; ++i)
    {
        const float norm = (dbs[i] - kFloorDB) / -kFloorDB;
        const float y = sb.getBottom() - norm * sb.getHeight();
        g.drawText(dbLbls[i], 0, (int)y - 6, 40, 12, juce::Justification::right);
    }
}

// ============================================================================
// Spectrum curve
// ============================================================================
void VocodeAudioProcessorEditor::drawSpectrum(
    juce::Graphics& g,
    const std::array<float, kMaxBins>& data,
    juce::Colour colour, float alpha)
{
    const auto  sb = spectrumBounds();
    const int   nb = proc.numBins;
    const float nyq = (float)(proc.currentSampleRate * 0.5);

    juce::Path outline;
    float firstX = -1.0f, lastX = -1.0f;

    for (int bin = 1; bin < nb; ++bin)
    {
        const float hz = (float)bin * nyq / (float)(nb - 1);
        if (hz < 20.0f)    continue;
        if (hz > 21000.0f) break;

        const float x = binToX(bin);
        const float y = magToY(data[bin]);

        if (firstX < 0.0f) { outline.startNewSubPath(x, y); firstX = x; }
        else { outline.lineTo(x, y); }
        lastX = x;
    }

    if (firstX < 0.0f) return;

    juce::Path fill = outline;
    fill.lineTo(lastX, sb.getBottom());
    fill.lineTo(firstX, sb.getBottom());
    fill.closeSubPath();
    g.setColour(colour.withAlpha(alpha * 0.10f));
    g.fillPath(fill);

    g.setColour(colour.withAlpha(alpha));
    g.strokePath(outline, juce::PathStrokeType(1.5f));
}

// ============================================================================
// paint
// ============================================================================
void VocodeAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace VocodeColors;
    const bool isAnalog = proc.apvts.getParameter("analogMode")->getValue() > 0.5f;

    // Full background
    g.fillAll(kBg);

    // Title bar
    g.setColour(kPanel);
    g.fillRect(0, 0, getWidth(), 26);
    g.setColour(kDivider);
    g.drawHorizontalLine(25, 0.0f, (float)getWidth());

    g.setFont(juce::Font(15.0f, juce::Font::bold));
    g.setColour(kAccent);
    g.drawText("VOCODE", 10, 4, 90, 18, juce::Justification::left);

    g.setFont(10.0f);
    g.setColour(juce::Colour(0xff2a4055));
    g.drawText("spectral vocoder", 96, 7, 160, 12, juce::Justification::left);

    // Mode badge (top-right of title bar)
    {
        const juce::Colour modeCol = isAnalog ? kAnalogOn : kAccent.withAlpha(0.4f);
        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.setColour(modeCol);
        const char* modeStr = isAnalog ? "● ANALOG" : "● FFT";
        g.drawText(modeStr, getWidth() - 80, 7, 74, 12, juce::Justification::right);
    }

    // Spectrum canvas
    const auto sb = spectrumBounds();
    g.setColour(kPanel);
    g.fillRect(sb);
    g.setColour(kGrid);
    g.drawRect(sb, 1.0f);

    drawGrid(g);

    drawSpectrum(g, mainData, kCarrier, 0.55f);
    drawSpectrum(g, sideData, kMod, 0.55f);
    drawSpectrum(g, morphData, kOut, 0.90f);

    // Legend
    {
        const float lx = sb.getRight() - 94.0f;
        const float ly = sb.getY() + 6.0f;
        const juce::Colour cols[] = { kCarrier, kMod, kOut };
        const char* names[] = { "CARRIER", "MODULATOR", "OUTPUT" };
        g.setFont(9.0f);
        for (int i = 0; i < 3; ++i)
        {
            const float y = ly + i * 13.0f;
            g.setColour(cols[i].withAlpha(0.80f));
            g.fillRect(lx, y + 4.0f, 10.0f, 5.0f);
            g.drawText(names[i], (int)(lx + 14.0f), (int)y, 80, 11,
                juce::Justification::left);
        }
    }

    // Control panel background
    const int ctrlTop = getHeight() - 134;
    g.setColour(kPanel);
    g.fillRect(0, ctrlTop, getWidth(), 134);
    g.setColour(kDivider);
    g.drawHorizontalLine(ctrlTop, 0.0f, (float)getWidth());

    // Thin accent line under top row of controls when analog mode is on
    if (isAnalog)
    {
        g.setColour(kAnalogOn.withAlpha(0.35f));
        g.drawHorizontalLine(ctrlTop + 52, 4.0f, (float)(getWidth() - 4));
    }

    // Vertical separator between combos and knobs area (optional cosmetic)
    g.setColour(kDivider);
    g.drawVerticalLine(410, ctrlTop + 8, getHeight() - 8);

    // Dim the inactive combo when in the other mode
    // (drawn by JUCE; we just draw a translucent overlay)
    const int row1Y = ctrlTop + 8;
    if (isAnalog)
    {
        // Dim the FFT SIZE combo
        g.setColour(kBg.withAlpha(0.55f));
        g.fillRoundedRectangle(10.0f, (float)(row1Y), 120.0f, 44.0f, 3.0f);
    }
    else
    {
        // Dim the BANDS combo
        g.setColour(kBg.withAlpha(0.55f));
        g.fillRoundedRectangle(140.0f, (float)(row1Y), 120.0f, 44.0f, 3.0f);
    }

    // Also dim CLARITY knob in analog mode (it has no effect there)
    if (isAnalog)
    {
        const int knobAreaX = 8;
        const int knobW = (getWidth() - 16) / 5;
        const int x = knobAreaX + 1 * knobW;   // CLARITY is the 2nd knob
        g.setColour(kBg.withAlpha(0.50f));
        g.fillRoundedRectangle((float)x, (float)(ctrlTop + 54), (float)knobW, 76.0f, 3.0f);
    }
}

// ============================================================================
// resized
// ============================================================================
void VocodeAudioProcessorEditor::resized()
{
    const int ctrlTop = getHeight() - 134;

    // ---- Row 1: two combos + analog toggle (top 52 px of control panel) ----

    // FFT SIZE
    fftLbl.setBounds(10, ctrlTop + 8, 120, 14);
    fftSizeCombo.setBounds(10, ctrlTop + 24, 120, 24);

    // BANDS
    bandsLbl.setBounds(140, ctrlTop + 8, 120, 14);
    numBandsCombo.setBounds(140, ctrlTop + 24, 120, 24);

    // ANALOG toggle button
    analogModeBtn.setBounds(276, ctrlTop + 22, 120, 28);

    // ---- Row 2: 5 rotary knobs (bottom 76 px) ----
    // MORPH | CLARITY | SMOOTH | GATE | FORMANT
    const int knobAreaX = 8;
    const int knobW = (getWidth() - 16) / 5;
    const int knobRow = ctrlTop + 54;

    juce::Label* lbls[] = { &morphLbl,  &clarityLbl, &smoothingLbl, &gateLbl,  &formantLbl };
    juce::Slider* knbs[] = { &morphKnob, &clarityKnob, &smoothingKnob, &gateKnob, &formantKnob };

    for (int i = 0; i < 5; ++i)
    {
        const int x = knobAreaX + i * knobW;
        lbls[i]->setBounds(x, knobRow, knobW, 14);
        knbs[i]->setBounds(x + 6, knobRow + 14, knobW - 12, 62);
    }
}