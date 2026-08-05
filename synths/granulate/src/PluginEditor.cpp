#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Default colour values — also used by ColorSidePanel to initialise editors
//==============================================================================
namespace Defaults
{
    static const juce::Colour bgTop{ 0xffc0c0c0 };  // silver
    static const juce::Colour bgBottom{ 0xff00ffff };  // cyan
    static const juce::Colour waveformBg{ 0xff1a5555 };  // dark teal
    static const juce::Colour waveform{ 0xff00ffff };  // cyan
    static const juce::Colour playhead{ 0xff00ff00 };  // lime
    static const juce::Colour marker{ 0xffffff00 };  // yellow
    static const juce::Colour text{ 0xffffffff };  // white
}

//==============================================================================
// Helper: return a validated 6-char uppercase hex string from a Colour,
// stripping the leading alpha "FF" that toString() prepends.
//==============================================================================
static juce::String colourToHex6(juce::Colour c)
{
    return c.toString().substring(2).toUpperCase();
}

//==============================================================================
// WaveformDisplay Implementation
//==============================================================================

void WaveformDisplay::paint(juce::Graphics& g)
{
    // Read user-defined colours (with sensible defaults if not yet set)
    const juce::Colour waveformBgCol = processor.getColourProperty("colorWaveformBg", Defaults::waveformBg);
    const juce::Colour waveformCol = processor.getColourProperty("colorWaveform", Defaults::waveform);
    const juce::Colour playheadCol = processor.getColourProperty("colorPlayhead", Defaults::playhead);
    const juce::Colour markerCol = processor.getColourProperty("colorMarker", Defaults::marker);

    g.fillAll(waveformBgCol);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.drawRect(getLocalBounds(), 2);

    if (!processor.hasSample())
    {
        const juce::Colour textCol = processor.getColourProperty("colorText", Defaults::text);
        g.setColour(textCol);
        g.setFont(14.0f);
        g.drawText("Drag and drop an audio file here or click 'Load Sample'. Play with mouse click & drag or MIDI.",
            getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto& buffer = processor.getSampleBuffer();
    auto  bounds = getLocalBounds().reduced(4);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    juce::Path waveformPath;
    const int displayWidth = bounds.getWidth();
    const int samplesPerPixel = juce::jmax(1, numSamples / (displayWidth * 2));

    // Top half
    for (int x = 0; x < displayWidth; ++x)
    {
        juce::int64 s0 = ((juce::int64)x * numSamples) / displayWidth;
        juce::int64 s1 = ((juce::int64)(x + 1) * numSamples) / displayWidth;
        s0 = juce::jlimit((juce::int64)0, (juce::int64)(numSamples - 1), s0);
        s1 = juce::jlimit((juce::int64)0, (juce::int64)(numSamples - 1), s1);

        float maxVal = 0.0f;
        for (int s = (int)s0; s < (int)s1; s += samplesPerPixel)
        {
            if (s >= numSamples) break;
            float sample = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                if (s >= 0 && s < buffer.getNumSamples())
                    sample += buffer.getReadPointer(ch)[s];
            sample /= (float)numChannels;
            maxVal = juce::jmax(maxVal, sample);
        }

        const float centerY = bounds.getCentreY();
        const float maxY = centerY - maxVal * bounds.getHeight() * 0.45f;

        if (x == 0)
            waveformPath.startNewSubPath(bounds.getX() + x, maxY);
        else
            waveformPath.lineTo(bounds.getX() + x, maxY);
    }

    // Bottom half
    for (int x = displayWidth - 1; x >= 0; --x)
    {
        juce::int64 s0 = ((juce::int64)x * numSamples) / displayWidth;
        juce::int64 s1 = ((juce::int64)(x + 1) * numSamples) / displayWidth;
        s0 = juce::jlimit((juce::int64)0, (juce::int64)(numSamples - 1), s0);
        s1 = juce::jlimit((juce::int64)0, (juce::int64)(numSamples - 1), s1);

        float minVal = 0.0f;
        for (int s = (int)s0; s < (int)s1; s += samplesPerPixel)
        {
            if (s >= numSamples) break;
            float sample = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                if (s >= 0 && s < buffer.getNumSamples())
                    sample += buffer.getReadPointer(ch)[s];
            sample /= (float)numChannels;
            minVal = juce::jmin(minVal, sample);
        }

        const float centerY = bounds.getCentreY();
        const float minY = centerY - minVal * bounds.getHeight() * 0.45f;
        waveformPath.lineTo(bounds.getX() + x, minY);
    }

    waveformPath.closeSubPath();

    g.setColour(waveformCol.withAlpha(0.7f));
    g.fillPath(waveformPath);
    g.setColour(waveformCol);
    g.strokePath(waveformPath, juce::PathStrokeType(1.0f));

    // Window region overlay
    const float position = processor.getValueTreeState().getRawParameterValue("grainPosition")->load();
    const float windowSize = processor.getValueTreeState().getRawParameterValue("windowSize")->load();
    const int   winX = bounds.getX() + (int)(position * bounds.getWidth());
    const int   winW = (int)(windowSize * bounds.getWidth());

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRect(winX, bounds.getY(), winW, bounds.getHeight());
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.drawRect(winX, bounds.getY(), winW, bounds.getHeight(), 2);

    // Position marker
    g.setColour(markerCol.withAlpha(0.8f));
    g.drawLine((float)winX, (float)bounds.getY(),
        (float)winX, (float)bounds.getBottom(), 2.0f);

    // Active grain playheads
    const auto activeGrains = processor.getActiveGrains();
    for (const auto& grain : activeGrains)
    {
        const double normalizedPos = grain.samplePosition / (double)numSamples;
        const int    phX = bounds.getX() + (int)(normalizedPos * bounds.getWidth());
        const float  brightness = 1.0f - grain.grainEnvelopePhase;

        g.setColour(playheadCol.withAlpha(0.7f * brightness));
        g.drawLine((float)phX, (float)bounds.getY(),
            (float)phX, (float)bounds.getBottom(), 1.5f);
    }
}

bool WaveformDisplay::isInterestedInFileDrag(const juce::StringArray& /*files*/)
{
    // Accept any file — unknown formats are handled by the raw-import dialog.
    return true;
}

void WaveformDisplay::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    if (files.size() == 0)
        return;

    // Route through the editor so the raw-import dialog can be shown when needed.
    if (auto* editor = findParentComponentOfClass<GranulateAudioProcessorEditor>())
        editor->handleFileImport(juce::File(files[0]));
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    if (processor.hasSample())
    {
        float pos = juce::jlimit(0.0f, 1.0f, (float)event.x / getWidth());
        processor.setMousePosition(pos);
        repaint();
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event)
{
    if (processor.hasSample())
    {
        float pos = juce::jlimit(0.0f, 1.0f, (float)event.x / getWidth());
        processor.setMousePosition(pos);
        repaint();
    }
}

void WaveformDisplay::mouseUp(const juce::MouseEvent& /*event*/)
{
    processor.releaseMousePosition();
}

//==============================================================================
// ColorSidePanel Implementation
//==============================================================================

ColorSidePanel::ColorSidePanel(GranulateAudioProcessor& p,
    std::function<void()>    repaintCb)
    : processor(p), repaintCallback(std::move(repaintCb))
{
    // ---- Define the six editable colours --------------------------------
    struct RowDef { const char* prop; const char* name; juce::Colour def; };
    const RowDef defs[] = {
        { "colorBgTop",      "BG Top",      Defaults::bgTop      },
        { "colorBgBottom",   "BG Bottom",   Defaults::bgBottom   },
        { "colorWaveformBg", "Waveform BG", Defaults::waveformBg },
        { "colorWaveform",   "Waveform",    Defaults::waveform   },
        { "colorPlayhead",   "Playheads",   Defaults::playhead   },
        { "colorMarker",     "Marker",      Defaults::marker     },
        { "colorText",       "Text",        Defaults::text       },
    };

    // ---- Create rows ----------------------------------------------------
    for (auto& d : defs)
    {
        ColorRow row;
        row.propertyName = d.prop;
        row.displayName = d.name;
        row.defaultColour = d.def;
        row.hexEditor = std::make_unique<juce::TextEditor>();

        auto& ed = *row.hexEditor;
        ed.setMultiLine(false);
        ed.setJustification(juce::Justification::centred);
        ed.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        ed.setInputRestrictions(6, "0123456789ABCDEFabcdef");
        ed.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2a2a));
        ed.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        ed.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff555555));
        ed.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::cyan);

        // Initialise text from processor (or default if not yet set)
        juce::Colour currentCol = processor.getColourProperty(d.prop, d.def);
        ed.setText(colourToHex6(currentCol), juce::dontSendNotification);

        addAndMakeVisible(ed);
        colorRows.push_back(std::move(row));
    }

    // ---- Wire up callbacks (after push_back, pointers are stable) -------
    for (auto& row : colorRows)
    {
        ColorRow* rowPtr = &row;
        row.hexEditor->onReturnKey = [this, rowPtr]() { applyHexFromEditor(*rowPtr); };
        row.hexEditor->onFocusLost = [this, rowPtr]() { applyHexFromEditor(*rowPtr); };
    }

    // ---- Save / Load preset buttons ------------------------------------
    addAndMakeVisible(savePresetButton);
    savePresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4a2a));
    savePresetButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a6a3a));
    savePresetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    savePresetButton.onClick = [this]() { savePreset(); };

    addAndMakeVisible(loadPresetButton);
    loadPresetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4a));
    loadPresetButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a6a));
    loadPresetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loadPresetButton.onClick = [this]() { loadPreset(); };

    // Randomize colours button — title bar, left side (replaces title text)
    addAndMakeVisible(randomizeButton);
    randomizeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a3020));
    randomizeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6a4030));
    randomizeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    randomizeButton.onClick = [this]()
        {
            // ---- Colour palette -----------------------------------------
            juce::Random rng;
            const float hue = rng.nextFloat();

            auto randCol = [&](float hueShift, float sat, float bri) -> juce::Colour {
                return juce::Colour::fromHSV(std::fmod(hue + hueShift, 1.0f), sat, bri, 1.0f);
                };

            processor.setColourProperty("colorBgTop", randCol(0.0f, 0.25f, 0.85f));
            processor.setColourProperty("colorBgBottom", randCol(0.5f, 0.80f, 0.90f));
            processor.setColourProperty("colorWaveformBg", randCol(0.55f, 0.70f, 0.25f));
            processor.setColourProperty("colorWaveform", randCol(0.5f, 0.90f, 1.00f));
            processor.setColourProperty("colorPlayhead", randCol(0.25f, 1.00f, 1.00f));
            processor.setColourProperty("colorMarker", randCol(0.12f, 1.00f, 1.00f));
            processor.setColourProperty("colorText", randCol(0.08f, 0.10f, 1.00f));

            // ---- Knob values (via normalized 0-1 parameter values) ------
            auto setParam = [&](const juce::String& id, float normalizedValue)
                {
                    if (auto* p = processor.getValueTreeState().getParameter(id))
                        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalizedValue));
                };

            // numGrains: pick 4..20 grains, convert to normalized 0-1 over range 1-32
            const int ng = rng.nextInt({ 4, 20 });
            setParam("numGrains", (float)(ng - 1) / 31.0f);
            // grainSize: prefer short-to-medium (0.05..1.5s) — use normalized range
            setParam("grainSize", rng.nextFloat() * 0.6f);         // skewed range: lower = shorter
            setParam("grainPosition", rng.nextFloat());               // 0..1 direct
            setParam("spray", rng.nextFloat() * 0.7f);         // 0..0.7 normalized
            setParam("windowSize", 0.1f + rng.nextFloat() * 0.9f);  // prefer wider windows
            setParam("amplitudeMod", rng.nextFloat() * 0.8f);
            setParam("amDispersion", rng.nextFloat());

            refreshEditors();
            repaintCallback();
            repaint();
        };
}

