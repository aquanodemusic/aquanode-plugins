#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectralStereoizeAudioProcessorEditor::SpectralStereoizeAudioProcessorEditor (SpectralStereoizeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (960, 540);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);

    // Prepare spectrum buffers (size will be updated in timerCallback)
    mainSpectrum.assign (audioProcessor.maxBins, 0.0f);
    scSpectrum.assign   (audioProcessor.maxBins, 0.0f);

    // FFT size selector
    fftSizeLabel.setText("FFT Size:", juce::dontSendNotification);
    fftSizeLabel.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    fftSizeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(fftSizeLabel);

    // Add all items with unique IDs
    fftSizeCombo.addItem("32", 5);
    fftSizeCombo.addItem("128", 6);
    fftSizeCombo.addItem("256", 7);
    fftSizeCombo.addItem("512", 8);
    fftSizeCombo.addItem("1024", 1);
    fftSizeCombo.addItem("2048", 2);
    fftSizeCombo.addItem("4096", 3);
    fftSizeCombo.addItem("8192", 4);

    // Select the correct initial ID based on the processor's current state
    int currentSize = audioProcessor.fftSize;
    if (currentSize == 32)   fftSizeCombo.setSelectedId(5);
    else if (currentSize == 128)  fftSizeCombo.setSelectedId(6);
    else if (currentSize == 256)  fftSizeCombo.setSelectedId(7);
    else if (currentSize == 512)  fftSizeCombo.setSelectedId(8);
    else if (currentSize == 1024) fftSizeCombo.setSelectedId(1);
    else if (currentSize == 2048) fftSizeCombo.setSelectedId(2);
    else if (currentSize == 4096) fftSizeCombo.setSelectedId(3);
    else if (currentSize == 8192) fftSizeCombo.setSelectedId(4);

    // Update the processor when the combo box changes
    fftSizeCombo.onChange = [this]
        {
            int newSize = 2048;
            switch (fftSizeCombo.getSelectedId())
            {
            case 1:  newSize = 1024; break;
            case 2:  newSize = 2048; break;
            case 3:  newSize = 4096; break;
            case 4:  newSize = 8192; break;
            case 5:  newSize = 32;   break;
            case 6:  newSize = 128;  break;
            case 7:  newSize = 256;  break;
            case 8:  newSize = 512;  break;
            }
            audioProcessor.setFFTSize(newSize);
        };
    addAndMakeVisible(fftSizeCombo);

    visualScaleSlider.setSliderStyle(juce::Slider::LinearVertical);
    visualScaleSlider.setRange(0.001, 1.0, 0.05);
    visualScaleSlider.setValue(audioProcessor.visualScale);
    visualScaleSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    visualScaleSlider.onValueChange = [this] {
        audioProcessor.visualScale = (float)visualScaleSlider.getValue();
        };
    visualScaleSlider.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    addAndMakeVisible(visualScaleSlider);

    visualScaleLabel.setText("Gain", juce::dontSendNotification);
    visualScaleLabel.setJustificationType(juce::Justification::centred);
    visualScaleLabel.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    addAndMakeVisible(visualScaleLabel);

    startTimerHz (30);  // 30 fps updates
}

SpectralStereoizeAudioProcessorEditor::~SpectralStereoizeAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SpectralStereoizeAudioProcessorEditor::timerCallback()
{
    // Pull width curve from processor
    audioProcessor.getWidthCurve (widthCurve);

    // Pull spectrum data (processor uses its own smoothing)
    audioProcessor.getMainSpectrumData      (mainSpectrum.data(), audioProcessor.numBins);
    audioProcessor.getSidechainSpectrumData (scSpectrum.data(),   audioProcessor.numBins);

    repaint();
}

//==============================================================================
// Coordinate helpers
//==============================================================================

float SpectralStereoizeAudioProcessorEditor::xToFreq (float x) const
{
    const float nyquist = static_cast<float> (audioProcessor.currentSampleRate) * 0.5f;
    const float logMin  = std::log10 (20.0f);
    const float logMax  = std::log10 (nyquist);

    float w = static_cast<float> (getWidth());
    float norm = juce::jlimit (0.0f, 1.0f, x / w);
    float logFreq = logMin + norm * (logMax - logMin);
    return std::pow (10.0f, logFreq);
}

