/*
  ==============================================================================
    PitchControl – IIR Bell Filter + FFT Spectral Shift Plugin
    Editor Implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//==============================================================================
namespace Colors
{
    const juce::Colour background{ 0xff00cccc };
    const juce::Colour panelBg{ 0xff00cc67 };
    const juce::Colour accent{ 0x77eeeeee };
    const juce::Colour accentBright{ 0xff533483 };
    const juce::Colour activeKey{ 0xff00ffff };
    const juce::Colour knobFill{ 0xff00ffff };
    const juce::Colour whiteKey{ 0xfff0f0f0 };
    const juce::Colour blackKey{ 0xff1e1e2e };
    const juce::Colour whiteKeyActive{ 0xff00ffff };
    const juce::Colour blackKeyActive{ 0xff00dddd };
    const juce::Colour knobTrack{ 0xff000000 };
    const juce::Colour knobPointer{ 0xffffffff };
    const juce::Colour textBright{ 0xfff0f0f0 };
    const juce::Colour textDim{ 0xff888888 };
    const juce::Colour borderColor{ 0xff00ffff };
    const juce::Colour fftAccent{ 0xffffaa00 };
    const juce::Colour fftActive{ 0xffff6600 };

    // Spectrum view colours — mirror the reference SpectralFilter plugin exactly
    const juce::Colour specBg{ 0xff111416 };   // near-black bg
    const juce::Colour specGrid{ 0xff2a2e32 };   // subtle dark grid
    const juce::Colour specGridZero{ 0xff44aacc };   // 0 dB line teal
    const juce::Colour specFill{ 0xff336655 };   // green fill (same as SpectralFilter)
    const juce::Colour specLine{ 0xff55eedd };   // bright teal line
    const juce::Colour specNoteOn{ 0x88ffaa00 };   // amber note lines (protected)
    const juce::Colour specText{ 0xffaaaaaa };   // label text
    const juce::Colour specTooltipBg{ 0xcc111416 };
}

//==============================================================================
// PitchControlLookAndFeel
//==============================================================================
PitchControlLookAndFeel::PitchControlLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId, Colors::knobFill);
    setColour(juce::Slider::rotarySliderOutlineColourId, Colors::knobTrack);
    setColour(juce::Slider::thumbColourId, Colors::knobPointer);
    setColour(juce::Label::textColourId, Colors::textBright);
    setColour(juce::ComboBox::backgroundColourId, Colors::accent);
    setColour(juce::ComboBox::textColourId, Colors::textBright);
    setColour(juce::ComboBox::outlineColourId, Colors::borderColor);
    setColour(juce::ComboBox::arrowColourId, Colors::textBright);
    setColour(juce::PopupMenu::backgroundColourId, Colors::panelBg);
    setColour(juce::PopupMenu::textColourId, Colors::textBright);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Colors::accentBright);
    setColour(juce::PopupMenu::highlightedTextColourId, Colors::textBright);
}

void PitchControlLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    const float r = std::min(width, height) * 0.5f - 4.0f;
    const float cx = x + width * 0.5f;
    const float cy = y + height * 0.5f;

    {
        juce::Path p; p.addArc(cx - r, cy - r, r * 2, r * 2, startAngle, endAngle, true);
        g.setColour(Colors::knobTrack); g.strokePath(p, juce::PathStrokeType(3.0f));
    }

    {
        juce::Path p; p.addArc(cx - r, cy - r, r * 2, r * 2, startAngle,
            startAngle + (endAngle - startAngle) * sliderPos, true);
        g.setColour(Colors::knobFill); g.strokePath(p, juce::PathStrokeType(3.0f));
    }

    juce::ColourGradient grad(Colors::accent.brighter(0.3f), cx - r * 0.3f, cy - r * 0.3f,
        Colors::accent.darker(0.5f), cx + r * 0.3f, cy + r * 0.3f, false);
    g.setGradientFill(grad);
    g.fillEllipse(cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f);
    g.setColour(Colors::borderColor.withAlpha(0.6f));
    g.drawEllipse(cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f, 1.5f);

    float angle = startAngle + sliderPos * (endAngle - startAngle);
    g.setColour(Colors::knobPointer);
    g.drawLine(cx + std::sin(angle) * r * 0.35f, cy - std::cos(angle) * r * 0.35f,
        cx + std::sin(angle) * r * 0.55f, cy - std::cos(angle) * r * 0.55f, 2.0f);
}

void PitchControlLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (!label.isBeingEdited())
    {
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont());
        g.drawFittedText(label.getText(), label.getLocalBounds(),
            label.getJustificationType(), 1, 1.0f);
    }
}

//==============================================================================
// SpectrumView
//==============================================================================
SpectrumView::SpectrumView(PitchControlAudioProcessor& p) : m_proc(p)
{
    m_displayData.fill(0.0f);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(30);
}

SpectrumView::~SpectrumView() { stopTimer(); }

void SpectrumView::resized()
{
    // Toggle button lives in the top-right corner of the component
    m_toggleBtnRect = juce::Rectangle<int>(getWidth() - 102, 3, 98, 16);
}

void SpectrumView::timerCallback()
{
    m_proc.getSpectrumData(m_displayData.data(), kNumFFTBins);

    // Detect silence: compute peak magnitude over the snapshot
    float peak = 0.0f;
    for (int i = 1; i < kNumFFTBins; ++i)
        peak = juce::jmax(peak, m_displayData[i]);

    // Smooth the silence alpha: fast attack, slow release (~1s at 30Hz)
    const float target = (peak > 0.005f) ? 1.0f : 0.0f;
    const float attack = 0.35f;   // fast: ~3 frames to full
    const float release = 0.033f;  // slow: ~1s to fade out
    const float coeff = (target > m_silenceAlpha) ? attack : release;
    m_silenceAlpha += coeff * (target - m_silenceAlpha);

    repaint();
}

void SpectrumView::mouseMove(const juce::MouseEvent& e) { m_hoverX = (float)e.x; repaint(); }
void SpectrumView::mouseExit(const juce::MouseEvent&) { m_hoverX = -1.0f;       repaint(); }

void SpectrumView::mouseDown(const juce::MouseEvent& e)
{
    if (m_toggleBtnRect.contains(e.x, e.y))
    {
        m_showDiff = !m_showDiff;
        repaint();
    }
}

//==============================================================================
// Coordinate helpers
//==============================================================================
float SpectrumView::binToX(int bin) const
{
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float fpb = nyquist / (float)(kNumFFTBins - 1);
    float freq = juce::jmax(20.0f, (float)bin * fpb);
    return freqToX(freq);
}

float SpectrumView::freqToX(float hz) const
{
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float logMin = std::log10(20.0f);
    const float logMax = std::log10(nyquist);
    float norm = (std::log10(juce::jmax(20.0f, hz)) - logMin) / (logMax - logMin);
    return juce::jlimit(0.0f, (float)getWidth(), norm * (float)getWidth());
}

float SpectrumView::magToY(float norm) const
{
    return (float)getHeight() - norm * (float)getHeight() * 0.80f;
}

float SpectrumView::xToFreq(float x) const
{
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float logMin = std::log10(20.0f);
    const float logMax = std::log10(nyquist);
    float norm = juce::jlimit(0.0f, 1.0f, x / (float)getWidth());
    return std::pow(10.0f, logMin + norm * (logMax - logMin));
}

//==============================================================================
// computeFilterGainDB
// Evaluate the combined peaking EQ response (dB) at a given frequency.
// We compute the magnitude response of each active biquad analytically.
// For a peaking EQ, |H(e^jw)|^2 = (b0 + b1*cos(w) + b2*cos(2w))^2 + (b1*sin(w) + b2*sin(2w))^2
//                                   / same with a coefficients.
// This is exact and fast (no audio thread needed).
//==============================================================================
float SpectrumView::computeFilterGainDB(float hz) const
{
    if (hz <= 0.0f) return 0.0f;
    const double sr = m_proc.currentSampleRate;

    // Read active params
    const float depthDB = m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::depthParamID())->load();
    const float Q = m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::qParamID())->load();
    const float boostDB = m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::boostDBParamID())->load();
    const float boostQ = m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::boostQParamID())->load();
    int rangeFrom = (int)m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::rangeFromParamID())->load();
    int rangeTo = (int)m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::rangeToParamID())->load();
    if (rangeFrom > rangeTo) std::swap(rangeFrom, rangeTo);

    bool noteProtected[kNumNotes]{};
    int numProtected = 0;
    for (int i = 0; i < kNumNotes; ++i)
    {
        noteProtected[i] = m_proc.apvts.getRawParameterValue(
            PitchControlAudioProcessor::noteActiveParamID(i))->load() > 0.5f;
        if (noteProtected[i]) ++numProtected;
    }
    if (numProtected == 0) return 0.0f;

    const bool wetOnly = m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::wetOnlyParamID())->load() > 0.5f;

    // Helper: evaluate biquad magnitude response (dB) at given frequency
    auto biquadMagDB = [&](const Biquad& bq, double freqHz) -> double
        {
            double w = 2.0 * M_PI * freqHz / sr;
            double c1 = std::cos(w), c2 = std::cos(2.0 * w);
            double s1 = std::sin(w), s2 = std::sin(2.0 * w);
            double numRe = bq.b0 + bq.b1 * c1 + bq.b2 * c2;
            double numIm = bq.b1 * s1 + bq.b2 * s2;
            // denominator: 1 + a1*cos(w) + a2*cos(2w), etc.
            double denRe = 1.0 + bq.a1 * c1 + bq.a2 * c2;
            double denIm = bq.a1 * s1 + bq.a2 * s2;
            double mag2 = (numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm + 1e-30);
            return 10.0 * std::log10(juce::jmax(1e-12, mag2));
        };

    double totalDB = 0.0;

    for (int n = 0; n < kTotalNotes; ++n)
    {
        int  nc = n % kNumNotes;
        bool isProtected = noteProtected[nc];
        bool shouldFilter = wetOnly ? isProtected : !isProtected;

        if (n >= rangeFrom && n <= rangeTo && shouldFilter)
        {
            Biquad bq = makePeakingEQ(440.0 * std::pow(2.0, (n - 69.0) / 12.0),
                sr, (double)depthDB, (double)Q);
            totalDB += biquadMagDB(bq, (double)hz);
        }

        bool shouldBoost = wetOnly ? !isProtected : isProtected;
        if (n >= rangeFrom && n <= rangeTo && shouldBoost && boostDB > 0.01f)
        {
            Biquad bq = makePeakingEQ(440.0 * std::pow(2.0, (n - 69.0) / 12.0),
                sr, (double)boostDB, (double)boostQ);
            totalDB += biquadMagDB(bq, (double)hz);
        }
    }

    return (float)totalDB;
}

//==============================================================================
// computeShiftedX
// For FFT mode diff: given a source frequency, find where it gets shifted to
// on screen. Returns the destination x pixel. If this bin has blendWeight > 0,
// the destination is lerped toward the protected-note frequency.
//==============================================================================
float SpectrumView::computeShiftedX(float hz) const
{
    if (hz <= 20.0f) return freqToX(hz);

    const float sr = (float)m_proc.currentSampleRate;
    const float shiftStr = m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::shiftStrengthParamID())->load();
    const float Q = m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::qParamID())->load();
    const float curveExp = juce::jlimit(0.1f, 10.0f, 10.0f * (1.0f - (Q - 1.0f) / 99.0f));

    int rangeFrom = (int)m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::rangeFromParamID())->load();
    int rangeTo = (int)m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::rangeToParamID())->load();
    if (rangeFrom > rangeTo) std::swap(rangeFrom, rangeTo);

    bool noteProtected[kNumNotes]{};
    int numProtected = 0;
    for (int i = 0; i < kNumNotes; ++i)
    {
        noteProtected[i] = m_proc.apvts.getRawParameterValue(
            PitchControlAudioProcessor::noteActiveParamID(i))->load() > 0.5f;
        if (noteProtected[i]) ++numProtected;
    }
    if (numProtected == 0) return freqToX(hz);

    // Continuous MIDI of this frequency
    const double binMidi = 69.0 + 12.0 * std::log2((double)hz / 440.0);

    // Check range
    if (binMidi < (double)rangeFrom || binMidi >(double)rangeTo)
        return freqToX(hz);

    // Find nearest and second nearest protected notes (across all octaves)
    float bestDist = 1e9f;
    float bestHz = hz;
    float secondDist = 1e9f;

    for (int oct = -1; oct <= kNumOctaves; ++oct)
        for (int nc = 0; nc < kNumNotes; ++nc)
        {
            if (!noteProtected[nc]) continue;
            int mn = nc + oct * kNumNotes;
            float dist = std::abs((float)(binMidi - (double)mn)) * 100.0f;  // cents
            if (dist < bestDist)
            {
                secondDist = bestDist;
                bestDist = dist;
                bestHz = (float)(440.0 * std::pow(2.0, (mn - 69.0) / 12.0));
            }
            else if (dist < secondDist)
            {
                secondDist = dist;
            }
        }

    const float attractRadius = juce::jmax(50.0f, secondDist * 0.5f);
    const float normDist = juce::jlimit(0.0f, 1.0f, bestDist / attractRadius);
    const float weight = std::pow(1.0f - normDist, curveExp) * shiftStr;

    const float srcX = freqToX(hz);
    const float dstX = freqToX(bestHz);
    return srcX + weight * (dstX - srcX);
}

//==============================================================================
void SpectrumView::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawSpectrum(g);     // fades to invisible when silent
    drawNoteLines(g);
    if (m_showDiff)
        drawDiffOverlay(g);
    drawLabels(g);
    drawTooltip(g);
    drawToggleButton(g);

    // Border
    g.setColour(Colors::borderColor.withAlpha(0.5f));
    g.drawRect(getLocalBounds(), 1);
}

//==============================================================================
void SpectrumView::drawBackground(juce::Graphics& g)
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float logMin = std::log10(20.0f);
    const float logMax = std::log10(nyquist);

    g.fillAll(Colors::specBg);

    const float dbLines[] = { 0.0f, -12.0f, -24.0f, -48.0f, -72.0f, -90.0f };
    for (float dB : dbLines)
    {
        float norm = juce::jlimit(0.0f, 1.0f, (dB + 90.0f) / 90.0f);
        float y = magToY(norm);
        g.setColour(dB == 0.0f ? Colors::specGridZero : Colors::specGrid);
        g.drawHorizontalLine((int)y, 0.0f, W);
    }

    const float freqLines[] = { 20.f, 50.f, 100.f, 200.f, 500.f,
                                 1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
    for (float freq : freqLines)
    {
        if (freq > nyquist) continue;
        float x = (std::log10(freq) - logMin) / (logMax - logMin) * W;
        g.setColour(Colors::specGrid);
        g.drawVerticalLine((int)x, 0.0f, H);
    }
}

//==============================================================================
void SpectrumView::drawSpectrum(juce::Graphics& g)
{
    // Fade the spectrum in/out based on silence detection
    if (m_silenceAlpha < 0.005f) return;

    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float logMin = std::log10(20.0f);
    const float logMax = std::log10(nyquist);
    const float fpb = nyquist / (float)(kNumFFTBins - 1);

    float maxMag = 0.001f;
    for (int i = 1; i < kNumFFTBins; ++i)
        maxMag = juce::jmax(maxMag, m_displayData[i]);

    juce::Path sp;
    bool started = false;
    for (int i = 1; i < kNumFFTBins; ++i)
    {
        float freq = (float)i * fpb;
        if (freq < 20.0f || freq > nyquist) continue;

        float x = (std::log10(freq) - logMin) / (logMax - logMin) * W;
        float dB = 20.0f * std::log10(m_displayData[i] / maxMag + 0.00001f);
        float norm = juce::jlimit(0.0f, 1.0f, (dB + 90.0f) / 90.0f);
        float y = magToY(norm);

        if (!started) { sp.startNewSubPath(x, H); sp.lineTo(x, y); started = true; }
        else            sp.lineTo(x, y);
    }
    if (started) { sp.lineTo(W, H); sp.closeSubPath(); }

    g.setColour(Colors::specFill.withAlpha(0.18f * m_silenceAlpha));
    g.fillPath(sp);
    g.setColour(Colors::specLine.withAlpha(0.85f * m_silenceAlpha));
    g.strokePath(sp, juce::PathStrokeType(1.5f));
}

//==============================================================================
void SpectrumView::drawNoteLines(juce::Graphics& g)
{
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float H = (float)getHeight();

    for (int nc = 0; nc < kNumNotes; ++nc)
    {
        bool active = m_proc.apvts.getRawParameterValue(
            PitchControlAudioProcessor::noteActiveParamID(nc))->load() > 0.5f;
        if (!active) continue;

        for (int oct = 0; oct < kNumOctaves; ++oct)
        {
            int midiNote = nc + oct * kNumNotes;
            float hz = (float)(440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0));
            if (hz < 20.0f || hz > nyquist) continue;

            float x = freqToX(hz);

            g.setColour(Colors::specNoteOn.withAlpha(0.55f));
            g.drawVerticalLine((int)x, 0.0f, H);

            g.setColour(Colors::fftAccent.withAlpha(0.9f));
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
            g.drawText(kNoteNames[nc], (int)x - 8, 2, 16, 10, juce::Justification::centred, false);
        }
    }
}

//==============================================================================
// drawDiffOverlay
//
// Filter mode: composite EQ curve drawn analytically from all active biquads.
//   Colour: bright white/cyan line, filled area below zero-dB tinted magenta/red.
//
// FFT mode: for each protected note across all octaves, draw:
//   • a thin source line at the note's natural frequency
//   • an arrow pointing to the shifted destination frequency
//   • the shift amount scales with shiftStrength and Q just like the processor
//==============================================================================
void SpectrumView::drawDiffOverlay(juce::Graphics& g)
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);

    const bool fftMode = m_proc.apvts.getRawParameterValue(
        PitchControlAudioProcessor::fftModeParamID())->load() > 0.5f;

    // -----------------------------------------------------------------------
    if (!fftMode)
    {
        // ---- Filter mode: draw composite EQ curve ----
        // We evaluate at ~400 log-spaced frequencies across 20Hz–nyquist.
        // Map dB to Y using a ±30 dB range centred at the 0dB gridline.
        // The 0dB gridline sits at magToY(1.0) = top of the 80% band.

        const int   kPoints = 400;
        const float logMin = std::log10(20.0f);
        const float logMax = std::log10(nyquist);
        const float dBRange = 30.0f;   // ±30 dB display range for the EQ curve
        const float zeroY = magToY(1.0f);  // 0 dB line Y

        // Helper: dB offset → Y (positive dB = above zeroY)
        auto eqDBtoY = [&](float dB) -> float {
            float norm = juce::jlimit(-1.0f, 1.0f, dB / dBRange);
            return zeroY - norm * H * 0.38f;  // 38% of height for ±30 dB
            };

        juce::Path fillPos, fillNeg, line;
        bool started = false;

        for (int i = 0; i <= kPoints; ++i)
        {
            float t = (float)i / (float)kPoints;
            float freq = std::pow(10.0f, logMin + t * (logMax - logMin));
            float x = t * W;
            float dB = computeFilterGainDB(freq);
            float y = eqDBtoY(dB);

            if (!started)
            {
                line.startNewSubPath(x, y);
                fillPos.startNewSubPath(x, zeroY);
                fillPos.lineTo(x, y);
                fillNeg.startNewSubPath(x, zeroY);
                fillNeg.lineTo(x, y);
                started = true;
            }
            else
            {
                line.lineTo(x, y);
                fillPos.lineTo(x, y);
                fillNeg.lineTo(x, y);
            }
        }
        // Close fill paths at zero line
        fillPos.lineTo(W, zeroY); fillPos.closeSubPath();
        fillNeg.lineTo(W, zeroY); fillNeg.closeSubPath();

        // Cuts (below 0dB) in warm red, boosts (above 0dB) in cyan
        g.setColour(juce::Colour(0x33ff4444));  // translucent red fill for cuts
        g.fillPath(fillNeg);
        g.setColour(juce::Colour(0x2200ffcc));  // translucent cyan fill for boosts
        g.fillPath(fillPos);

        // Bright white/cyan curve line
        g.setColour(juce::Colour(0xddffffff));
        g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        // Zero dB reference line in the EQ overlay (subtle)
        g.setColour(juce::Colour(0x55ffffff));
        g.drawHorizontalLine((int)zeroY, 0.0f, W);
    }
    else
    {
        // ---- FFT mode: draw shift arrows for each protected note ----
        // For each active note across all octaves:
        //   • solid amber line at source frequency
        //   • dashed or thinner line at destination frequency  
        //   • horizontal arrow between them

        const float nyq = nyquist;
        int rangeFrom = (int)m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::rangeFromParamID())->load();
        int rangeTo = (int)m_proc.apvts.getRawParameterValue(PitchControlAudioProcessor::rangeToParamID())->load();
        if (rangeFrom > rangeTo) std::swap(rangeFrom, rangeTo);

        // Non-protected notes in range: show where they shift TO
        // (they get pulled toward protected notes)
        // We sample at semi-tone intervals within the active range
        for (int n = rangeFrom; n <= rangeTo; ++n)
        {
            int nc = n % kNumNotes;
            bool isProtected = m_proc.apvts.getRawParameterValue(
                PitchControlAudioProcessor::noteActiveParamID(nc))->load() > 0.5f;
            if (isProtected) continue;  // skip protected, they're shown differently

            float srcHz = (float)(440.0 * std::pow(2.0, (n - 69.0) / 12.0));
            if (srcHz < 20.0f || srcHz > nyq) continue;

            float srcX = freqToX(srcHz);
            float dstX = computeShiftedX(srcHz);

            if (std::abs(dstX - srcX) < 1.0f) continue;  // no visible shift, skip

            // Arrow height: centred at mid-height
            const float arrowY = H * 0.5f;
            const float tickH = H * 0.18f;

            // Source tick (thin, white)
            g.setColour(juce::Colour(0x66ffffff));
            g.drawVerticalLine((int)srcX, arrowY - tickH * 0.5f, arrowY + tickH * 0.5f);

            // Horizontal arrow from srcX to dstX
            const float dir = (dstX > srcX) ? 1.0f : -1.0f;
            const float headSz = 4.0f;
            g.setColour(juce::Colour(0xccffaa00));   // amber arrow
            g.drawLine(srcX, arrowY, dstX, arrowY, 1.0f);
            // Arrowhead
            g.drawLine(dstX, arrowY, dstX - dir * headSz, arrowY - headSz, 1.0f);
            g.drawLine(dstX, arrowY, dstX - dir * headSz, arrowY + headSz, 1.0f);
        }

        // Protected note destination lines: bright amber, taller
        for (int n = rangeFrom; n <= rangeTo; ++n)
        {
            int nc = n % kNumNotes;
            bool isProtected = m_proc.apvts.getRawParameterValue(
                PitchControlAudioProcessor::noteActiveParamID(nc))->load() > 0.5f;
            if (!isProtected) continue;

            float hz = (float)(440.0 * std::pow(2.0, (n - 69.0) / 12.0));
            if (hz < 20.0f || hz > nyq) continue;

            float x = freqToX(hz);

            // Bright glowing protected-note column
            g.setColour(juce::Colour(0x44ffaa00));
            g.fillRect(x - 1.5f, 0.0f, 3.0f, H);
            g.setColour(juce::Colour(0xeeffaa00));
            g.drawVerticalLine((int)x, 0.0f, H);
        }
    }
}

//==============================================================================
void SpectrumView::drawLabels(juce::Graphics& g)
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float logMin = std::log10(20.0f);
    const float logMax = std::log10(nyquist);

    g.setFont(juce::FontOptions(9.5f));

    struct { float dB; const char* label; } dbLabels[] =
    { {0.0f,"0 dB"}, {-12.0f,"-12"}, {-24.0f,"-24"}, {-48.0f,"-48"}, {-72.0f,"-72"} };
    for (auto& dl : dbLabels)
    {
        float norm = juce::jlimit(0.0f, 1.0f, (dl.dB + 90.0f) / 90.0f);
        float y = magToY(norm);
        g.setColour(dl.dB == 0.0f ? Colors::specGridZero.brighter(0.3f) : Colors::specText);
        g.drawText(dl.label, (int)W - 28, (int)y - 6, 26, 12, juce::Justification::right, false);
    }

    const float freqLabels[] = { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f };
    for (float freq : freqLabels)
    {
        if (freq > nyquist) continue;
        float x = (std::log10(freq) - logMin) / (logMax - logMin) * W;
        juce::String lbl = (freq < 1000.f) ? juce::String((int)freq)
            : (freq < 10000.f ? juce::String(freq / 1000.f, 1) + "k"
                : juce::String((int)(freq / 1000.f)) + "k");
        g.setColour(Colors::specText);
        g.drawText(lbl, (int)x - 16, (int)H - 13, 32, 12, juce::Justification::centred, false);
    }

    // Mode chip top-left
    const bool fftMode = m_proc.apvts.getRawParameterValue(
        PitchControlAudioProcessor::fftModeParamID())->load() > 0.5f;
    juce::String chip = fftMode ? "FFT MODE" : "FILTER MODE";
    g.setColour(fftMode ? Colors::fftAccent : Colors::specGridZero);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(chip, 4, 2, 80, 12, juce::Justification::left, false);
}

//==============================================================================
void SpectrumView::drawTooltip(juce::Graphics& g)
{
    if (m_hoverX < 0.0f || m_hoverX >(float)getWidth()) return;
    if (m_silenceAlpha < 0.02f) return;  // no tooltip when silent

    const float nyquist = (float)(m_proc.currentSampleRate * 0.5);
    const float fpb = nyquist / (float)(kNumFFTBins - 1);
    float freq = xToFreq(m_hoverX);
    int   bin = juce::jlimit(1, kNumFFTBins - 1, (int)(freq / fpb));

    float maxMag = 0.001f;
    for (int i = 1; i < kNumFFTBins; ++i) maxMag = juce::jmax(maxMag, m_displayData[i]);
    float dB = 20.0f * std::log10(m_displayData[bin] / maxMag + 0.00001f);
    float norm = juce::jlimit(0.0f, 1.0f, (dB + 90.0f) / 90.0f);
    float dotY = magToY(norm);

    g.setColour(juce::Colours::white);
    g.fillEllipse(m_hoverX - 3.5f, dotY - 3.5f, 7.0f, 7.0f);

    juce::String freqStr = (freq < 1000.0f)
        ? juce::String((int)freq) + " Hz"
        : juce::String(freq / 1000.0f, 1) + " kHz";
    juce::String tip = freqStr + "  " + juce::String(dB, 1) + " dB";

    float lx = m_hoverX + 9.0f;
    if (lx + 120.0f > (float)getWidth()) lx = m_hoverX - 128.0f;
    float ly = dotY - 22.0f;
    if (ly < 2.0f) ly = dotY + 8.0f;

    g.setColour(Colors::specTooltipBg);
    g.fillRoundedRectangle(lx - 3.0f, ly - 2.0f, 124.0f, 17.0f, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(10.5f));
    g.drawText(tip, (int)lx, (int)ly, 120, 14, juce::Justification::left, false);
}

//==============================================================================
void SpectrumView::drawToggleButton(juce::Graphics& g)
{
    const bool active = m_showDiff;
    juce::Colour bgCol = active ? juce::Colour(0xddffaa00) : juce::Colour(0x881a1f24);
    juce::Colour txtCol = active ? juce::Colours::black : juce::Colour(0xffaaaaaa);

    g.setColour(bgCol);
    g.fillRoundedRectangle(m_toggleBtnRect.toFloat(), 3.0f);
    g.setColour(active ? juce::Colour(0xeeffffff) : juce::Colour(0x44aaaaaa));
    g.drawRoundedRectangle(m_toggleBtnRect.toFloat(), 3.0f, 1.0f);

    g.setColour(txtCol);
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("Show Differences", m_toggleBtnRect, juce::Justification::centred, false);
}

//==============================================================================
// PianoKeyboard
//==============================================================================
constexpr int   PianoKeyboard::kWhiteNotes[7];
constexpr int   PianoKeyboard::kBlackNotes[5];
constexpr float PianoKeyboard::kBlackKeyOffsets[5];

PianoKeyboard::PianoKeyboard(PitchControlAudioProcessor& p) : m_processor(p) {}

void PianoKeyboard::resized()
{
    m_whiteKeyWidth = (float)getWidth() / (float)kNumWhite;
    m_whiteKeyHeight = (float)getHeight();
    m_blackKeyWidth = m_whiteKeyWidth * 0.62f;
    m_blackKeyHeight = m_whiteKeyHeight * 0.62f;
}

float PianoKeyboard::blackKeyCentreX(int b) const noexcept
{
    return kBlackKeyOffsets[b] * m_whiteKeyWidth;
}

bool PianoKeyboard::hitTestBlack(int b, int x, int y) const noexcept
{
    float cx = blackKeyCentreX(b);
    return x >= (cx - m_blackKeyWidth * 0.5f) && x < (cx + m_blackKeyWidth * 0.5f) && y < m_blackKeyHeight;
}

int PianoKeyboard::getNoteAtPosition(int x, int y) const
{
    for (int b = 0; b < kNumBlack; ++b) if (hitTestBlack(b, x, y)) return kBlackNotes[b];
    int w = (int)((float)x / m_whiteKeyWidth);
    if (w >= 0 && w < kNumWhite) return kWhiteNotes[w];
    return -1;
}

void PianoKeyboard::mouseDown(const juce::MouseEvent& e)
{
    int note = getNoteAtPosition(e.x, e.y);
    if (note < 0) return;
    auto* param = dynamic_cast<juce::AudioParameterBool*>(
        m_processor.apvts.getParameter(PitchControlAudioProcessor::noteActiveParamID(note)));
    if (param) param->setValueNotifyingHost(param->get() ? 0.0f : 1.0f);
    repaint();
}

void PianoKeyboard::paint(juce::Graphics& g)
{
    const bool fftMode = m_processor.apvts.getRawParameterValue(
        PitchControlAudioProcessor::fftModeParamID())->load() > 0.5f;

    bool active[kNumNotes]{};
    for (int i = 0; i < kNumNotes; ++i)
        active[i] = m_processor.apvts.getRawParameterValue(
            PitchControlAudioProcessor::noteActiveParamID(i))->load() > 0.5f;

    const float gap = 1.5f, cornerR = 4.0f;
    juce::Colour wAct = fftMode ? Colors::fftAccent : Colors::whiteKeyActive;
    juce::Colour bAct = fftMode ? Colors::fftActive : Colors::blackKeyActive;

    for (int w = 0; w < kNumWhite; ++w)
    {
        int note = kWhiteNotes[w]; bool on = active[note];
        float kx = w * m_whiteKeyWidth + gap * 0.5f, kw = m_whiteKeyWidth - gap, kh = m_whiteKeyHeight - gap;
        juce::ColourGradient grad((on ? wAct : Colors::whiteKey).brighter(0.05f), kx, 0,
            (on ? wAct : Colors::whiteKey).darker(0.12f), kx, kh, false);
        g.setGradientFill(grad); g.fillRoundedRectangle(kx, gap * 0.5f, kw, kh, cornerR);
        g.setColour(on ? wAct : juce::Colours::grey.withAlpha(0.4f));
        g.drawRoundedRectangle(kx, gap * 0.5f, kw, kh, cornerR, 1.5f);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.setColour(on ? Colors::background : Colors::textDim);
        g.drawText(kNoteNames[note], (int)kx, (int)(kh - 20), (int)kw, 16, juce::Justification::centred, false);
    }

    for (int b = 0; b < kNumBlack; ++b)
    {
        int note = kBlackNotes[b]; bool on = active[note];
        float bx = blackKeyCentreX(b) - m_blackKeyWidth * 0.5f, bw = m_blackKeyWidth, bh = m_blackKeyHeight;
        juce::ColourGradient grad((on ? bAct : Colors::blackKey).brighter(0.15f), bx, 0,
            (on ? bAct : Colors::blackKey).darker(0.3f), bx, bh, false);
        g.setGradientFill(grad); g.fillRoundedRectangle(bx, 0, bw, bh, 3.0f);
        g.setColour(on ? bAct.brighter(0.3f) : Colors::accent.withAlpha(0.6f));
        g.drawRoundedRectangle(bx, 0, bw, bh, 3.0f, 1.5f);
    }


}

//==============================================================================
// LabelledKnob
//==============================================================================
LabelledKnob::LabelledKnob(const juce::String& labelText, const juce::String& paramID,
    PitchControlAudioProcessor& processor, juce::LookAndFeel* laf)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, Colors::textBright);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    if (laf) slider.setLookAndFeel(laf);

    label.setText(labelText, juce::dontSendNotification);
    label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, Colors::textBright);
    label.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(slider); addAndMakeVisible(label);

    m_attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, slider);
}

void LabelledKnob::setLabelText(const juce::String& text) { label.setText(text, juce::dontSendNotification); }
void LabelledKnob::resized() { auto a = getLocalBounds(); label.setBounds(a.removeFromTop(18)); slider.setBounds(a); }

//==============================================================================
// NoteRangeSelector
//==============================================================================
NoteRangeSelector::NoteRangeSelector(const juce::String& labelText, const juce::String& paramID,
    PitchControlAudioProcessor& processor)
{
    m_label.setText(labelText, juce::dontSendNotification);
    m_label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    m_label.setColour(juce::Label::textColourId, Colors::textBright);
    m_label.setJustificationType(juce::Justification::centred);

    for (int n = 0; n < kTotalNotes; ++n)
        m_combo.addItem(kNoteNames[n % kNumNotes] + juce::String(n / kNumNotes), n + 1);
    m_combo.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_label); addAndMakeVisible(m_combo);

    m_attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, paramID, m_combo);
}

void NoteRangeSelector::resized()
{
    auto a = getLocalBounds(); m_label.setBounds(a.removeFromTop(18)); m_combo.setBounds(a.reduced(2, 2));
}

//==============================================================================
// PitchControlAudioProcessorEditor
//==============================================================================
PitchControlAudioProcessorEditor::PitchControlAudioProcessorEditor(PitchControlAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    m_spectrumView(p),
    m_keyboard(p),
    m_depthKnob("Depth (dB)", PitchControlAudioProcessor::depthParamID(), p, &m_laf),
    m_qKnob("Q", PitchControlAudioProcessor::qParamID(), p, &m_laf),
    m_boostKnob("Boost (dB)", PitchControlAudioProcessor::boostDBParamID(), p, &m_laf),
    m_boostQKnob("Boost Q", PitchControlAudioProcessor::boostQParamID(), p, &m_laf),
    m_shiftStrengthKnob("Shift Strength", PitchControlAudioProcessor::shiftStrengthParamID(), p, &m_laf),
    m_rangeFrom("Range From", PitchControlAudioProcessor::rangeFromParamID(), p),
    m_rangeTo("Range To", PitchControlAudioProcessor::rangeToParamID(), p)
{
    setLookAndFeel(&m_laf);

    m_titleLabel.setText("PitchControl  |  If Q is high, lower Depth!", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    m_titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_titleLabel);

    addAndMakeVisible(m_spectrumView);
    addAndMakeVisible(m_keyboard);
    addAndMakeVisible(m_depthKnob);
    addAndMakeVisible(m_qKnob);
    addAndMakeVisible(m_boostKnob);
    addAndMakeVisible(m_boostQKnob);
    addAndMakeVisible(m_shiftStrengthKnob);
    addAndMakeVisible(m_rangeFrom);
    addAndMakeVisible(m_rangeTo);

    m_modeButton.setClickingTogglesState(true);
    m_modeButton.setColour(juce::TextButton::buttonOnColourId, Colors::fftAccent);
    m_modeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    m_modeButton.setColour(juce::TextButton::buttonColourId, Colors::accentBright);
    m_modeButton.setColour(juce::TextButton::textColourOffId, Colors::textBright);
    m_modeButton.onClick = [this] {
        bool fftOn = audioProcessor.apvts.getRawParameterValue(
            PitchControlAudioProcessor::fftModeParamID())->load() > 0.5f;
        m_modeButton.setButtonText(fftOn ? "FFT Mode" : "Filter Mode");
        updateModeUI(fftOn);
        };
    addAndMakeVisible(m_modeButton);
    m_modeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, PitchControlAudioProcessor::fftModeParamID(), m_modeButton);

    m_wetOnlyButton.setClickingTogglesState(true);
    m_wetOnlyButton.setColour(juce::TextButton::buttonOnColourId, Colors::activeKey);
    m_wetOnlyButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(m_wetOnlyButton);
    m_wetOnlyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, PitchControlAudioProcessor::wetOnlyParamID(), m_wetOnlyButton);

    m_dampenButton.setClickingTogglesState(true);
    m_dampenButton.setColour(juce::TextButton::buttonOnColourId, Colors::fftAccent);
    m_dampenButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(m_dampenButton);
    m_dampenAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, PitchControlAudioProcessor::dampenUnprotectedParamID(), m_dampenButton);

    {
        bool fftOn = audioProcessor.apvts.getRawParameterValue(
            PitchControlAudioProcessor::fftModeParamID())->load() > 0.5f;
        m_modeButton.setButtonText(fftOn ? "FFT Mode" : "Filter Mode");
        updateModeUI(fftOn);
    }

    setSize(660, 560);
    setResizable(false, false);
    startTimerHz(30);
}

PitchControlAudioProcessorEditor::~PitchControlAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void PitchControlAudioProcessorEditor::updateModeUI(bool fftMode)
{
    if (fftMode)
    {
        m_depthKnob.setLabelText("INACTIVE");
        m_qKnob.setLabelText("Attract. Shape");
        m_boostQKnob.setVisible(false);
        m_shiftStrengthKnob.setVisible(true);
        m_wetOnlyButton.setVisible(false);
        m_dampenButton.setVisible(true);
    }
    else
    {
        m_depthKnob.setLabelText("Depth (dB)");
        m_qKnob.setLabelText("Q");
        m_boostQKnob.setVisible(true);
        m_shiftStrengthKnob.setVisible(false);
        m_wetOnlyButton.setVisible(true);
        m_dampenButton.setVisible(false);
    }
    m_keyboard.repaint();
    repaint();
}

//==============================================================================
void PitchControlAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();
    const bool fftMode = audioProcessor.apvts.getRawParameterValue(
        PitchControlAudioProcessor::fftModeParamID())->load() > 0.5f;

    juce::Colour bgTop = fftMode ? juce::Colour(0xff226655) : Colors::background;
    juce::Colour bgBot = fftMode ? juce::Colour(0xff443300) : Colors::panelBg.darker(0.3f);

    juce::ColourGradient bg(bgTop, 0, 0, bgBot, 0, (float)H, false);
    g.setGradientFill(bg); g.fillAll();

    {
        juce::Colour gc = fftMode ? juce::Colour(0xffffaa00) : juce::Colour(0xff00ffff);
        juce::DropShadow s1(gc.withAlpha(0.3f), 80, { 0,0 }), s2(gc.withAlpha(0.2f), 120, { 0,0 });
        juce::Path c1, c2;
        c1.addEllipse(80, 240, 160, 160); c2.addEllipse(420, 300, 200, 200);
        s1.drawForPath(g, c1); s2.drawForPath(g, c2);
    }

    // Keyboard panel shadow
    juce::Rectangle<float> kb(10, 196, W - 20, 180);
    g.setColour(Colors::accent.withAlpha(0.4f)); g.fillRoundedRectangle(kb.expanded(4), 10);
    g.setColour((fftMode ? Colors::fftAccent : Colors::borderColor).withAlpha(0.5f));
    g.drawRoundedRectangle(kb.expanded(4), 10, 1.5f);

    // Controls panel shadow
    juce::Rectangle<float> ctrl(10, 388, W - 20, 155);
    g.setColour(Colors::panelBg.withAlpha(0.7f)); g.fillRoundedRectangle(ctrl, 8);
    g.setColour((fftMode ? Colors::fftAccent : Colors::borderColor).withAlpha(0.35f));
    g.drawRoundedRectangle(ctrl, 8, 1.0f);


}

//==============================================================================
void PitchControlAudioProcessorEditor::resized()
{
    const int margin = 14;
    const int W = getWidth() - 2 * margin;

    // Title strip
    m_titleLabel.setBounds(margin, 4, W, 24);

    // Spectrum view — occupies the top section below the title
    const int specH = 155;
    m_spectrumView.setBounds(margin, 30, W, specH);

    // Piano keyboard
    m_keyboard.setBounds(margin, 198, W, 172);

    // Controls row
    const int ctrlY = 394;
    const int ctrlH = 135;
    const int knobW = 95;
    const int gap = 10;
    const int stackW = 140;

    const int totalKnobsW = knobW * 4 + gap * 3;
    const int totalW = totalKnobsW + gap + stackW;
    const int startX = margin + (W - totalW) / 2;

    m_depthKnob.setBounds(startX, ctrlY, knobW, ctrlH);
    m_qKnob.setBounds(startX + (knobW + gap), ctrlY, knobW, ctrlH);
    m_boostKnob.setBounds(startX + 2 * (knobW + gap), ctrlY, knobW, ctrlH);
    m_boostQKnob.setBounds(startX + 3 * (knobW + gap), ctrlY, knobW, ctrlH);
    m_shiftStrengthKnob.setBounds(startX + 3 * (knobW + gap), ctrlY, knobW, ctrlH);

    const int stackX = startX + totalKnobsW + gap;
    const int itemH = 44;   // label(18) + combo(24) + 2 padding
    const int stackGap = 4;
    const int stackY = ctrlY + 5;

    // Row 0: Range From and Range To side by side
    const int halfW = (stackW - stackGap) / 2;
    m_rangeFrom.setBounds(stackX, stackY, halfW, itemH);
    m_rangeTo.setBounds(stackX + halfW + stackGap, stackY, halfW, itemH);

    // Row 1: Wet Only (Filter mode) or Dampen (FFT mode)
    m_wetOnlyButton.setBounds(stackX, stackY + (itemH + stackGap), stackW, itemH - 15);
    m_dampenButton.setBounds(stackX, stackY + (itemH + stackGap), stackW, itemH - 15);

    // Row 2: Mode toggle
    m_modeButton.setBounds(stackX, stackY + 2 * (itemH + stackGap) - 15, stackW, itemH - 15);
}