//------------------------------------------------------------------------------
void ColorSidePanel::applyHexFromEditor(ColorRow& row)
{
    const juce::String hex = row.hexEditor->getText().trim().toUpperCase();

    // Must be exactly 6 valid hex chars
    if (hex.length() != 6)
        return;
    if (!hex.containsOnly("0123456789ABCDEF"))
        return;

    const juce::Colour newCol = juce::Colour::fromString("ff" + hex);
    processor.setColourProperty(row.propertyName, newCol);

    repaintCallback(); // repaint main editor
    repaint();         // refresh swatch
}

//------------------------------------------------------------------------------
void ColorSidePanel::refreshEditors()
{
    for (auto& row : colorRows)
    {
        juce::Colour col = processor.getColourProperty(row.propertyName, row.defaultColour);
        row.hexEditor->setText(colourToHex6(col), juce::dontSendNotification);
    }
}

//------------------------------------------------------------------------------
void ColorSidePanel::savePreset()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Granulate Preset...", juce::File{}, "*.granulate");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            juce::File file = fc.getResult();
            if (file.getFullPathName().isEmpty())
                return;

            // Ensure correct extension
            if (!file.hasFileExtension("granulate"))
                file = file.withFileExtension("granulate");

            // Copy the full APVTS state (includes audio params + colour
            // properties + lastLoadedSamplePath stored as ValueTree properties)
            auto state = processor.getValueTreeState().copyState();
            std::unique_ptr<juce::XmlElement> xml(state.createXml());
            if (xml)
                file.replaceWithText(xml->toString());
        });
}

