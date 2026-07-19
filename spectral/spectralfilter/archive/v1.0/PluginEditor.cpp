#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectralFilterAudioProcessorEditor::SpectralFilterAudioProcessorEditor(SpectralFilterAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(860, 480);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);

    displayCurveDB.fill(0.0f);
    fftDisplayData.fill(0.0f);

    // FFT size selector
    fftSizeLabel.setText("FFT Size:", juce::dontSendNotification);
    fftSizeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fftSizeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(fftSizeLabel);

    fftSizeCombo.addItem("1024", 1);
    fftSizeCombo.addItem("2048", 2);
    fftSizeCombo.addItem("4096", 3);
    fftSizeCombo.addItem("8192", 4);

    // Set current selection based on processor's FFT size
    int currentSize = audioProcessor.fftSize;
    if (currentSize == 1024) fftSizeCombo.setSelectedId(1, juce::dontSendNotification);
    else if (currentSize == 2048) fftSizeCombo.setSelectedId(2, juce::dontSendNotification);
    else if (currentSize == 4096) fftSizeCombo.setSelectedId(3, juce::dontSendNotification);
    else if (currentSize == 8192) fftSizeCombo.setSelectedId(4, juce::dontSendNotification);

    fftSizeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    fftSizeCombo.setColour(juce::ComboBox::textColourId, juce::Colours::lightgrey);
    fftSizeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff444444));
    fftSizeCombo.onChange = [this]()
        {
            int selectedId = fftSizeCombo.getSelectedId();
            int newSize = 2048;
            if (selectedId == 1) newSize = 1024;
            else if (selectedId == 2) newSize = 2048;
            else if (selectedId == 3) newSize = 4096;
            else if (selectedId == 4) newSize = 8192;

            audioProcessor.setFFTSize(newSize);
        };
    addAndMakeVisible(fftSizeCombo);

    // Background color input
    bgColorLabel.setText("BG:", juce::dontSendNotification);
    bgColorLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    bgColorLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bgColorLabel);

    bgColorInput.setText(audioProcessor.getBackgroundColor().toDisplayString(false));
    bgColorInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    bgColorInput.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    bgColorInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff444444));
    bgColorInput.setFont(11.0f);
    bgColorInput.onReturnKey = [this]() { updateBackgroundColor(); };
    bgColorInput.onFocusLost = [this]() { updateBackgroundColor(); };
    addAndMakeVisible(bgColorInput);

    // Curve color input
    curveColorLabel.setText("Curve:", juce::dontSendNotification);
    curveColorLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    curveColorLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(curveColorLabel);

    curveColorInput.setText(audioProcessor.getCurveColor().toDisplayString(false));
    curveColorInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    curveColorInput.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    curveColorInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff444444));
    curveColorInput.setFont(11.0f);
    curveColorInput.onReturnKey = [this]() { updateCurveColor(); };
    curveColorInput.onFocusLost = [this]() { updateCurveColor(); };
    addAndMakeVisible(curveColorInput);

    // Grid color input
    gridColorLabel.setText("Grid:", juce::dontSendNotification);
    gridColorLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    gridColorLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(gridColorLabel);

    gridColorInput.setText(audioProcessor.getGridColor().toDisplayString(false));
    gridColorInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    gridColorInput.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    gridColorInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff444444));
    gridColorInput.setFont(11.0f);
    gridColorInput.onReturnKey = [this]() { updateGridColor(); };
    gridColorInput.onFocusLost = [this]() { updateGridColor(); };
    addAndMakeVisible(gridColorInput);

    // Spectrum color input
    spectrumColorLabel.setText("Spectrum:", juce::dontSendNotification);
    spectrumColorLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    spectrumColorLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(spectrumColorLabel);

    spectrumColorInput.setText(audioProcessor.getSpectrumColor().toDisplayString(false));
    spectrumColorInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
    spectrumColorInput.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    spectrumColorInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff444444));
    spectrumColorInput.setFont(11.0f);
    spectrumColorInput.onReturnKey = [this]() { updateSpectrumColor(); };
    spectrumColorInput.onFocusLost = [this]() { updateSpectrumColor(); };
    addAndMakeVisible(spectrumColorInput);

    // Randomizer button
    randomButton.setButtonText("Random");
    randomButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    randomButton.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    randomButton.onClick = [this]() { audioProcessor.randomizeFilterCurve(); };
    addAndMakeVisible(randomButton);

    // Export IR button
    exportIRButton.setButtonText("Export IR");
    exportIRButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
    exportIRButton.setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    exportIRButton.onClick = [this]()
        {
            auto chooser = std::make_shared<juce::FileChooser>("Save Impulse Response",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                "*.wav");

            auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

            chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file != juce::File{})
                    {
                        audioProcessor.exportImpulseResponse(file);
                    }
                });
        };
    addAndMakeVisible(exportIRButton);

    startTimerHz(60);
}