float SpectralStereoizeAudioProcessorEditor::freqToX (float freq) const
{
    const float nyquist = static_cast<float> (audioProcessor.currentSampleRate) * 0.5f;
    freq = juce::jlimit (20.0f, nyquist, freq);

    const float logMin = std::log10 (20.0f);
    const float logMax = std::log10 (nyquist);
    float norm = (std::log10 (freq) - logMin) / (logMax - logMin);
    return norm * static_cast<float> (getWidth());
}

float SpectralStereoizeAudioProcessorEditor::yToWidth (float y) const
{
    float h = static_cast<float> (getHeight());
    float norm = 1.0f - juce::jlimit (0.0f, 1.0f, y / h);
    // Y = top (0) -> width = 2.0, Y = bottom (height) -> width = 0.0
    return norm * 2.0f;
}

float SpectralStereoizeAudioProcessorEditor::widthToY (float width) const
{
    float h = static_cast<float> (getHeight());
    float norm = juce::jlimit (0.0f, 1.0f, width / 2.0f);
    return h * (1.0f - norm);
}

//==============================================================================
// Mouse interaction
//==============================================================================

void SpectralStereoizeAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    isDragging = true;
    lastDragX = static_cast<float> (e.x);
    lastDragY = static_cast<float> (e.y);

    float freq = xToFreq (lastDragX);
    int bin = static_cast<int> (freq / (audioProcessor.currentSampleRate * 0.5f) *
                                (audioProcessor.numBins - 1));
    bin = juce::jlimit (0, audioProcessor.numBins - 1, bin);
    float val = yToWidth (lastDragY);
    audioProcessor.setWidthCurveRange (bin, bin, val, val);
}

void SpectralStereoizeAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDragging) return;

    float x = static_cast<float> (e.x);
    float y = static_cast<float> (e.y);

    float startFreq = xToFreq (lastDragX);
    float endFreq   = xToFreq (x);
    float startVal  = yToWidth (lastDragY);
    float endVal    = yToWidth (y);

    int startBin = static_cast<int> (startFreq / (audioProcessor.currentSampleRate * 0.5f) *
                                     (audioProcessor.numBins - 1));
    int endBin   = static_cast<int> (endFreq   / (audioProcessor.currentSampleRate * 0.5f) *
                                     (audioProcessor.numBins - 1));

    startBin = juce::jlimit (0, audioProcessor.numBins - 1, startBin);
    endBin   = juce::jlimit (0, audioProcessor.numBins - 1, endBin);

    audioProcessor.setWidthCurveRange (startBin, endBin, startVal, endVal);

    lastDragX = x;
    lastDragY = y;
}

void SpectralStereoizeAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    isDragging = false;
}

void SpectralStereoizeAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    hoverX = static_cast<float> (e.x);
}

void SpectralStereoizeAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent&)
{
    audioProcessor.resetWidthCurve();
}

//==============================================================================
// Drawing
//==============================================================================

void SpectralStereoizeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
    drawGrid (g);
    drawSpectrum (g);
    drawWidthCurve (g);
    if (isMouseOver(true))
    {
        auto mousePos = getMouseXYRelative();
        float mouseX = static_cast<float>(mousePos.getX());
        float mouseY = static_cast<float>(mousePos.getY());

        // Frequenz an der Mausposition berechnen
        float freq = xToFreq(mouseX);

        // Den entsprechenden Bin und den Width-Wert holen
        int bin = static_cast<int>(freq / (audioProcessor.currentSampleRate * 0.5f) * (audioProcessor.numBins - 1));
        bin = juce::jlimit(0, audioProcessor.numBins - 1, bin);

        // Wir holen den Wert direkt aus der Kurve des Processors
        float width = audioProcessor.widthCurve_read[bin];

        juce::String freqStr = (freq < 1000.0f) ? juce::String(static_cast<int>(freq)) + " Hz"
            : juce::String(freq / 1000.0f, 1) + " kHz";
        juce::String widthStr = juce::String(width, 2);
        juce::String tooltip = freqStr + "  |  width = " + widthStr;

        // Positionierung der Box: Immer 20 Pixel über dem Mauszeiger
        float labelX = mouseX + 12.0f;
        float labelY = mouseY - 25.0f; // Schwebt über dem Cursor

        // Verhindern, dass die Box aus dem Fenster rutscht
        if (labelX + 150.0f > getWidth())  labelX = mouseX - 160.0f;
        if (labelY < 5.0f)                 labelY = mouseY + 20.0f;

        // Box zeichnen
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillRoundedRectangle(labelX - 4.0f, labelY - 2.0f, 156.0f, 18.0f, 4.0f);

        g.setColour(juce::Colours::white);
        g.setFont(11.0f);
        g.drawText(tooltip, static_cast<int>(labelX), static_cast<int>(labelY), 150, 15, juce::Justification::centredLeft);
    }
}