//------------------------------------------------------------------------------
void ColorSidePanel::loadPreset()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Granulate Preset...", juce::File{}, "*.granulate");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            const juce::File file = fc.getResult();
            if (!file.existsAsFile())
                return;

            auto xml = juce::parseXML(file);
            if (!xml)
                return;

            // Restore state only if the tag name matches (so we don't crash
            // on unrelated XML files)
            if (xml->hasTagName(processor.getValueTreeState().state.getType()))
            {
                auto newState = juce::ValueTree::fromXml(*xml);
                processor.getValueTreeState().replaceState(newState);

                // Reload the sample if the preset stored a path, honouring raw
                // import settings exactly the same way setStateInformation does.
                const juce::String path =
                    newState.getProperty("samplePath", "").toString();
                if (path.isNotEmpty())
                {
                    const juce::File sampleFile(path);
                    if (sampleFile.existsAsFile())
                    {
                        const bool hasRaw = newState.hasProperty("rawBitDepth")
                            && newState.hasProperty("rawNumChannels")
                            && newState.hasProperty("rawSampleRate");
                        if (hasRaw)
                        {
                            const int    bd = (int)newState.getProperty("rawBitDepth", 16);
                            const int    ch = (int)newState.getProperty("rawNumChannels", 1);
                            const double sr = (double)newState.getProperty("rawSampleRate", 44100.0);
                            processor.loadSampleRaw(sampleFile, bd, ch, sr);
                        }
                        else
                        {
                            processor.loadSample(sampleFile);
                        }
                    }
                }

                refreshEditors();
                repaintCallback();
            }
        });
}