SpectralFilterAudioProcessorEditor::~SpectralFilterAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SpectralFilterAudioProcessorEditor::timerCallback()
{
    // Pull fresh curve and spectrum data from the processor — no lock held during paint
    audioProcessor.getFilterCurve(displayCurveDB);
    audioProcessor.getFFTData(fftDisplayData.data(), audioProcessor.numBins);
    repaint();
}

//==============================================================================
// Coordinate helpers
//==============================================================================

int SpectralFilterAudioProcessorEditor::xToBin(float x) const
{
    const float kNyquist = nyquist();
    const float kLogMin = std::log10(20.0f);
    const float kLogMax = std::log10(kNyquist);

    float w = static_cast<float>(getWidth());
    float normalized = juce::jlimit(0.0f, 1.0f, x / w);
    float logFreq = kLogMin + normalized * (kLogMax - kLogMin);
    float freq = std::pow(10.0f, logFreq);

    float freqPerBin = kNyquist / static_cast<float>(audioProcessor.numBins - 1);
    int bin = static_cast<int>(freq / freqPerBin);
    return juce::jlimit(0, audioProcessor.numBins - 1, bin);
}

float SpectralFilterAudioProcessorEditor::binToX(int bin) const
{
    const float kNyquist = nyquist();
    const float kLogMin = std::log10(20.0f);
    const float kLogMax = std::log10(kNyquist);

    float w = static_cast<float>(getWidth());
    float freqPerBin = kNyquist / static_cast<float>(audioProcessor.numBins - 1);
    float freq = static_cast<float>(bin) * freqPerBin;
    freq = juce::jmax(freq, 20.0f);
    float logFreq = std::log10(freq);
    float normalized = (logFreq - kLogMin) / (kLogMax - kLogMin);
    return normalized * w;
}

float SpectralFilterAudioProcessorEditor::yToDB(float y) const
{
    float h = static_cast<float>(getHeight());
    float normalized = 1.0f - juce::jlimit(0.0f, 1.0f, y / h);
    return minDB + normalized * (maxDB - minDB);
}

float SpectralFilterAudioProcessorEditor::dBToY(float dB) const
{
    float h = static_cast<float>(getHeight());
    float normalized = (dB - minDB) / (maxDB - minDB);
    return h * (1.0f - juce::jlimit(0.0f, 1.0f, normalized));
}

//==============================================================================
// Mouse
//==============================================================================

void SpectralFilterAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    isDrawing = true;
    lastDragX = static_cast<float>(event.x);
    lastDragY = static_cast<float>(event.y);
    paintCurveSegment(lastDragX, lastDragY);
}

void SpectralFilterAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (!isDrawing) return;
    float x = static_cast<float>(event.x);
    float y = static_cast<float>(event.y);
    paintCurveSegment(x, y);
    lastDragX = x;
    lastDragY = y;
}

void SpectralFilterAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    isDrawing = false;
}

void SpectralFilterAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    hoverX = static_cast<float>(event.x);
}

void SpectralFilterAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent&)
{
    audioProcessor.resetFilterCurve();
}

void SpectralFilterAudioProcessorEditor::paintCurveSegment(float x, float y)
{
    int   endBin = xToBin(x);
    float endDB = yToDB(y);
    int   startBin = xToBin(lastDragX);
    float startDB = yToDB(lastDragY);
    audioProcessor.setFilterCurveRange(startBin, endBin, startDB, endDB);
}

//==============================================================================
// Paint
//==============================================================================

void SpectralFilterAudioProcessorEditor::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawFFTSpectrum(g);
    drawFilterCurve(g);
    drawLabels(g);
}