void SpectralStereoizeAudioProcessorEditor::drawGrid (juce::Graphics& g)
{
    const float nyquist = static_cast<float> (audioProcessor.currentSampleRate) * 0.5f;
    const float w = static_cast<float> (getWidth());
    const float h = static_cast<float> (getHeight());

    // Light grey lines
    g.setColour (juce::Colours::lightgrey);

    // Horizontal lines for width values (0.0, 0.5, 1.0, 1.5, 2.0)
    const float widths[] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f };
    for (float val : widths)
    {
        float y = widthToY (val);
        g.drawHorizontalLine (static_cast<int> (y), 0.0f, w);
        g.setFont (10.0f);
        g.drawText (juce::String (val, 1), w - 36.0f, y - 6.0f, 34, 14,
                    juce::Justification::right);
    }

    // Vertical lines for standard frequencies (log spaced)
    const float freqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                            1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    for (float f : freqs)
    {
        if (f > nyquist) continue;
        float x = freqToX (f);
        g.drawVerticalLine (static_cast<int> (x), 0.0f, h);
        g.setFont (10.0f);
        juce::String label = (f < 1000.0f) ? juce::String (static_cast<int> (f))
                                           : juce::String (f / 1000.0f, 1) + "k";
        g.drawText (label, static_cast<int> (x) - 16, static_cast<int> (h) - 14, 32, 12,
                    juce::Justification::centred);
    }
}

void SpectralStereoizeAudioProcessorEditor::drawSpectrum(juce::Graphics& g)
{
    const float h = static_cast<float>(getHeight());
    const float centerY = h * 0.5f;

    // Wir lassen rechts 60 Pixel Platz für den Visual Scale Slider
    const float mainWidth = static_cast<float> (getWidth() - 60);

    // Lambda-Funktion zum Zeichnen eines symmetrischen Pfades
    auto drawButterflyPath = [&](const std::vector<float>& data, juce::Colour col)
        {
            juce::Path path;
            bool started = false;

            // 1. Obere Hälfte des Pfades (von links nach rechts)
            for (int bin = 0; bin < audioProcessor.numBins; ++bin)
            {
                float freq = static_cast<float>(bin) * (audioProcessor.currentSampleRate * 0.5f) /
                    (audioProcessor.numBins - 1);

                if (freq < 20.0f) continue;

                float x = freqToX(freq);

                // Stoppen, wenn wir den Bereich des Sliders erreichen
                if (x > mainWidth) break;

                float mag = data[bin];

                // Berechnung der vertikalen Höhe mit dem visualScale Multiplier
                // 2.5f ist der Basis-Boost, h * 0.45f begrenzt den Ausschlag auf fast die halbe Höhe
                float normMag = juce::jlimit(0.0f, 1.0f, mag * 2.5f * audioProcessor.visualScale);
                float y = centerY - (normMag * h * 0.45f);

                if (!started) {
                    path.startNewSubPath(x, y);
                    started = true;
                }
                else {
                    path.lineTo(x, y);
                }
            }

            // 2. Untere Hälfte des Pfades (von rechts nach links zurück spiegeln)
            for (int bin = audioProcessor.numBins - 1; bin >= 0; --bin)
            {
                float freq = static_cast<float> (bin) * (audioProcessor.currentSampleRate * 0.5f) /
                    (audioProcessor.numBins - 1);

                if (freq < 20.0f) continue;

                float x = freqToX(freq);
                if (x > mainWidth) continue;

                float mag = data[bin];
                float normMag = juce::jlimit(0.0f, 1.0f, mag * 2.5f * audioProcessor.visualScale);
                float y = centerY + (normMag * h * 0.45f); // Spiegelung nach unten

                path.lineTo(x, y);
            }

            if (started)
            {
                path.closeSubPath();

                // Fläche füllen (transparent)
                g.setColour(col.withAlpha(0.2f));
                g.fillPath(path);

                // Umrandung zeichnen
                g.setColour(col);
                g.strokePath(path, juce::PathStrokeType(1.2f));
            }
        };

    // --- Zeichnen ---

    // 1. Pinke Kurve (Sidechain / Referenz) im Hintergrund
    drawButterflyPath(scSpectrum, juce::Colours::pink);

    // 2. Grüne Kurve (Output / Processed Stereo) im Vordergrund
    drawButterflyPath(mainSpectrum, juce::Colours::green);

    // 3. Mittellinie (Horizont) zeichnen
    g.setColour(juce::Colours::grey.withAlpha(0.3f));
    g.drawHorizontalLine(static_cast<int> (centerY), 0.0f, mainWidth);
}