//------------------------------------------------------------------------------
void ColorSidePanel::paint(juce::Graphics& g)
{
    // Panel background
    g.fillAll(juce::Colour(0xf0141414));

    // Left-edge separator line
    g.setColour(juce::Colour(0xff444444));
    g.drawLine(0.5f, 0.0f, 0.5f, (float)getHeight(), 1.5f);

    // Thin divider below title bar buttons
    g.setColour(juce::Colour(0xff444444));
    g.drawHorizontalLine(37, 8.0f, (float)getWidth() - 8.0f);

    // Draw label + swatch for each row
    g.setFont(juce::Font(11.0f));
    const int rowH = 33;
    const int startY = 52;
    const int pad = 10;
    const int swSize = 16;

    for (int i = 0; i < (int)colorRows.size(); ++i)
    {
        const auto& row = colorRows[i];
        const int   ry = startY + i * rowH;

        // Label
        g.setColour(juce::Colours::lightgrey);
        g.drawText(row.displayName, pad, ry + 8, 72, 18,
            juce::Justification::centredLeft);

        // Swatch
        const juce::Colour col = processor.getColourProperty(
            row.propertyName, row.defaultColour);
        g.setColour(col);
        g.fillRect(row.swatchBounds);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawRect(row.swatchBounds, 1);

        // '#' prefix
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(12.0f));
        g.drawText("#", row.swatchBounds.getRight() + 2, ry + 6, 12, 22,
            juce::Justification::centredLeft);
    }

    juce::ignoreUnused(swSize);
}