void SpectralFilterAudioProcessorEditor::drawBackground(juce::Graphics& g)
{
    const float kNyquist = nyquist();
    const float kLogMin = std::log10(20.0f);
    const float kLogMax = std::log10(kNyquist);
    auto bounds = getLocalBounds();

    g.fillAll(audioProcessor.getBackgroundColor());

    // Horizontal dB grid lines
    const float gridDBs[] = { 24.f, 18.f, 12.f, 6.f, 0.f, -6.f, -12.f, -24.f, -48.f, -96.f };
    for (float dB : gridDBs)
    {
        float y = dBToY(dB);
        g.setColour(dB == 0.0f ? juce::Colour(0xff44aacc) : audioProcessor.getGridColor());
        g.drawHorizontalLine(static_cast<int>(y), 0.f, static_cast<float>(bounds.getWidth()));
    }

    // Vertical frequency grid lines
    const float gridFreqs[] = { 20.f, 50.f, 100.f, 200.f, 500.f,
                                  1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
    for (float freq : gridFreqs)
    {
        if (freq < 20.0f || freq > kNyquist) continue;
        float logF = std::log10(freq);
        float norm = (logF - kLogMin) / (kLogMax - kLogMin);
        float x = norm * static_cast<float>(bounds.getWidth());
        g.setColour(audioProcessor.getGridColor());
        g.drawVerticalLine(static_cast<int>(x), 0.f, static_cast<float>(bounds.getHeight()));
    }
}

void SpectralFilterAudioProcessorEditor::drawFFTSpectrum(juce::Graphics& g)
{
    // fftDisplayData already populated in timerCallback — no lock needed here
    const float kNyquist = nyquist();
    const float kLogMin = std::log10(20.0f);
    const float kLogMax = std::log10(kNyquist);

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());

    float freqPerBin = kNyquist / static_cast<float>(audioProcessor.numBins - 1);

    float maxMag = 0.001f;
    for (int i = 0; i < audioProcessor.numBins; ++i)
        maxMag = juce::jmax(maxMag, fftDisplayData[i]);

    juce::Path specPath;
    bool started = false;

    for (int i = 1; i < audioProcessor.numBins; ++i)
    {
        float freq = static_cast<float>(i) * freqPerBin;
        if (freq < 20.0f) continue;

        float logF = std::log10(freq);
        float norm = (logF - kLogMin) / (kLogMax - kLogMin);
        float x = norm * w;

        float dB = 20.0f * std::log10(fftDisplayData[i] / maxMag + 0.00001f);
        float specNorm = juce::jlimit(0.0f, 1.0f, (dB + 90.0f) / 90.0f);
        float y = h - specNorm * h * 0.65f;

        if (!started)
        {
            specPath.startNewSubPath(x, h);
            specPath.lineTo(x, y);
            started = true;
        }
        else
        {
            specPath.lineTo(x, y);
        }
    }

    if (started)
    {
        specPath.lineTo(w, h);
        specPath.closeSubPath();
    }

    g.setColour(audioProcessor.getSpectrumColor().withAlpha(0.13f));
    g.fillPath(specPath);
    g.setColour(audioProcessor.getSpectrumColor().withAlpha(0.35f));
    g.strokePath(specPath, juce::PathStrokeType(1.0f));
}