void SpectralStereoizeAudioProcessorEditor::drawWidthCurve (juce::Graphics& g)
{
    const float w = static_cast<float> (getWidth());
    const float h = static_cast<float> (getHeight());

    // Draw filled area between curve and 1.0 (unity)
    juce::Path fillPath;
    bool started = false;
    for (int bin = 0; bin < audioProcessor.numBins; ++bin)
    {
        float freq = static_cast<float> (bin) * (audioProcessor.currentSampleRate * 0.5f) /
                     (audioProcessor.numBins - 1);
        if (freq < 20.0f) continue;

        float x = freqToX (freq);
        float widthVal = widthCurve[bin];
        float y = widthToY (widthVal);
        float oneY = widthToY (1.0f);

        if (! started)
        {
            fillPath.startNewSubPath (x, oneY);
            fillPath.lineTo (x, y);
            started = true;
        }
        else
        {
            fillPath.lineTo (x, y);
        }
    }
    if (started)
    {
        fillPath.lineTo (freqToX (audioProcessor.currentSampleRate * 0.5f), widthToY (1.0f));
        fillPath.closeSubPath();
    }
    g.setColour (juce::Colours::cyan.withAlpha (0.15f));
    g.fillPath (fillPath);

    // Draw the curve line
    juce::Path curvePath;
    started = false;
    for (int bin = 0; bin < audioProcessor.numBins; ++bin)
    {
        float freq = static_cast<float> (bin) * (audioProcessor.currentSampleRate * 0.5f) /
                     (audioProcessor.numBins - 1);
        if (freq < 20.0f) continue;

        float x = freqToX (freq);
        float y = widthToY (widthCurve[bin]);

        if (! started)
        {
            curvePath.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            curvePath.lineTo (x, y);
        }
    }
    g.setColour (juce::Colours::cyan);
    g.strokePath (curvePath, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
}

void SpectralStereoizeAudioProcessorEditor::drawTooltip (juce::Graphics& g, float mouseX, float curveY)
{
    float freq = xToFreq (mouseX);
    int bin = static_cast<int> (freq / (audioProcessor.currentSampleRate * 0.5f) *
                                (audioProcessor.numBins - 1));
    bin = juce::jlimit (0, audioProcessor.numBins - 1, bin);
    float width = widthCurve[bin];

    juce::String freqStr = (freq < 1000.0f) ? juce::String (static_cast<int> (freq)) + " Hz"
                                            : juce::String (freq / 1000.0f, 1) + " kHz";
    juce::String widthStr = juce::String (width, 2);

    juce::String tooltip = freqStr + "  |  width = " + widthStr;

    float labelX = mouseX + 12.0f;
    float labelY = curveY - 16.0f;
    if (labelX + 150.0f > getWidth())  labelX = mouseX - 162.0f;
    if (labelY < 4.0f)                 labelY = curveY + 8.0f;

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (labelX - 4.0f, labelY - 2.0f, 156.0f, 18.0f, 4.0f);
    g.setColour (juce::Colours::white);
    g.setFont (11.0f);
    g.drawText (tooltip, static_cast<int> (labelX), static_cast<int> (labelY),
                150, 16, juce::Justification::left);
}

//==============================================================================
void SpectralStereoizeAudioProcessorEditor::resized()
{
    int w = getWidth();
    int h = getHeight();

    // Platz für Slider rechts reservieren (ca. 60 Pixel)
    int sliderWidth = 60;
    int mainAreaRight = w - sliderWidth;

    // Bestehende FFT Controls oben rechts (im neuen Bereich)
    fftSizeLabel.setBounds(mainAreaRight - 120, 10, 70, 24);
    fftSizeCombo.setBounds(mainAreaRight - 40, 10, 80, 24);

    // Der neue Visual Gain Slider ganz rechts
    visualScaleLabel.setBounds(mainAreaRight, h / 2 + 50, sliderWidth, 20);
    visualScaleSlider.setBounds(mainAreaRight, h / 2 + 80, sliderWidth, 180);
}