//------------------------------------------------------------------------------
void ColorSidePanel::resized()
{
    const int rowH = 33;
    const int startY = 52;   // more top padding — rows start below the Close Sidebar button
    const int pad = 10;
    const int swSize = 16;
    const int w = getWidth();

    for (int i = 0; i < (int)colorRows.size(); ++i)
    {
        auto& row = colorRows[i];
        const int ry = startY + i * rowH;

        row.swatchBounds = { pad + 72 + 4, ry + 8, swSize, swSize };

        const int edX = row.swatchBounds.getRight() + 14 + 2;
        const int edW = w - edX - pad;
        row.hexEditor->setBounds(edX, ry + 7, edW, 19);
    }

    // Title bar: Close Sidebar full-width
    const int titleH = 36;
    const int btnH0 = 22;
    const int btnY0 = (titleH - btnH0) / 2;
    juce::ignoreUnused(btnY0);

    // Buttons below rows: Randomize → Save Preset → Load Preset
    const int btnAreaY = startY + (int)colorRows.size() * rowH + 10;
    const int btnH = 28;
    randomizeButton.setBounds(pad, btnAreaY, w - 2 * pad, btnH);
    savePresetButton.setBounds(pad, btnAreaY + btnH + 6, w - 2 * pad, btnH);
    loadPresetButton.setBounds(pad, btnAreaY + 2 * (btnH + 6), w - 2 * pad, btnH);
}

//==============================================================================
// GranulateAudioProcessorEditor Implementation
//==============================================================================