void SpectralFilterAudioProcessorEditor::drawFilterCurve(juce::Graphics& g)
{
    const float kNyquist = nyquist();

    // Filled area between the curve and 0 dB
    juce::Path fillPath;
    float zeroY = dBToY(0.0f);
    bool  started = false;

    for (int bin = 0; bin < audioProcessor.numBins; ++bin)
    {
        float x = binToX(bin);
        float y = dBToY(displayCurveDB[bin]);

        if (!started)
        {
            fillPath.startNewSubPath(x, zeroY);
            fillPath.lineTo(x, y);
            started = true;
        }
        else
        {
            fillPath.lineTo(x, y);
        }
    }

    if (started)
    {
        fillPath.lineTo(binToX(audioProcessor.numBins - 1), zeroY);
        fillPath.closeSubPath();
    }

    g.setColour(audioProcessor.getCurveColor().withAlpha(0.2f));
    g.fillPath(fillPath);

    // Curve line
    juce::Path curvePath;
    started = false;

    for (int bin = 0; bin < audioProcessor.numBins; ++bin)
    {
        float x = binToX(bin);
        float y = dBToY(displayCurveDB[bin]);

        if (!started) { curvePath.startNewSubPath(x, y); started = true; }
        else { curvePath.lineTo(x, y); }
    }

    g.setColour(audioProcessor.getCurveColor());
    g.strokePath(curvePath, juce::PathStrokeType(2.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Hover tooltip
    if (hoverX >= 0.f && hoverX <= static_cast<float>(getWidth()))
    {
        int   hoverBin = xToBin(hoverX);
        float hoverDB = displayCurveDB[juce::jlimit(0, audioProcessor.numBins - 1, hoverBin)];
        float hoverY = dBToY(hoverDB);

        // Dot on curve
        g.setColour(juce::Colours::white);
        g.fillEllipse(hoverX - 4.f, hoverY - 4.f, 8.f, 8.f);

        // Frequency value from actual sample rate
        float freqPerBin = kNyquist / static_cast<float>(audioProcessor.numBins - 1);
        float freq = static_cast<float>(hoverBin) * freqPerBin;
        juce::String freqStr = (freq < 1000.f)
            ? juce::String(static_cast<int>(freq)) + " Hz"
            : juce::String(freq / 1000.f, 1) + " kHz";

        juce::String dbStr = (hoverDB <= -144.f) ? "-inf dB"
            : juce::String(hoverDB, 1) + " dB";

        juce::String tooltip = freqStr + "  " + dbStr;

        float labelX = hoverX + 8.f;
        if (labelX + 130.f > static_cast<float>(getWidth()))
            labelX = hoverX - 138.f;
        float labelY = hoverY - 20.f;
        if (labelY < 4.f) labelY = hoverY + 8.f;

        g.setColour(juce::Colour(0xcc111416));
        g.fillRoundedRectangle(labelX - 4.f, labelY - 2.f, 134.f, 18.f, 3.f);
        g.setColour(juce::Colours::white);
        g.setFont(11.0f);
        g.drawText(tooltip, static_cast<int>(labelX), static_cast<int>(labelY),
            130, 16, juce::Justification::left);
    }
}

void SpectralFilterAudioProcessorEditor::drawLabels(juce::Graphics& g)
{
    const float kNyquist = nyquist();
    const float kLogMin = std::log10(20.0f);
    const float kLogMax = std::log10(kNyquist);

    int w = getWidth();
    int h = getHeight();

    g.setFont(10.0f);

    // dB axis labels (right edge)
    const float labelDBs[] = { 24.f, 12.f, 6.f, 0.f, -6.f, -12.f, -24.f, -48.f };
    for (float dB : labelDBs)
    {
        float y = dBToY(dB);
        juce::String label = (dB == 0.f) ? "0 dB"
            : juce::String(static_cast<int>(dB)) + " dB";
        g.setColour(dB == 0.f ? juce::Colour(0xff44aacc) : audioProcessor.getGridColor().brighter(0.3f));
        g.drawText(label, w - 44, static_cast<int>(y) - 7, 42, 14,
            juce::Justification::right);
    }

    // Frequency axis labels (bottom edge)
    const float labelFreqs[] = { 20.f, 50.f, 100.f, 200.f, 500.f,
                                   1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
    for (float freq : labelFreqs)
    {
        if (freq > kNyquist) continue;
        float logF = std::log10(freq);
        float norm = (logF - kLogMin) / (kLogMax - kLogMin);
        float x = norm * static_cast<float>(w);

        juce::String label = (freq < 1000.f)
            ? juce::String(static_cast<int>(freq))
            : (freq < 10000.f ? juce::String(freq / 1000.f, 1) + "k"
                : juce::String(static_cast<int>(freq / 1000.f)) + "k");

        g.setColour(audioProcessor.getGridColor().brighter(0.3f));
        g.drawText(label, static_cast<int>(x) - 18, h - 14, 36, 12,
            juce::Justification::centred);
    }

    // Usage hint
    g.setColour(audioProcessor.getGridColor().brighter(0.1f));
    g.drawText("SpectralFilter by aquanode | Click and Drag to Draw Curve | Click Twice to Reset | Choose UI Colors in the Menu | Click Random for Random Filtering",
        4, 4, w - 180, 14, juce::Justification::left);
}

//==============================================================================
void SpectralFilterAudioProcessorEditor::updateBackgroundColor()
{
    juce::String hexText = bgColorInput.getText().trim();

    // Remove # if present
    if (hexText.startsWith("#"))
        hexText = hexText.substring(1);

    // Parse hex color
    try
    {
        juce::Colour newColor = juce::Colour::fromString("ff" + hexText);
        audioProcessor.setBackgroundColor(newColor);
        repaint();
    }
    catch (...)
    {
        // Invalid color, reset to current
        bgColorInput.setText(audioProcessor.getBackgroundColor().toDisplayString(false));
    }
}

void SpectralFilterAudioProcessorEditor::updateCurveColor()
{
    juce::String hexText = curveColorInput.getText().trim();

    // Remove # if present
    if (hexText.startsWith("#"))
        hexText = hexText.substring(1);

    // Parse hex color
    try
    {
        juce::Colour newColor = juce::Colour::fromString("ff" + hexText);
        audioProcessor.setCurveColor(newColor);
        repaint();
    }
    catch (...)
    {
        // Invalid color, reset to current
        curveColorInput.setText(audioProcessor.getCurveColor().toDisplayString(false));
    }
}

void SpectralFilterAudioProcessorEditor::updateGridColor()
{
    juce::String hexText = gridColorInput.getText().trim();

    // Remove # if present
    if (hexText.startsWith("#"))
        hexText = hexText.substring(1);

    // Parse hex color
    try
    {
        juce::Colour newColor = juce::Colour::fromString("ff" + hexText);
        audioProcessor.setGridColor(newColor);
        repaint();
    }
    catch (...)
    {
        // Invalid color, reset to current
        gridColorInput.setText(audioProcessor.getGridColor().toDisplayString(false));
    }
}

void SpectralFilterAudioProcessorEditor::updateSpectrumColor()
{
    juce::String hexText = spectrumColorInput.getText().trim();

    // Remove # if present
    if (hexText.startsWith("#"))
        hexText = hexText.substring(1);

    // Parse hex color
    try
    {
        juce::Colour newColor = juce::Colour::fromString("ff" + hexText);
        audioProcessor.setSpectrumColor(newColor);
        repaint();
    }
    catch (...)
    {
        // Invalid color, reset to current
        spectrumColorInput.setText(audioProcessor.getSpectrumColor().toDisplayString(false));
    }
}

//==============================================================================
void SpectralFilterAudioProcessorEditor::resized()
{
    int w = getWidth();

    // Top-right controls - shifted 20px to the left
    int rightEdge = w - 26;  // Was -6, now -26 (20px shift)
    int inputWidth = 80;
    int labelWidth = 68;  // Wider label for "Spectrum:"
    int rowHeight = 26;
    int startY = 6;

    // Row 1: FFT Size
    fftSizeCombo.setBounds(rightEdge - inputWidth, startY, inputWidth, 22);
    fftSizeLabel.setBounds(rightEdge - inputWidth - 58, startY, 58, 22);

    // Row 2: BG Color
    bgColorInput.setBounds(rightEdge - inputWidth, startY + rowHeight, inputWidth, 22);
    bgColorLabel.setBounds(rightEdge - inputWidth - 58, startY + rowHeight, 58, 22);

    // Row 3: Curve Color
    curveColorInput.setBounds(rightEdge - inputWidth, startY + rowHeight * 2, inputWidth, 22);
    curveColorLabel.setBounds(rightEdge - inputWidth - 58, startY + rowHeight * 2, 58, 22);

    // Row 4: Grid Color
    gridColorInput.setBounds(rightEdge - inputWidth, startY + rowHeight * 3, inputWidth, 22);
    gridColorLabel.setBounds(rightEdge - inputWidth - 58, startY + rowHeight * 3, 58, 22);

    // Row 5: Spectrum Color
    spectrumColorInput.setBounds(rightEdge - inputWidth, startY + rowHeight * 4, inputWidth, 22);
    spectrumColorLabel.setBounds(rightEdge - inputWidth - labelWidth, startY + rowHeight * 4, labelWidth, 22);

    // Row 6: Random Button
    randomButton.setBounds(rightEdge - inputWidth, startY + rowHeight * 5, inputWidth, 22);

    // Row 7: Export IR Button
    exportIRButton.setBounds(rightEdge - inputWidth, startY + rowHeight * 6, inputWidth, 22);
}