GranulateAudioProcessorEditor::GranulateAudioProcessorEditor(GranulateAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), waveformDisplay(p)
{
    setSize(770, 420);

    // ---- Waveform display ------------------------------------------------
    addAndMakeVisible(waveformDisplay);

    // ---- Load button -----------------------------------------------------
    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load Sample");
    loadButton.onClick = [this]
    {
        // Accept any file type; non-audio files are routed to the raw-import dialog.
        chooser = std::make_unique<juce::FileChooser>("Select a file...", juce::File{}, "*");
        chooser->launchAsync(juce::FileBrowserComponent::openMode,
            [this](const juce::FileChooser& fc)
            {
                const juce::File file = fc.getResult();
                if (file.existsAsFile())
                    handleFileImport(file);
            });
    };

    // ---- Color panel toggle button ---------------------------------------
    addAndMakeVisible(colorPanelButton);
    colorPanelButton.setButtonText("Open Sidebar");
    colorPanelButton.setColour(juce::TextButton::buttonColourId,
        juce::Colour(0xff334455));
    colorPanelButton.setColour(juce::TextButton::buttonOnColourId,
        juce::Colour(0xff446677));
    colorPanelButton.setColour(juce::TextButton::textColourOffId,
        juce::Colours::white);
    colorPanelButton.onClick = [this]
        {
            sidebarOpen = !sidebarOpen;
            colorPanelButton.setButtonText(sidebarOpen ? "Close Sidebar" : "Open Sidebar");
            colorSidePanel->setVisible(sidebarOpen);
            if (sidebarOpen)
            {
                colorSidePanel->refreshEditors();
                colorSidePanel->toFront(false);
            }
            // Always keep the toggle button painted on top of the sidebar overlay
            colorPanelButton.toFront(false);
        };

    // ---- Color side panel (invisible until opened) ----------------------
    colorSidePanel = std::make_unique<ColorSidePanel>(audioProcessor,
        [this]() { repaint(); waveformDisplay.repaint(); });
    addChildComponent(*colorSidePanel); // invisible by default
    // Must set bounds NOW — resized() already ran (via setSize above) before
    // colorSidePanel was created, so the if-check in resized() was skipped.
    colorSidePanel->setBounds(770 - 210, 0, 210, 420);

    // ---- Slider / Label helper ------------------------------------------
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label,
        const juce::String& labelText,
        const juce::String& paramID,
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
        {
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

            addAndMakeVisible(label);
            label.setText(labelText, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setFont(juce::Font(11.0f));
            label.attachToComponent(&slider, false);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                audioProcessor.getValueTreeState(), paramID, slider);
        };

    // Grain controls
    setupSlider(numGrainsSlider, numGrainsLabel, "Grains", "numGrains", numGrainsAttachment);
    setupSlider(grainSizeSlider, grainSizeLabel, "Seconds", "grainSize", grainSizeAttachment);
    setupSlider(grainPositionSlider, grainPositionLabel, "Start Pos", "grainPosition", grainPositionAttachment);
    setupSlider(spraySlider, sprayLabel, "Spray", "spray", sprayAttachment);
    setupSlider(windowSizeSlider, windowSizeLabel, "End Pos", "windowSize", windowSizeAttachment);

    // Grain ADSR
    setupSlider(grainAttackSlider, grainAttackLabel, "G-Attack", "grainAttack", grainAttackAttachment);
    setupSlider(grainDecaySlider, grainDecayLabel, "G-Decay", "grainDecay", grainDecayAttachment);
    setupSlider(grainSustainSlider, grainSustainLabel, "G-Sustain", "grainSustain", grainSustainAttachment);
    setupSlider(grainReleaseSlider, grainReleaseLabel, "G-Release", "grainRelease", grainReleaseAttachment);

    // Note ADSR
    setupSlider(noteAttackSlider, noteAttackLabel, "N-Attack", "noteAttack", noteAttackAttachment);
    setupSlider(noteDecaySlider, noteDecayLabel, "N-Decay", "noteDecay", noteDecayAttachment);
    setupSlider(noteSustainSlider, noteSustainLabel, "N-Sustain", "noteSustain", noteSustainAttachment);
    setupSlider(noteReleaseSlider, noteReleaseLabel, "N-Release", "noteRelease", noteReleaseAttachment);

    // Modulation controls
    setupSlider(amplitudeModSlider, amplitudeModLabel, "AM", "amplitudeMod", amplitudeModAttachment);
    setupSlider(amDispersionSlider, amDispersionLabel, "AM Disp", "amDispersion", amDispersionAttachment);
    setupSlider(pitchDispersionSlider, pitchDispersionLabel, "Pitch Disp", "pitchDispersion", pitchDispersionAttachment);
    setupSlider(pitchSlider, pitchLabel, "Pitch (Mouse)", "pitch", pitchAttachment);
    setupSlider(stereoSpreadSlider, stereoSpreadLabel, "Stereo", "stereoSpread", stereoSpreadAttachment);
    setupSlider(volumeSlider, volumeLabel, "Volume", "volume", volumeAttachment);
    setupSlider(reversedGrainsSlider, reversedGrainsLabel, "Rev Grains", "reversedGrains", reversedGrainsAttachment);

    startTimerHz(30);
}

GranulateAudioProcessorEditor::~GranulateAudioProcessorEditor()
{
    stopTimer();
}

//------------------------------------------------------------------------------
// handleFileImport — called by both the Load button and drag-and-drop.
// Tries to open the file as a standard audio format; if that fails (the format
// manager returns nullptr) the file is treated as raw PCM and the import dialog
// is shown so the user can supply bit-depth, channel count and sample rate.
//------------------------------------------------------------------------------
void GranulateAudioProcessorEditor::handleFileImport(const juce::File& file)
{
    // Quick extension check for the common audio formats that JUCE supports.
    // AudioFormatManager::createReaderFor() is the authoritative test, but
    // checking the extension first avoids loading the entire file into memory
    // just to discover it is not a supported format.
    static const juce::StringArray audioExtensions {
        ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg",
        ".w64", ".rf64", ".caf", ".aac", ".m4a"
    };

    const juce::String ext = file.getFileExtension().toLowerCase();

    if (audioExtensions.contains(ext))
    {
        // Known audio extension — let the processor handle it the normal way.
        audioProcessor.loadSample(file);
        waveformDisplay.repaint();
        return;
    }

    // Unknown extension: attempt to open with AudioFormatManager anyway,
    // because some audio files have non-standard or missing extensions.
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(file));

        if (reader != nullptr)
        {
            // AudioFormatManager succeeded — load normally.
            audioProcessor.loadSample(file);
            waveformDisplay.repaint();
            return;
        }
    }

    // AudioFormatManager could not open the file — treat it as raw PCM.
    showRawImportDialog(file);
}

//------------------------------------------------------------------------------
// showRawImportDialog — presents an AlertWindow for the user to specify the
// raw PCM format (bit depth, channel count, sample rate), then calls
// audioProcessor.loadSampleRaw() on confirmation.
//
// Only 16-bit and 24-bit little-endian signed integer PCM are supported,
// matching the implementation in GranulateAudioProcessor::loadSampleRaw().
//------------------------------------------------------------------------------
void GranulateAudioProcessorEditor::showRawImportDialog(const juce::File& file)
{
    auto* dw = new juce::AlertWindow(
        "Raw Data Import",
        "The file does not appear to be a recognised audio format.\n"
        "Import as raw (headerless) PCM data?\n\n"
        "File: " + file.getFileName(),
        juce::AlertWindow::QuestionIcon);

    dw->addComboBox("bitDepth",  { "16-bit", "24-bit" }, "Bit Depth");
    dw->addComboBox("channels",  { "Mono", "Stereo" },   "Channels");
    dw->addTextEditor("sampleRate", "44100",             "Sample Rate (Hz):");

    dw->addButton("Import", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // enterModalState owns the AlertWindow and deletes it after the callback.
    dw->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [this, dw, file](int result)
            {
                if (result == 1)
                {
                    const int depthIdx = dw->getComboBoxComponent("bitDepth")->getSelectedItemIndex();
                    const int bitDepth = (depthIdx == 0) ? 16 : 24;

                    const int channels = dw->getComboBoxComponent("channels")->getSelectedItemIndex() + 1;

                    double sampleRate = dw->getTextEditorContents("sampleRate").getDoubleValue();
                    if (sampleRate <= 0.0)
                        sampleRate = 44100.0;

                    audioProcessor.loadSampleRaw(file, bitDepth, channels, sampleRate);
                    waveformDisplay.repaint();
                }
            }),
        true /* deleteWhenDismissed */);
}

//------------------------------------------------------------------------------
void GranulateAudioProcessorEditor::timerCallback()
{
    waveformDisplay.repaint();

    // Note: reversedGrains slider range is always 0-32 (matching the parameter
    // definition). The audio thread clamps to the current numGrains value,
    // so there's no need to — and we must NOT — call setRange() here, because
    // doing so after the SliderAttachment was created breaks the APVTS
    // normalization mapping and corrupts stored/recalled values.

    // Push text colour to all knob labels and textboxes when user changes it
    const juce::Colour tc = audioProcessor.getColourProperty("colorText", Defaults::text);
    if (tc != lastTextColour)
    {
        lastTextColour = tc;

        // Helper to colour one slider's label + textbox
        auto applyToSlider = [&](juce::Slider& s, juce::Label& l)
            {
                l.setColour(juce::Label::textColourId, tc);
                s.setColour(juce::Slider::textBoxTextColourId, tc);
                s.setColour(juce::Slider::textBoxOutlineColourId, tc.withAlpha(0.4f));
            };

        applyToSlider(numGrainsSlider, numGrainsLabel);
        applyToSlider(grainSizeSlider, grainSizeLabel);
        applyToSlider(grainPositionSlider, grainPositionLabel);
        applyToSlider(spraySlider, sprayLabel);
        applyToSlider(windowSizeSlider, windowSizeLabel);
        applyToSlider(grainAttackSlider, grainAttackLabel);
        applyToSlider(grainDecaySlider, grainDecayLabel);
        applyToSlider(grainSustainSlider, grainSustainLabel);
        applyToSlider(grainReleaseSlider, grainReleaseLabel);
        applyToSlider(noteAttackSlider, noteAttackLabel);
        applyToSlider(noteDecaySlider, noteDecayLabel);
        applyToSlider(noteSustainSlider, noteSustainLabel);
        applyToSlider(noteReleaseSlider, noteReleaseLabel);
        applyToSlider(amplitudeModSlider, amplitudeModLabel);
        applyToSlider(amDispersionSlider, amDispersionLabel);
        applyToSlider(pitchDispersionSlider, pitchDispersionLabel);
        applyToSlider(pitchSlider, pitchLabel);
        applyToSlider(stereoSpreadSlider, stereoSpreadLabel);
        applyToSlider(volumeSlider, volumeLabel);
        applyToSlider(reversedGrainsSlider, reversedGrainsLabel);

        repaint();
    }
}

//------------------------------------------------------------------------------
void GranulateAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Read user-defined background gradient colours
    const juce::Colour bgTop = audioProcessor.getColourProperty("colorBgTop", Defaults::bgTop);
    const juce::Colour bgBottom = audioProcessor.getColourProperty("colorBgBottom", Defaults::bgBottom);

    // ---- Background gradient --------------------------------------------
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient background(bgTop, r.getTopLeft(),
        bgBottom, r.getBottomRight(), false);
    g.setGradientFill(background);
    g.fillAll();

    // ---- Gloss overlay --------------------------------------------------
    {
        auto gloss = r.withHeight(r.getHeight() * 0.35f);
        juce::ColourGradient glossGrad(
            juce::Colours::white.withAlpha(0.35f), gloss.getTopLeft(),
            juce::Colours::white.withAlpha(0.05f), gloss.getBottomLeft(),
            false);
        g.setGradientFill(glossGrad);
        g.fillRect(gloss);
    }

    // ---- Title ----------------------------------------------------------
    const juce::Colour textCol = audioProcessor.getColourProperty("colorText", Defaults::text);
    g.setColour(textCol);
    g.setFont(18.0f);
    // Plugin name — left side
    g.drawText("Granulate by aquanode", 15, 10, 195, 25,
        juce::Justification::centredLeft);
    // G/N hint — between plugin name and Load Sample button
    g.setFont(13.0f);
    g.drawText("G = Per Grain   N = Per Note", 215, 10, 230, 25,
        juce::Justification::centredLeft);

    // ---- Inner bevel (outer) -------------------------------------------
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(b, 6.0f, 1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.drawRoundedRectangle(b.reduced(1.0f), 6.0f, 1.0f);
    }

    // ---- Inner bevel around controls section ----------------------------
    {
        const int ctrlTop = 160;
        const int ctrlBottom = getHeight() - 10;
        auto cb = juce::Rectangle<float>(
            7.0f, (float)ctrlTop,
            (float)getWidth() - 20.0f,
            (float)(ctrlBottom - ctrlTop - 10.0f)).reduced(2.0f);

        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawRoundedRectangle(cb, 8.0f, 1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.drawRoundedRectangle(cb.reduced(1.5f), 8.0f, 1.0f);
    }
}

//------------------------------------------------------------------------------
void GranulateAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // ---- TOP: waveform --------------------------------------------------
    auto topSection = bounds.removeFromTop(150);
    topSection.removeFromTop(40);
    waveformDisplay.setBounds(topSection.reduced(10));

    loadButton.setBounds(455, 7, 105, 22);

    // "Open Sidebar" / "Close Sidebar" — always same rect, immediately right of Load Sample
    colorPanelButton.setBounds(566, 7, 198, 22);
    colorPanelButton.toFront(false);

    // Color side panel — overlaid on the right 210px of the plugin window.
    // setVisible() controls whether it's shown; window size stays at 770px.
    if (colorSidePanel)
        colorSidePanel->setBounds(getWidth() - 210, 0, 210, getHeight());

    // ---- KNOB GRID ------------------------------------------------------
    const int xStart = 12;
    const int knobSize = 65;
    const int spacingX = 75;
    const int spacingY = 95;
    const int labelGap = 15;

    int x = xStart;
    int y = 170;

    // ROW 1
    numGrainsSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainSizeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainPositionSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    spraySlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    windowSizeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    // Reversed knob sits in the second half of the old 2×spacingX gap
    reversedGrainsSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;

    grainAttackSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainDecaySlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainSustainSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    grainReleaseSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20);

    // ROW 2
    x = xStart;
    y += spacingY + 10;

    amplitudeModSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    amDispersionSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    pitchDispersionSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    pitchSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    stereoSpreadSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    volumeSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;

    noteAttackSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    noteDecaySlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    noteSustainSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20); x += spacingX;
    noteReleaseSlider.setBounds(x, y + labelGap, knobSize, knobSize + 20);
}
