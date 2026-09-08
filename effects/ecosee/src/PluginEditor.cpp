#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>

//==============================================================================
namespace
{
    juce::Colour packedToColour (int rgb)
    {
        return juce::Colour ((juce::uint8) ((rgb >> 16) & 0xFF),
                              (juce::uint8) ((rgb >> 8) & 0xFF),
                              (juce::uint8) (rgb & 0xFF));
    }

    int colourToPacked (juce::Colour c)
    {
        return ((int) c.getRed() << 16) | ((int) c.getGreen() << 8) | (int) c.getBlue();
    }

    // A clickable colour swatch that opens a ColourSelector popup and writes
    // the picked colour straight back into an AudioParameterInt (packed
    // 0xRRGGBB), so it's saved/restored with the rest of plugin state.
    class ColourSwatchButton : public juce::Component,
                                private juce::ChangeListener
    {
    public:
        explicit ColourSwatchButton (juce::AudioParameterInt& p) : param (p) {}

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (packedToColour (param.get()));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRoundedRectangle (b, 3.0f, 1.0f);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            auto selector = std::make_unique<juce::ColourSelector> (
                juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                    | juce::ColourSelector::showColourspace);
            selector->setCurrentColour (packedToColour (param.get()), juce::dontSendNotification);
            selector->setSize (250, 300);
            selector->addChangeListener (this);

            juce::CallOutBox::launchAsynchronously (std::move (selector), getScreenBounds(), nullptr);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster* source) override
        {
            if (auto* cs = dynamic_cast<juce::ColourSelector*> (source))
            {
                const int packed = colourToPacked (cs->getCurrentColour());
                param.setValueNotifyingHost (param.convertTo0to1 ((float) packed));
                repaint();
            }
        }

        juce::AudioParameterInt& param;
    };
}

//==============================================================================
// ControlSidebar
//==============================================================================
ControlSidebar::ControlSidebar (juce::AudioProcessorValueTreeState& state)
    : apvtsForTheme (state)
{
    // Order follows the signal's actual path through the plugin: how it's
    // analysed -> how that data is scaled -> how it's laid out on screen ->
    // how it moves -> how it's rendered -> what colours it uses.
    sections.push_back ({ "ANALYSIS", {} });
    addCombo  (sections.back(), state, "AN_FFT_SIZE", "FFT Size");
    addCombo  (sections.back(), state, "AN_WINDOW",   "Window");
    addCombo  (sections.back(), state, "AN_OVERLAP",  "Overlap");

    sections.push_back ({ "RANGE", {} });
    addSlider (sections.back(), state, "RG_FREQ_MIN",    "Freq Low (Hz)");
    addSlider (sections.back(), state, "RG_FREQ_MAX",    "Freq High (Hz)");
    addSlider (sections.back(), state, "RG_SPREAD_MAX",  "Spread Range");
    addSlider (sections.back(), state, "RG_AMP_GAIN",    "Amplitude Gain");
    addSlider (sections.back(), state, "RG_AMMOD_SCALE", "AM Mod Scale");

    sections.push_back ({ "VIEW", {} });
    addCombo  (sections.back(), state, "VS_VIEW_MODE",  "View Mode");
    addSlider (sections.back(), state, "VS_MACRO_ZOOM", "Single-Panel Macro Zoom");
    addCombo  (sections.back(), state, "VS_PANEL_D_MODE", "Bottom-Right Panel");
    dragModeButton = &addActionButton (sections.back(), "Drag: Rotate");

    // Hidden by default; the editor reveals this section only while
    // VS_VIEW_MODE is set to "Custom 3D Grid". Placed right after VIEW
    // since it's really just extra configuration for that view mode.
    sections.push_back ({ "CUSTOM 3D GRID", {} });
    addCombo (sections.back(), state, "VS_GRID_X", "X Field");
    addCombo (sections.back(), state, "VS_GRID_Y", "Y Field");
    addCombo (sections.back(), state, "VS_GRID_Z", "Z Field");
    setSectionVisible ("CUSTOM 3D GRID", false);

    sections.push_back ({ "MOTION", {} });
    addToggle (sections.back(), state, "MO_AUTO_ROTATE", "Auto Rotate");
    addSlider (sections.back(), state, "MO_ROT_A",  "Rotation Speed A");
    addSlider (sections.back(), state, "MO_ROT_B",  "Rotation Speed B");
    addSlider (sections.back(), state, "MO_ANGLE_A", "Angle A (manual)");
    addSlider (sections.back(), state, "MO_ANGLE_B", "Angle B (manual)");
    addSlider (sections.back(), state, "MO_TRAIL",  "Trail Fade");
    addCombo  (sections.back(), state, "MO_FPS",    "Target FPS");

    sections.push_back ({ "VISUAL", {} });
    addToggle (sections.back(), state, "VS_SHOW_AXES",      "Show Axes");
    addSlider (sections.back(), state, "VS_POINT_COUNT",    "Visible Points");
    addToggle (sections.back(), state, "VS_SNAKE_MODE",     "Spline Trail Mode");
    addSlider (sections.back(), state, "VS_SNAKE_TAIL_BRIGHT", "Trail Tail Brightness");
    addSlider (sections.back(), state, "VS_SNAKE_HEAD_BRIGHT", "Trail Head Brightness");
    addToggle (sections.back(), state, "VS_NORMALIZE",      "Normalize Axes To Data");
    addToggle (sections.back(), state, "VS_LABEL_POINTS",   "Label Data Points");
    addToggle (sections.back(), state, "VS_HIDE_QUIET",     "Hide Dots If Quiet");
    addToggle (sections.back(), state, "VS_WHITE_BG",       "White Background");
    addSlider (sections.back(), state, "VS_SCALE",          "Graph Scale");
    addSlider (sections.back(), state, "VS_GLOW_SIZE",      "Glow Size");
    addSlider (sections.back(), state, "VS_GLOW_INTENSITY", "Glow Intensity");
    addSlider (sections.back(), state, "VS_POINT_SIZE",     "Point Size");
    addSlider (sections.back(), state, "VS_LINE_THICK",     "Line Thickness");
    addSlider (sections.back(), state, "VS_CEPSTRUM_SMOOTH", "Cepstrum Smoothing");

    sections.push_back ({ "COLOUR SCHEME", {} });
    addColourSwatch (sections.back(), state, "CS_LOW_RGB",  "Lowest");
    addColourSwatch (sections.back(), state, "CS_MID_RGB",  "Middle");
    addColourSwatch (sections.back(), state, "CS_HIGH_RGB", "Highest");
}

void ControlSidebar::updateTheme()
{
    const bool whiteBg = *apvtsForTheme.getRawParameterValue("VS_WHITE_BG") > 0.5f;
    if (whiteBg == themeIsWhite)
        return;

    themeIsWhite = whiteBg;

    const auto textColour = whiteBg
        ? juce::Colours::black.withAlpha(0.85f)
        : juce::Colours::white.withAlpha(0.85f);

    const auto buttonBg = whiteBg
        ? juce::Colour::fromRGB(235, 255, 255)
        : juce::Colour::fromRGB(30, 30, 36);

    for (auto& section : sections)
    {
        for (auto& row : section.rows)
        {
            if (row->label != nullptr)
                row->label->setColour(juce::Label::textColourId, textColour);

            if (row->toggle != nullptr)
            {
                row->toggle->setColour(juce::ToggleButton::textColourId, textColour);
                row->toggle->setColour(
                    juce::ToggleButton::tickColourId,
                    whiteBg ? juce::Colours::darkorange : juce::Colours::orange);
            }

            if (row->button != nullptr)
            {
                row->button->setColour(juce::TextButton::buttonColourId, buttonBg);
                row->button->setColour(juce::TextButton::textColourOffId, textColour);
            }

            if (row->slider != nullptr)
            {
                // Slider-Wert rechts neben dem Slider
                row->slider->setColour(
                    juce::Slider::textBoxTextColourId, textColour);

                row->slider->setColour(
                    juce::Slider::textBoxBackgroundColourId,
                    whiteBg ? juce::Colour::fromRGB(235, 255, 255)
                    : juce::Colour::fromRGB(18, 18, 22));

                row->slider->setColour(
                    juce::Slider::textBoxOutlineColourId,
                    whiteBg ? juce::Colours::black.withAlpha(0.15f)
                    : juce::Colours::white.withAlpha(0.15f));
            }

            if (row->combo != nullptr)
            {
                // Ausgewählter Wert in der ComboBox
                row->combo->setColour(
                    juce::ComboBox::textColourId, textColour);

                // Text im geöffneten Auswahlmenü
                row->combo->setColour(
                    juce::ComboBox::backgroundColourId,
                    whiteBg ? juce::Colour::fromRGB(235, 255, 255)
                    : juce::Colour::fromRGB(30, 30, 36));

                row->combo->setColour(
                    juce::ComboBox::outlineColourId,
                    whiteBg ? juce::Colours::black.withAlpha(0.15f)
                    : juce::Colours::white.withAlpha(0.15f));
            }
        }
    }

    repaint();
}

void ControlSidebar::addSlider (Section& section, juce::AudioProcessorValueTreeState& state,
                                  const juce::String& paramId, const juce::String& labelText)
{
    auto row = std::make_unique<Row>();
    row->label = std::make_unique<juce::Label>();
    row->label->setText (labelText, juce::dontSendNotification);
    row->label->setFont (juce::Font (juce::FontOptions (12.0f)));
    row->label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (*row->label);

    row->slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    row->slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
    row->slider->setColour (juce::Slider::trackColourId, juce::Colours::orange);
    row->slider->setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (*row->slider);

    row->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, paramId, *row->slider);

    section.rows.push_back (std::move (row));
}

void ControlSidebar::addCombo (Section& section, juce::AudioProcessorValueTreeState& state,
                                 const juce::String& paramId, const juce::String& labelText)
{
    auto row = std::make_unique<Row>();
    row->label = std::make_unique<juce::Label>();
    row->label->setText (labelText, juce::dontSendNotification);
    row->label->setFont (juce::Font (juce::FontOptions (12.0f)));
    row->label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (*row->label);

    row->combo = std::make_unique<juce::ComboBox>();
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
    {
        int idx = 1;
        for (const auto& choice : choiceParam->choices)
            row->combo->addItem (choice, idx++);
    }
    addAndMakeVisible (*row->combo);

    row->comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, paramId, *row->combo);

    section.rows.push_back (std::move (row));
}

void ControlSidebar::addToggle (Section& section, juce::AudioProcessorValueTreeState& state,
                                  const juce::String& paramId, const juce::String& labelText)
{
    auto row = std::make_unique<Row>();

    row->toggle = std::make_unique<juce::ToggleButton> (labelText);
    row->toggle->setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.85f));
    row->toggle->setColour (juce::ToggleButton::tickColourId, juce::Colours::orange);
    addAndMakeVisible (*row->toggle);

    row->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, paramId, *row->toggle);

    section.rows.push_back (std::move (row));
}

void ControlSidebar::addColourSwatch (Section& section, juce::AudioProcessorValueTreeState& state,
                                        const juce::String& paramId, const juce::String& labelText)
{
    auto row = std::make_unique<Row>();
    row->label = std::make_unique<juce::Label>();
    row->label->setText (labelText, juce::dontSendNotification);
    row->label->setFont (juce::Font (juce::FontOptions (12.0f)));
    row->label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (*row->label);

    if (auto* intParam = dynamic_cast<juce::AudioParameterInt*> (state.getParameter (paramId)))
    {
        row->colourSwatch = std::make_unique<ColourSwatchButton> (*intParam);
        addAndMakeVisible (*row->colourSwatch);
    }

    section.rows.push_back (std::move (row));
}

juce::TextButton& ControlSidebar::addActionButton (Section& section, const juce::String& labelText)
{
    auto row = std::make_unique<Row>();

    row->button = std::make_unique<juce::TextButton> (labelText);
    row->button->setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (30, 30, 36));
    row->button->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (*row->button);

    auto& button = *row->button;
    section.rows.push_back (std::move (row));
    return button;
}

void ControlSidebar::setSectionVisible (const juce::String& title, bool visible)
{
    for (auto& section : sections)
    {
        if (section.title != title)
            continue;

        if (section.visible == visible)
            return;

        section.visible = visible;
        for (auto& row : section.rows)
        {
            if (row->label  != nullptr) row->label->setVisible (visible);
            if (row->slider != nullptr) row->slider->setVisible (visible);
            if (row->combo  != nullptr) row->combo->setVisible (visible);
            if (row->toggle != nullptr) row->toggle->setVisible (visible);
            if (row->button != nullptr) row->button->setVisible (visible);
            if (row->colourSwatch != nullptr) row->colourSwatch->setVisible (visible);
        }

        resized();
        return;
    }
}

int ControlSidebar::rowHeightFor (const Row& row) const
{
    // Toggle and plain-button rows have no separate label line above the
    // control, so they're shorter than label+control rows. This is the
    // single source of truth for that -- paint() and resized() both call
    // it, so the section titles paint() draws can never drift out of sync
    // with where resized() actually put the controls (which happened
    // before when the two used their own separate hard-coded numbers).
    return (row.toggle != nullptr || row.button != nullptr) ? 30 : 46;
}

void ControlSidebar::paint (juce::Graphics& g)
{
    g.fillAll (themeIsWhite ? juce::Colour::fromRGB (235, 255, 255) : juce::Colour::fromRGB (18, 18, 22));

    int y = 10;
    g.setFont (juce::Font (juce::FontOptions (13.0f)).boldened());
    const auto titleColour = themeIsWhite ? juce::Colour::fromRGB (180, 90, 0) : juce::Colours::orange;
    for (auto& section : sections)
    {
        if (! section.visible)
            continue;

        g.setColour (titleColour.withAlpha (0.9f));
        g.drawText (section.title, 12, y, getWidth() - 24, 18, juce::Justification::left);
        y += 24;
        for (auto& row : section.rows)
            y += rowHeightFor (*row);
        y += 10;
    }
}

void ControlSidebar::resized()
{
    int y = 10;
    const int labelH = 16;

    for (auto& section : sections)
    {
        if (! section.visible)
            continue;

        y += 24; // space for section title, drawn in paint()

        for (auto& row : section.rows)
        {
            const int rowH = rowHeightFor (*row);

            if (row->toggle != nullptr)
            {
                row->toggle->setBounds (12, y + 6, getWidth() - 24, 22);
                y += rowH;
                continue;
            }

            if (row->button != nullptr)
            {
                row->button->setBounds (12, y + 4, getWidth() - 24, 22);
                y += rowH;
                continue;
            }

            row->label->setBounds (12, y, getWidth() - 24, labelH);
            const int controlY = y + labelH;
            if (row->slider != nullptr)
                row->slider->setBounds (12, controlY, getWidth() - 24, 22);
            else if (row->combo != nullptr)
                row->combo->setBounds (12, controlY, getWidth() - 24, 22);
            else if (row->colourSwatch != nullptr)
                row->colourSwatch->setBounds (12, controlY, 60, 22);

            y += rowH;
        }

        y += 10;
    }

    totalHeight = y + 10;
    setSize (getWidth(), totalHeight);
}

//==============================================================================
// EcoSeeAudioProcessorEditor
//==============================================================================
EcoSeeAudioProcessorEditor::EcoSeeAudioProcessorEditor (EcoSeeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), sidebar (p.apvts)
{
    setResizable (true, true);
    setSize (1200, 700); // includes the always-visible sidebar
    history.fill (SpectralFrame());

    sidebarViewport.setViewedComponent (&sidebar, false);
    sidebarViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (sidebarViewport);

    sidebar.getDragModeButton().onClick = [this] { cycleDragMode(); };

    startTimerHz (60);
}

EcoSeeAudioProcessorEditor::~EcoSeeAudioProcessorEditor()
{
    stopTimer();
}

void EcoSeeAudioProcessorEditor::resized()
{
    const int w = juce::jmax (1, getWidth());
    const int h = juce::jmax (1, getHeight());
    sidebarViewport.setBounds (0, 0, (int) kSidebarWidth, h);
    sidebar.setSize ((int) kSidebarWidth, sidebar.getPreferredHeight());

    if (trailImage.isNull() || trailImage.getWidth() != w || trailImage.getHeight() != h)
    {
        trailImage = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (trailImage);
        g.fillAll (paperColour());
    }
}

void EcoSeeAudioProcessorEditor::cycleDragMode()
{
    dragMode = (dragMode == DragMode::Rotate) ? DragMode::Move : DragMode::Rotate;
    sidebar.getDragModeButton().setButtonText (dragMode == DragMode::Rotate ? "Drag: Rotate" : "Drag: Move");
}

void EcoSeeAudioProcessorEditor::timerCallback()
{
    auto& apvts = processorRef.apvts;

    sidebar.updateTheme();

    // Sync target FPS from the parameter without recreating the timer needlessly.
    const int fpsIndex = (int)*apvts.getRawParameterValue("MO_FPS");
    const int fpsValues[3] = { 30, 45, 60 };
    const int fps = fpsValues[juce::jlimit(0, 2, fpsIndex)];
    if (fps != lastFps)
    {
        lastFps = fps;
        startTimerHz(fps);
    }

    processorRef.getHistorySnapshot (history);

    {
        // Ease the displayed periodogram toward the latest analysed frame's
        // cepstrum. This is what carries the "over time" information now --
        // the curve keeps moving as new audio comes in -- rather than a
        // second (time) axis on the plot itself.
        const float smoothingAmount = juce::jlimit(0.0f, 1.0f,
            (float)*apvts.getRawParameterValue("VS_CEPSTRUM_SMOOTH"));
        const float smoothAlpha = juce::jlimit(0.02f, 1.0f, 1.0f - smoothingAmount);
        const auto& latest = history[(size_t)(EcoSeeAudioProcessor::kHistorySize - 1)];

        // If the FFT Size parameter just changed, the coefficient count
        // (fftSize/2) changed with it -- snap straight to the new frame's
        // values that tick instead of easing, since interpolating between
        // buffers of two different lengths/meanings doesn't make sense.
        if (latest.cepstrumCount != displayCepstrumCount)
        {
            displayCepstrumCount = latest.cepstrumCount;
            for (int b = 0; b < displayCepstrumCount; ++b)
                displayCepstrum[(size_t)b] = latest.cepstrum[(size_t)b];
        }
        else
        {
            for (int b = 0; b < displayCepstrumCount; ++b)
                displayCepstrum[(size_t)b] += (latest.cepstrum[(size_t)b] - displayCepstrum[(size_t)b]) * smoothAlpha;
        }

        // Ease the Vocal Signature radar values with the same smoothing feel.
        const float radarTargets[5] = { latest.skewness01, latest.entropy01, latest.crest01,
                                          latest.slope01, latest.flatness01 };
        for (int k = 0; k < 5; ++k)
            radarSmoothed[(size_t)k] += (radarTargets[k] - radarSmoothed[(size_t)k]) * smoothAlpha;

        // A new analysis frame just landed if its centroid/amplitude moved --
        // flash the newest point white, then let it decay into its real colour.
        if (! juce::approximatelyEqual (latest.centroidHz, newestSeenFrame.centroidHz)
            || ! juce::approximatelyEqual (latest.amplitude01, newestSeenFrame.amplitude01))
        {
            newPointFlash = 1.0f;
            newestSeenFrame = latest;
        }
        else
        {
            newPointFlash = juce::jmax (0.0f, newPointFlash - 0.07f);
        }
    }

    const bool autoRotate = *apvts.getRawParameterValue("MO_AUTO_ROTATE") > 0.5f;
    if (autoRotate)
    {
        const float rotSpeedA = *apvts.getRawParameterValue("MO_ROT_A");
        const float rotSpeedB = *apvts.getRawParameterValue("MO_ROT_B");
        // While a panel is being actively dragged in Rotate mode, its base
        // angle is frozen so the drag grabs the rotation directly rather
        // than layering a phase offset on top of continuous auto-spin.
        if (!draggingRotateA)
            rotationA += rotSpeedA;
        if (!draggingRotateC)
            rotationB += rotSpeedB;
    }
    else
    {
        const float angleADeg = *apvts.getRawParameterValue("MO_ANGLE_A");
        const float angleBDeg = *apvts.getRawParameterValue("MO_ANGLE_B");
        rotationA = juce::degreesToRadians(angleADeg);
        rotationB = juce::degreesToRadians(angleBDeg);
    }

    // NOTE: the actual (expensive) panel/trail drawing used to happen right
    // here, unconditionally, on every single timer tick -- see paint() for
    // why that moved. Everything above this point is cheap bookkeeping only
    // (parameter reads, easing a handful of floats); it must stay that way,
    // since this runs on the message thread at up to 60Hz and anything
    // heavier than simple arithmetic here risks starving input handling on
    // that same thread even if frames still appear to update.
    repaint();
}

void EcoSeeAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    lastDragPos = e.position;

    if (panelBoundsA.contains (e.position))      dragPanelIndex = 0;
    else if (panelBoundsB.contains (e.position)) dragPanelIndex = 1;
    else if (panelBoundsC.contains (e.position)) dragPanelIndex = 2;
    else if (panelBoundsD.contains (e.position)) dragPanelIndex = 3;
    else                                          dragPanelIndex = -1;

    // Rotate mode only grabs the 3D panels (A, C); freeze that panel's
    // auto-rotation for the duration of the drag so the mouse takes over
    // the rotation directly instead of adding a phase offset on top of a
    // still-spinning base angle.
    if (dragMode == DragMode::Rotate)
    {
        draggingRotateA = (dragPanelIndex == 0);
        draggingRotateC = (dragPanelIndex == 2);
    }
}

void EcoSeeAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPanelIndex < 0)
        return;

    const auto delta = e.position - lastDragPos;
    lastDragPos = e.position;

    if (dragMode == DragMode::Rotate)
    {
        const float sensitivity = 0.012f;

        if (dragPanelIndex == 0)
        {
            userRotYA += delta.x * sensitivity;
            userRotXA += delta.y * sensitivity;
        }
        else if (dragPanelIndex == 2)
        {
            userRotYC += delta.x * sensitivity;
            userRotXC += delta.y * sensitivity;
        }
    }
    else // DragMode::Move -- pan whichever panel's content, any of the four
    {
        juce::Point<float>* panTarget = nullptr;
        const float* zoomForPanel = nullptr;

        switch (dragPanelIndex)
        {
            case 0: panTarget = &panA; zoomForPanel = &zoomA; break;
            case 1: panTarget = &panB; zoomForPanel = &zoomB; break;
            case 2: panTarget = &panC; zoomForPanel = &zoomC; break;
            case 3: panTarget = &panD; zoomForPanel = &zoomD; break;
            default: break;
        }

        if (panTarget != nullptr)
            *panTarget += delta / juce::jmax (0.01f, *zoomForPanel); // 1:1 with the cursor regardless of zoom
    }
}

void EcoSeeAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    dragPanelIndex = -1;
    draggingRotateA = false;
    draggingRotateC = false;
}

void EcoSeeAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    float* zoomTarget = nullptr;

    if (panelBoundsA.contains (e.position))      zoomTarget = &zoomA;
    else if (panelBoundsB.contains (e.position)) zoomTarget = &zoomB;
    else if (panelBoundsC.contains (e.position)) zoomTarget = &zoomC;
    else if (panelBoundsD.contains (e.position)) zoomTarget = &zoomD;

    if (zoomTarget != nullptr)
        *zoomTarget = juce::jlimit (0.4f, 4.0f, *zoomTarget + wheel.deltaY * 1.4f);
}

void EcoSeeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (paperColour());

    // All the actual (expensive) drawing lives here now, not in
    // timerCallback(). timerCallback() only updates cheap state and calls
    // repaint(); JUCE coalesces repeated repaint() requests into a single
    // paint() call, so if the system falls behind, frames get dropped
    // instead of the message thread being forced to run a full, heavy
    // render pass back-to-back on every single timer tick regardless of
    // whether anything else (like a queued mouse click) is waiting.
    auto& apvts = processorRef.apvts;

    if (trailImage.isNull())
        resized();

    const float trailFade = *apvts.getRawParameterValue("MO_TRAIL");

    {
        juce::Graphics tg(trailImage);

        if (*apvts.getRawParameterValue("VS_WHITE_BG") > 0.5f)
        {
            // White mode: never accumulate translucent remnants.
            // Fading white over previous pixels creates grey/bright ghosting.
            tg.setColour(juce::Colours::white);
            tg.fillRect(trailImage.getBounds());
        }
        else
        {
            // Black mode keeps the existing trail behaviour.
            tg.setColour(juce::Colours::black.withAlpha(trailFade));
            tg.fillRect(trailImage.getBounds());
        }
    }

    // The drag-mode toggle used to live in a dedicated top bar reserved here
    // via kTopBarHeight; now that it's a row inside the sidebar's VIEW
    // section, the whole area right of the sidebar is free for the panels.
    auto fullBounds = getLocalBounds().toFloat();
    auto bounds = fullBounds.withTrimmedLeft(kSidebarWidth).reduced(10.0f);

    // 0 = all four panels in a 2x2 grid, 1-4 = a single field shown full-size,
    // 5 = the Custom 3D Grid (user-assignable X/Y/Z fields).
    const int viewMode = (int) *apvts.getRawParameterValue ("VS_VIEW_MODE");

    const bool wantCustomGridControls = (viewMode == 5);
    if (wantCustomGridControls != customGridControlsVisible)
    {
        customGridControlsVisible = wantCustomGridControls;
        sidebar.setSectionVisible ("CUSTOM 3D GRID", customGridControlsVisible);
    }

    // When a single panel fills the view, the two 3D diagrams get an extra
    // scale boost (user-controlled via VS_MACRO_ZOOM, 0.5x-2x) on top of
    // simply occupying more screen space, so they can be pushed anywhere
    // from a subtle close-up to a genuinely giant, larger-than-life view.
    singlePanelScaleBoost = (viewMode == 0) ? 1.0f : (float) *apvts.getRawParameterValue ("VS_MACRO_ZOOM");

    {
        juce::Graphics tg(trailImage);
        // Clear the strip under the sidebar so panel content never bleeds beneath it.
        tg.setColour(paperColour());
        tg.fillRect(juce::Rectangle<float>(0, 0, kSidebarWidth, fullBounds.getHeight()));

        // Zoom (and pan) only affect the plotted content: it's clipped to the
        // panel's own rectangle and transformed inside that clip, so nothing
        // bleeds into neighbouring panels. The border + title are drawn
        // afterwards, outside the transform, so they stay put regardless of
        // zoom/pan level.
        auto drawZoomed = [&tg](juce::Rectangle<float> area, float zoom, juce::Point<float> pan, auto&& drawFn)
            {
                tg.saveState();
                tg.reduceClipRegion(area.getSmallestIntegerContainer());
                if (std::abs(zoom - 1.0f) > 0.001f || pan != juce::Point<float>())
                {
                    const auto centre = area.getCentre();
                    tg.addTransform(juce::AffineTransform::translation(pan.x, pan.y)
                        .followedBy(juce::AffineTransform::scale(zoom, zoom, centre.x, centre.y)));
                }
                drawFn();
                tg.restoreState();
            };

        const int panelDMode = (int) *apvts.getRawParameterValue ("VS_PANEL_D_MODE");

        if (viewMode == 0)
        {
            // --- 2x2 Grid Layout ---
            const float panelGap = 10.0f;
            const float panelW = (bounds.getWidth() - panelGap) / 2.0f;
            const float panelH = (bounds.getHeight() - panelGap) / 2.0f;

            auto topRow = bounds.removeFromTop(panelH);
            bounds.removeFromTop(panelGap); // Shift down past the horizontal gap

            auto areaA = topRow.removeFromLeft(panelW);
            topRow.removeFromLeft(panelGap); // Shift right past the vertical gap
            auto areaB = topRow;

            auto areaC = bounds.removeFromLeft(panelW);
            bounds.removeFromLeft(panelGap); // Shift right past the vertical gap
            auto areaD = bounds;

            panelBoundsA = areaA;
            panelBoundsB = areaB;
            panelBoundsC = areaC;
            panelBoundsD = areaD;

            drawZoomed(areaA, zoomA, panA, [&] { drawSpreadEntropySpace(tg, areaA); });
            drawPanelFrame(tg, areaA, "Spread / Entropy");

            drawZoomed(areaB, zoomB, panB, [&] { drawToneMap(tg, areaB); });
            drawPanelFrame(tg, areaB, "Tone Map");

            drawZoomed(areaC, zoomC, panC, [&] { drawFmAmCube(tg, areaC); });
            drawPanelFrame(tg, areaC, "FM / AM Cube");

            drawZoomed(areaD, zoomD, panD, [&]
            {
                if (panelDMode == 0)
                    drawCepstralPeriodogram (tg, areaD);
                else
                    drawVocalSignatureRadar (tg, areaD);
            });
            drawPanelFrame(tg, areaD, panelDMode == 0 ? "Cepstrogram" : "Vocal Signature");
        }
        else
        {
            // --- Single field, full-size ---
            panelBoundsA = panelBoundsB = panelBoundsC = panelBoundsD = juce::Rectangle<float>();

            switch (viewMode)
            {
                case 1:
                    panelBoundsA = bounds;
                    drawZoomed(bounds, zoomA, panA, [&] { drawSpreadEntropySpace(tg, bounds); });
                    drawPanelFrame(tg, bounds, "Spread / Entropy");
                    break;

                case 2:
                    panelBoundsB = bounds;
                    drawZoomed(bounds, zoomB, panB, [&] { drawToneMap(tg, bounds); });
                    drawPanelFrame(tg, bounds, "Tone Map");
                    break;

                case 3:
                    panelBoundsC = bounds;
                    drawZoomed(bounds, zoomC, panC, [&] { drawFmAmCube(tg, bounds); });
                    drawPanelFrame(tg, bounds, "FM / AM Cube");
                    break;

                case 4:
                    panelBoundsD = bounds;
                    drawZoomed(bounds, zoomD, panD, [&]
                    {
                        if (panelDMode == 0)
                            drawCepstralPeriodogram (tg, bounds);
                        else
                            drawVocalSignatureRadar (tg, bounds);
                    });
                    drawPanelFrame(tg, bounds, panelDMode == 0 ? "Cepstrogram" : "Vocal Signature");
                    break;

                case 5:
                default:
                    // Reuses panel A's rotation/drag/zoom state slot, since
                    // the grid panel A (Spread/Entropy) isn't shown at the
                    // same time as this one.
                    panelBoundsA = bounds;
                    drawZoomed(bounds, zoomA, panA, [&] { drawCustomGrid3D(tg, bounds); });
                    drawPanelFrame(tg, bounds, "Custom 3D Grid");
                    break;
            }
        }
    }

    if (trailImage.isValid())
        g.drawImageAt (trailImage, 0, 0);
}

//==============================================================================
juce::Colour EcoSeeAudioProcessorEditor::paperColour() const
{
    const bool whiteBg = *processorRef.apvts.getRawParameterValue ("VS_WHITE_BG") > 0.5f;
    return whiteBg ? juce::Colours::white : juce::Colours::black;
}

juce::Colour EcoSeeAudioProcessorEditor::ink (float alpha) const
{
    const bool whiteBg = *processorRef.apvts.getRawParameterValue ("VS_WHITE_BG") > 0.5f;
    return (whiteBg ? juce::Colours::black : juce::Colours::white).withAlpha (alpha);
}

void EcoSeeAudioProcessorEditor::maybeLabelPoint (juce::Graphics& g, juce::Point<float> pos, Vec3f coords, int index, bool everyN)
{
    const bool labelPoints = *processorRef.apvts.getRawParameterValue ("VS_LABEL_POINTS") > 0.5f;
    if (! labelPoints)
        return;
    if (everyN && (index % 6 != 0))
        return;

    auto fmt = [] (float v)
    {
        return juce::String (v, std::abs (v) < 10.0f ? 2 : 0);
    };

    const juce::String text = "[" + fmt (coords.x) + ", " + fmt (coords.y) + ", " + fmt (coords.z) + "]";

    // Snap to whole pixels and paint a small backing plate behind the text.
    // Previously this drew a bare index number directly onto the
    // continuously fading trail image at a sub-pixel position that shifts
    // every frame (auto-rotation, moving data); the overlapping
    // antialiased glyphs from consecutive frames blended into what read as
    // "flickering, rapidly-changing digits". Rounding the position and
    // giving the text an opaque-ish backing plate keeps each frame's label
    // crisp and legible instead of smearing into the previous one.
    const float textW = 14.0f + text.length() * 5.3f;
    const juce::Rectangle<float> plate (std::round (pos.x + 4.0f), std::round (pos.y - 13.0f), textW, 13.0f);

    g.setColour (paperColour().withAlpha (0.6f));
    g.fillRoundedRectangle (plate, 2.0f);

    g.setFont (juce::Font (juce::FontOptions (9.5f)));
    g.setColour (ink (0.9f));
    g.drawText (text, plate, juce::Justification::left);
}

//==============================================================================
juce::Point<float> EcoSeeAudioProcessorEditor::project (Vec3f p, float rotY, float rotX,
                                                                  juce::Point<float> origin, float scale) const
{
    const float cosY = std::cos (rotY), sinY = std::sin (rotY);
    const float x1 = p.x * cosY - p.z * sinY;
    const float z1 = p.x * sinY + p.z * cosY;

    const float cosX = std::cos (rotX), sinX = std::sin (rotX);
    const float y1 = p.y * cosX - z1 * sinX;
    const float z2 = p.y * sinX + z1 * cosX;

    const float perspective = 1.0f / (2.2f + z2 * 0.6f);
    const float sx = origin.x + x1 * scale * perspective;
    const float sy = origin.y - y1 * scale * perspective;
    return { sx, sy };
}

void EcoSeeAudioProcessorEditor::glowDot (juce::Graphics& g, juce::Point<float> pos,
                                                   float radius, juce::Colour colour)
{
    const float glowSizeMul = *processorRef.apvts.getRawParameterValue ("VS_GLOW_SIZE");
    const float glowIntensity = *processorRef.apvts.getRawParameterValue ("VS_GLOW_INTENSITY");
    const float pointSizeMul = *processorRef.apvts.getRawParameterValue ("VS_POINT_SIZE");
    const bool whiteBg = *processorRef.apvts.getRawParameterValue ("VS_WHITE_BG") > 0.5f;

    const float r = radius * pointSizeMul;

    // These layers are meant to read as a soft "glow": each translucent
    // ring, composited over BLACK, pushes brightness up towards the data
    // colour. Composited over WHITE, the exact same maths does the
    // opposite -- it pulls brightness down towards the colour -- so the
    // wide, faint outer rings don't look like a glow there, they look like
    // a grey/muddy shadow underneath the point. On a white background we
    // drop the widest, faintest rings and keep a tighter, punchier core.
    const int   startLayer    = whiteBg ? 1 : 4;
    const float outerAlphaMul = whiteBg ? 0.5f : 1.0f;

    for (int layer = startLayer; layer >= 0; --layer)
    {
        const float layerRadius = r * glowSizeMul * (1.0f + (float) layer * 1.6f);
        float alpha = colour.getFloatAlpha() * glowIntensity * (layer == 0 ? 0.9f : 0.10f / (float) layer);
        if (layer != 0)
            alpha *= outerAlphaMul;
        g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
        g.fillEllipse (pos.x - layerRadius, pos.y - layerRadius, layerRadius * 2.0f, layerRadius * 2.0f);
    }
}

void EcoSeeAudioProcessorEditor::glowLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b,
                                                    juce::Colour colour, float thickness)
{
    const float lineMul = *processorRef.apvts.getRawParameterValue ("VS_LINE_THICK");
    const bool whiteBg = *processorRef.apvts.getRawParameterValue ("VS_WHITE_BG") > 0.5f;
    const float t = thickness * lineMul;

    // Same reasoning as glowDot() above: the very wide, very faint outer
    // stroke reads as more of a shadow than a glow once the canvas is
    // white, so it's thinned down there rather than dropped entirely --
    // keeps a bit of glow while staying legible.
    g.setColour (colour.withAlpha (whiteBg ? 0.05f : 0.10f));
    g.drawLine ({ a, b }, t * (whiteBg ? 1.8f : 3.0f));

    g.setColour (colour.withAlpha (whiteBg ? 0.55f : 0.35f));
    g.drawLine ({ a, b }, t);
}

void EcoSeeAudioProcessorEditor::fineWhiteLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b)
{
    g.setColour (ink (0.35f));
    g.drawLine ({ a, b }, 0.5f);
}

int EcoSeeAudioProcessorEditor::visibleHistoryStart() const
{
    const int total = EcoSeeAudioProcessor::kHistorySize;
    const int count = juce::jlimit (1, total, (int) *processorRef.apvts.getRawParameterValue ("VS_POINT_COUNT"));
    return total - count;
}

void EcoSeeAudioProcessorEditor::drawSnakeTrail (juce::Graphics& g, const std::vector<juce::Point<float>>& pts,
                                                  const std::vector<juce::Colour>& colours,
                                                  const std::vector<float>& radii,
                                                  bool useGlowLine, float lineThickness,
                                                  float tailBrightness, float headBrightness)
{
    const int n = (int) pts.size();
    if (n == 0)
        return;

    if (n == 1)
    {
        // Slider at 1 (or a run with just one audible point): a single
        // point, no connecting curve at all.
        glowDot (g, pts[0], radii[0], colours[0]);
        return;
    }

    // Catmull-Rom spline through the visible points, subdivided into
    // several steps per real segment so the snake glides continuously
    // between actual datapoints instead of visiting them as sharp corners.
    constexpr int kSubSteps = 10;
    auto catmullRom = [] (juce::Point<float> p0, juce::Point<float> p1,
                          juce::Point<float> p2, juce::Point<float> p3, float t) -> juce::Point<float>
    {
        const float t2 = t * t, t3 = t2 * t;
        auto blend = [t, t2, t3] (float a, float b, float c, float d)
        {
            return 0.5f * ((2.0f * b) + (-a + c) * t
                           + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2
                           + (-a + 3.0f * b - 3.0f * c + d) * t3);
        };
        return { blend (p0.x, p1.x, p2.x, p3.x), blend (p0.y, p1.y, p2.y, p3.y) };
    };

    const int totalSteps = (n - 1) * kSubSteps;
    juce::Point<float> prevSample;
    bool havePrev = false;

    for (int step = 0; step <= totalSteps; ++step)
    {
        const float u = (float) step / (float) kSubSteps;
        const int seg = juce::jlimit (0, n - 2, (int) u);
        const float t = u - (float) seg;

        const auto& p1 = pts[(size_t) seg];
        const auto& p2 = pts[(size_t) (seg + 1)];
        const auto& p0 = pts[(size_t) juce::jmax (0, seg - 1)];
        const auto& p3 = pts[(size_t) juce::jmin (n - 1, seg + 2)];

        const auto sample = catmullRom (p0, p1, p2, p3, t);
        const auto segColour = colours[(size_t) seg].interpolatedWith (colours[(size_t) (seg + 1)], t);

        // Fade tail-brightness -> head-brightness along the whole trail
        // (both user-controlled), so the shape reads as a moving snake
        // rather than a static curve.
        const float posFrac = (float) step / (float) juce::jmax (1, totalSteps);
        const float tailAlpha = tailBrightness + (headBrightness - tailBrightness) * posFrac;
        const auto colour = segColour.withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, tailAlpha));

        if (havePrev)
        {
            if (useGlowLine)
                glowLine (g, prevSample, sample, colour, lineThickness);
            else
            {
                g.setColour (colour);
                g.drawLine ({ prevSample, sample }, lineThickness);
            }
        }

        prevSample = sample;
        havePrev = true;
    }

    // The snake's glowing head, right at the newest point.
    glowDot (g, pts.back(), radii.back() * 1.15f, colours.back().withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, headBrightness)));
}

void EcoSeeAudioProcessorEditor::flushTrailRun (juce::Graphics& g, std::vector<juce::Point<float>>& pts,
                                                 std::vector<juce::Colour>& colours, std::vector<float>& radii,
                                                 bool snakeMode, bool useGlowLine, float lineThickness)
{
    if (pts.empty())
        return;

    if (snakeMode)
    {
        const float tailBright = *processorRef.apvts.getRawParameterValue ("VS_SNAKE_TAIL_BRIGHT");
        const float headBright = *processorRef.apvts.getRawParameterValue ("VS_SNAKE_HEAD_BRIGHT");
        drawSnakeTrail (g, pts, colours, radii, useGlowLine, lineThickness, tailBright, headBright);
    }
    else
    {
        for (size_t k = 0; k < pts.size(); ++k)
        {
            if (k > 0)
            {
                if (useGlowLine)
                    glowLine (g, pts[k - 1], pts[k], colours[k], lineThickness);
                fineWhiteLine (g, pts[k - 1], pts[k]);
            }
            glowDot (g, pts[k], radii[k], colours[k]);
        }
    }

    pts.clear();
    colours.clear();
    radii.clear();
}

void EcoSeeAudioProcessorEditor::drawAxisGuides3D (juce::Graphics& g, juce::Point<float> origin, float scale,
                                                    float rotY, float rotX,
                                                    const juce::String& xLabel, const juce::String& yLabel,
                                                    const juce::String& zLabel,
                                                    float xMin, float xMax, float yMin, float yMax,
                                                    float zMin, float zMax, bool zNumeric)
{
    struct AxisDef { Vec3f dir; juce::String label; float rangeMin; float rangeMax; bool numeric; };
    const AxisDef axes[3] = {
        { { 1.0f, 0.0f, 0.0f }, xLabel, xMin, xMax, true },
        { { 0.0f, 1.0f, 0.0f }, yLabel, yMin, yMax, true },
        { { 0.0f, 0.0f, 1.0f }, zLabel, zMin, zMax, zNumeric }
    };

    for (const auto& ax : axes)
    {
        const Vec3f negDir { -ax.dir.x, -ax.dir.y, -ax.dir.z };
        const auto negP = project (negDir, rotY, rotX, origin, scale);
        const auto posP = project (ax.dir, rotY, rotX, origin, scale);

        g.setColour (ink (0.24f));
        g.drawLine ({ negP, posP }, 1.0f);

        // A handful of tick marks + numeric labels along the axis, matching
        // the reference visualiser's densely-labelled grid rather than just
        // the two endpoints.
        if (ax.numeric)
        {
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (int t = 0; t <= 4; ++t)
            {
                const float tn = (float) t / 4.0f;
                const Vec3f tickDir { ax.dir.x * (tn * 2.0f - 1.0f), ax.dir.y * (tn * 2.0f - 1.0f), ax.dir.z * (tn * 2.0f - 1.0f) };
                const auto tickP = project (tickDir, rotY, rotX, origin, scale);
                const float tickVal = ax.rangeMin + tn * (ax.rangeMax - ax.rangeMin);

                g.setColour (ink (0.30f));
                g.fillEllipse (tickP.x - 1.2f, tickP.y - 1.2f, 2.4f, 2.4f);

                g.setColour (ink (0.42f));
                g.drawText (juce::String (tickVal, tickVal < 10.0f ? 2 : 0), tickP.x - 22.0f, tickP.y + 3.0f, 44.0f, 12.0f,
                            juce::Justification::centred);
            }
        }

        g.setFont (juce::Font (juce::FontOptions (11.5f)).boldened());
        g.setColour (ink (0.7f));
        g.drawText (ax.label, juce::Rectangle<float> (posP.x - 55.0f, posP.y - 22.0f, 110.0f, 16.0f),
                    juce::Justification::centred);
    }
}

juce::Colour EcoSeeAudioProcessorEditor::schemeColour (float n) const
{
    n = juce::jlimit (0.0f, 1.0f, n);

    const auto low  = packedToColour ((int) *processorRef.apvts.getRawParameterValue ("CS_LOW_RGB"));
    const auto mid  = packedToColour ((int) *processorRef.apvts.getRawParameterValue ("CS_MID_RGB"));
    const auto high = packedToColour ((int) *processorRef.apvts.getRawParameterValue ("CS_HIGH_RGB"));

    if (n < 0.5f)
        return low.interpolatedWith (mid, n * 2.0f);
    return mid.interpolatedWith (high, (n - 0.5f) * 2.0f);
}

juce::Colour EcoSeeAudioProcessorEditor::frequencyToColour (float n) const
{
    return schemeColour (n);
}

juce::Colour EcoSeeAudioProcessorEditor::flatnessToColour (float n) const
{
    return schemeColour (n);
}

juce::Colour EcoSeeAudioProcessorEditor::cepstrumColour (float n) const
{
    // Classic periodogram/spectrogram feel: darker at low energy, ramping up
    // through the user's chosen low/mid/high scheme as energy rises.
    n = juce::jlimit (0.0f, 1.0f, n);
    const float val = juce::jlimit (0.0f, 1.0f, std::pow (n, 0.55f));
    return schemeColour (n).withMultipliedBrightness (juce::jmax (0.25f, val));
}

bool EcoSeeAudioProcessorEditor::isAudible (const SpectralFrame& f) const
{
    const bool hideQuiet = *processorRef.apvts.getRawParameterValue ("VS_HIDE_QUIET") > 0.5f;
    if (! hideQuiet)
        return true;

    // Generous threshold so faint sounds still draw -- only true silence /
    // noise-floor hiss below this gets hidden.
    constexpr float kQuietThresholdDb = -90.0f;
    return f.levelDb > kQuietThresholdDb;
}

void EcoSeeAudioProcessorEditor::adaptiveRange (const std::function<float (const SpectralFrame&, int)>& getter,
                                                 float nominalMin, float nominalMax,
                                                 float& outMin, float& outMax) const
{
    if (! (*processorRef.apvts.getRawParameterValue ("VS_NORMALIZE") > 0.5f))
    {
        outMin = nominalMin;
        outMax = nominalMax;
        return;
    }

    float mn = std::numeric_limits<float>::max();
    float mx = -std::numeric_limits<float>::max();

    for (int i = 0; i < (int) history.size(); ++i)
    {
        const auto& f = history[(size_t) i];
        if (! isAudible (f))
            continue;

        const float v = getter (f, i);
        mn = juce::jmin (mn, v);
        mx = juce::jmax (mx, v);
    }

    if (mx > mn)
    {
        outMin = mn;
        outMax = mx;
    }
    else
    {
        // No audible data yet, or the field is genuinely constant right now
        // -- keep the nominal range rather than dividing by a zero width.
        outMin = nominalMin;
        outMax = nominalMax;
    }
}

void EcoSeeAudioProcessorEditor::drawPanelFrame (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title)
{
    // Fixed chrome: never scaled or panned, always drawn after the panel's
    // zoomed/panned content so it sits on top and stays put.
    g.setColour (ink (0.28f));
    g.drawRoundedRectangle (area, 4.0f, 1.0f);

    const auto titleArea = juce::Rectangle<float> (area.getX() + 6.0f, area.getY() + 4.0f, area.getWidth() - 12.0f, 18.0f);
    g.setColour (paperColour().withAlpha (0.6f));
    g.fillRect (titleArea);
    g.setColour (juce::Colours::orange.withAlpha (0.9f));
    g.setFont (juce::Font (juce::FontOptions (12.5f)).boldened());
    g.drawText (title, titleArea, juce::Justification::left);
}

//==============================================================================
void EcoSeeAudioProcessorEditor::drawSpreadEntropySpace (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto origin = area.getCentre();
    const float userScale = *processorRef.apvts.getRawParameterValue ("VS_SCALE");
    const float scale = juce::jmin (area.getWidth(), area.getHeight()) * 0.85f * userScale * singlePanelScaleBoost;
    const int n = EcoSeeAudioProcessor::kHistorySize;

    const float spreadMax = *processorRef.apvts.getRawParameterValue ("RG_SPREAD_MAX");
    const bool showAxes = *processorRef.apvts.getRawParameterValue ("VS_SHOW_AXES") > 0.5f;

    float spreadLo, spreadHi, entropyLo, entropyHi;
    adaptiveRange ([] (const SpectralFrame& f, int) { return f.spreadHz; },  0.0f, spreadMax, spreadLo, spreadHi);
    adaptiveRange ([] (const SpectralFrame& f, int) { return f.entropy01; }, 0.0f, 1.0f,       entropyLo, entropyHi);

    const float rotY = rotationA + userRotYA;
    const float rotX = 0.5f + userRotXA;

    if (showAxes)
        drawAxisGuides3D (g, origin, scale, rotY, rotX,
                           "Spectral Spread (Hz)", "Spectral Entropy", "Time",
                           spreadLo, spreadHi, entropyLo, entropyHi);

    const int startIdx = visibleHistoryStart();
    const bool snakeMode = *processorRef.apvts.getRawParameterValue ("VS_SNAKE_MODE") > 0.5f;

    std::vector<juce::Point<float>> runPts;
    std::vector<juce::Colour> runColours;
    std::vector<float> runRadii;

    for (int i = 0; i < n; ++i)
    {
        if (i < startIdx)
            continue; // hidden by the Visible Points slider

        const auto& f = history[(size_t) i];

        if (! isAudible (f))
        {
            flushTrailRun (g, runPts, runColours, runRadii, snakeMode, true, 1.2f);
            continue;
        }

        const float spreadN  = juce::jlimit (0.0f, 1.0f, (f.spreadHz - spreadLo) / juce::jmax (1.0f, spreadHi - spreadLo));
        const float entropyN = juce::jlimit (0.0f, 1.0f, (f.entropy01 - entropyLo) / juce::jmax (0.0001f, entropyHi - entropyLo));
        const float timeN    = (float) i / (float) (n - 1);

        Vec3f p3 { (spreadN - 0.5f) * 2.0f, (entropyN - 0.5f) * 2.0f, (timeN - 0.5f) * 2.0f };

        const auto sp = project (p3, rotY, rotX, origin, scale);
        auto colour = frequencyToColour (f.amplitude01);
        const bool isNewest = (i == n - 1);
        if (isNewest && newPointFlash > 0.001f)
            colour = colour.interpolatedWith (juce::Colours::white, newPointFlash);

        runPts.push_back (sp);
        runColours.push_back (colour);
        runRadii.push_back (1.9f + f.amplitude01 * 3.0f);

        maybeLabelPoint (g, sp, Vec3f { f.spreadHz, f.entropy01, (float) i }, i);
    }

    flushTrailRun (g, runPts, runColours, runRadii, snakeMode, true, 1.2f);
}

void EcoSeeAudioProcessorEditor::drawToneMap (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float userScale = *processorRef.apvts.getRawParameterValue ("VS_SCALE");
    auto plot = area.reduced (18.0f);
    // Growing the plot slightly beyond the panel via VS_SCALE gives the same
    // "fills more of the frame" feel the 3D panels get from their scale term.
    plot = plot.expanded ((userScale - 1.0f) * 0.5f * plot.getWidth(), (userScale - 1.0f) * 0.5f * plot.getHeight());

    const float freqMin = *processorRef.apvts.getRawParameterValue ("RG_FREQ_MIN");
    const float freqMax = juce::jmax (freqMin + 1.0f, (float) *processorRef.apvts.getRawParameterValue ("RG_FREQ_MAX"));
    const bool showAxes = *processorRef.apvts.getRawParameterValue ("VS_SHOW_AXES") > 0.5f;

    if (showAxes)
    {
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        // A denser grid of frequency and amplitude gridlines + labels, rather
        // than just the four corner values.
        for (int t = 0; t <= 5; ++t)
        {
            const float tn = (float) t / 5.0f;
            const float x = plot.getX() + tn * plot.getWidth();
            const float freqVal = freqMin + tn * (freqMax - freqMin);

            g.setColour (ink (0.10f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 0.5f);
            g.setColour (ink (0.45f));
            g.drawText (juce::String ((int) freqVal) + "Hz", x - 24.0f, plot.getBottom() + 4.0f, 48.0f, 12.0f,
                        juce::Justification::centred);

            const float y = plot.getBottom() - tn * plot.getHeight();
            g.setColour (ink (0.10f));
            g.drawLine (plot.getX(), y, plot.getRight(), y, 0.5f);
            g.setColour (ink (0.45f));
            g.drawText (juce::String (tn, 2), plot.getX() - 26.0f, y - 6.0f, 24.0f, 12.0f,
                        juce::Justification::right);
        }

        g.setFont (juce::Font (juce::FontOptions (11.5f)).boldened());
        g.setColour (ink (0.7f));
        g.drawText ("SPECTRAL CENTROID", plot.getCentreX() - 70.0f, plot.getBottom() + 16.0f, 140.0f, 14.0f,
                    juce::Justification::centred);
    }

    const int n = (int) history.size();
    const int startIdx = visibleHistoryStart();
    const bool snakeMode = *processorRef.apvts.getRawParameterValue ("VS_SNAKE_MODE") > 0.5f;

    std::vector<juce::Point<float>> runPts;
    std::vector<juce::Colour> runColours;
    std::vector<float> runRadii;

    for (int i = 0; i < n; ++i)
    {
        if (i < startIdx)
            continue; // hidden by the Visible Points slider

        const auto& f = history[(size_t) i];

        if (! isAudible (f))
        {
            flushTrailRun (g, runPts, runColours, runRadii, snakeMode, false, 1.5f);
            continue;
        }

        const float xN = juce::jlimit (0.0f, 1.0f, (f.centroidHz - freqMin) / (freqMax - freqMin));
        const float yN = juce::jlimit (0.0f, 1.0f, f.amplitude01);

        const juce::Point<float> pos (plot.getX() + xN * plot.getWidth(),
                                       plot.getBottom() - yN * plot.getHeight());

        auto colour = frequencyToColour (xN);
        const bool isNewest = (i == n - 1);
        if (isNewest && newPointFlash > 0.001f)
            colour = colour.interpolatedWith (juce::Colours::white, newPointFlash);

        runPts.push_back (pos);
        runColours.push_back (colour);
        runRadii.push_back (1.5f + yN * 2.4f);

        maybeLabelPoint (g, pos, Vec3f { f.centroidHz, f.amplitude01, (float) i }, i);
    }

    flushTrailRun (g, runPts, runColours, runRadii, snakeMode, false, 1.5f);
}

void EcoSeeAudioProcessorEditor::drawFmAmCube (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto origin = area.getCentre();
    const float userScale = *processorRef.apvts.getRawParameterValue ("VS_SCALE");
    const float scale = juce::jmin (area.getWidth(), area.getHeight()) * 0.85f * userScale * singlePanelScaleBoost;
    const int n = EcoSeeAudioProcessor::kHistorySize;

    const float freqMin = *processorRef.apvts.getRawParameterValue ("RG_FREQ_MIN");
    const float freqMax = juce::jmax (freqMin + 1.0f, (float) *processorRef.apvts.getRawParameterValue ("RG_FREQ_MAX"));
    const float amModScale = *processorRef.apvts.getRawParameterValue ("RG_AMMOD_SCALE");
    const bool showAxes = *processorRef.apvts.getRawParameterValue ("VS_SHOW_AXES") > 0.5f;

    // amMod is a frame-to-frame delta, so its getter needs the previous
    // frame too; adaptiveRange's index parameter lets it look that up via
    // `history` directly (matching the same i=1.. start the draw loop below
    // uses -- frame 0 has no previous frame to diff against).
    float freqLo, freqHi, amModLo, amModHi;
    adaptiveRange ([] (const SpectralFrame& f, int) { return f.centroidHz; }, freqMin, freqMax, freqLo, freqHi);
    adaptiveRange ([this, amModScale] (const SpectralFrame& f, int i)
                   {
                       if (i == 0) return 0.0f;
                       return (f.amplitude01 - history[(size_t) (i - 1)].amplitude01) * amModScale;
                   }, -1.0f, 1.0f, amModLo, amModHi);

    const float rotY = rotationB + userRotYC;
    const float rotX = 0.45f + userRotXC;

    if (showAxes)
        drawAxisGuides3D (g, origin, scale, rotY, rotX,
                           "Spectral Centroid (Hz)", "AM Modulation", "Time",
                           freqLo, freqHi, amModLo, amModHi);

    const int startIdx = visibleHistoryStart();
    const bool snakeMode = *processorRef.apvts.getRawParameterValue ("VS_SNAKE_MODE") > 0.5f;

    std::vector<juce::Point<float>> runPts;
    std::vector<juce::Colour> runColours;
    std::vector<float> runRadii;

    for (int i = 1; i < n; ++i)
    {
        if (i < startIdx)
            continue; // hidden by the Visible Points slider

        const auto& f  = history[(size_t) i];
        const auto& fp = history[(size_t) (i - 1)];

        if (! isAudible (f))
        {
            flushTrailRun (g, runPts, runColours, runRadii, snakeMode, false, 1.5f);
            continue;
        }

        const float freqN = juce::jlimit (0.0f, 1.0f, (f.centroidHz - freqLo) / juce::jmax (1.0f, freqHi - freqLo));
        const float timeN = (float) i / (float) (n - 1);
        const float amModRaw = (f.amplitude01 - fp.amplitude01) * amModScale;
        const float amMod = juce::jlimit (-1.0f, 1.0f,
            2.0f * (amModRaw - amModLo) / juce::jmax (0.0001f, amModHi - amModLo) - 1.0f);

        Vec3f p3 { (freqN - 0.5f) * 2.0f, amMod, (timeN - 0.5f) * 2.0f };

        const auto sp = project (p3, rotY, rotX, origin, scale);
        auto colour = flatnessToColour (f.flatness01);
        const bool isNewest = (i == n - 1);
        if (isNewest && newPointFlash > 0.001f)
            colour = colour.interpolatedWith (juce::Colours::white, newPointFlash);

        runPts.push_back (sp);
        runColours.push_back (colour);
        runRadii.push_back (1.6f + f.amplitude01 * 2.4f);

        maybeLabelPoint (g, sp, Vec3f { f.centroidHz, amModRaw, (float) i }, i);
    }

    flushTrailRun (g, runPts, runColours, runRadii, snakeMode, false, 1.5f);
}

void EcoSeeAudioProcessorEditor::drawCepstralPeriodogram (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Panel border + title are drawn separately by drawPanelFrame, outside
    // the zoom/pan transform.
    auto plot = area.reduced (24.0f, 20.0f);
    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return;

    // Standard periodogram layout: quefrency along X, magnitude along Y.
    // Line only (no fill), redrawn fresh from the smoothed,
    // continuously-animating buffer every tick so it keeps moving as new
    // audio comes in.
    //
    // IMPORTANT: cepCount scales with the FFT Size parameter, up to
    // kMaxCepstrumSize (4096) at FFT Size = 8192. Drawing one full glowDot()
    // (5 alpha-blended ellipse layers) PER BIN, every tick at up to 60Hz,
    // means up to ~4096 * 5 = ~20000 fills/second for this panel alone --
    // enough sustained software-rendering load on the message thread to
    // eventually starve the message pump (mouse clicks stop registering)
    // even though nothing has actually crashed or deadlocked. We therefore
    // cap the number of drawn points to kMaxDisplayPoints regardless of the
    // underlying FFT size, subsampling evenly across the real bin range so
    // the shape is preserved and the cost stays constant no matter which
    // FFT Size the user picks.
    juce::Path linePath;
    juce::Point<float> prev;

    const int cepCount = juce::jmax (1, displayCepstrumCount);
    constexpr int kMaxDisplayPoints = 512;
    const int step = juce::jmax (1, (cepCount + kMaxDisplayPoints - 1) / kMaxDisplayPoints);

    const bool labelPoints = *processorRef.apvts.getRawParameterValue ("VS_LABEL_POINTS") > 0.5f;
    int drawnIndex = 0;

    for (int b = 0; b < cepCount; b += step, ++drawnIndex)
    {
        const float v = juce::jlimit (0.0f, 1.0f, displayCepstrum[(size_t) b]);
        const float xN = (float) b / (float) juce::jmax (1, cepCount - 1);
        const juce::Point<float> pos (plot.getX() + xN * plot.getWidth(),
                                       plot.getBottom() - v * plot.getHeight());

        if (drawnIndex == 0)
        {
            linePath.startNewSubPath (pos);
        }
        else
        {
            linePath.lineTo (pos);
            fineWhiteLine (g, prev, pos);
        }

        glowDot (g, pos, 1.0f + v * 1.6f, cepstrumColour (v));

        if (labelPoints && (b % 32 == 0))
            maybeLabelPoint (g, pos, Vec3f { (float) b, v, 0.0f }, b, false);

        prev = pos;
    }

    g.setColour (cepstrumColour (0.9f).withAlpha (0.8f));
    g.strokePath (linePath, juce::PathStrokeType (1.4f));

    const bool showAxes = *processorRef.apvts.getRawParameterValue ("VS_SHOW_AXES") > 0.5f;
    if (showAxes)
    {
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        for (int t = 0; t <= 4; ++t)
        {
            const float tn = (float) t / 4.0f;
            const float x = plot.getX() + tn * plot.getWidth();
            g.setColour (ink (0.10f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 0.5f);
            g.setColour (ink (0.45f));
            g.drawText (juce::String ((int) (tn * cepCount)), x - 20.0f, plot.getBottom() + 4.0f, 40.0f, 12.0f,
                        juce::Justification::centred);

            const float y = plot.getBottom() - tn * plot.getHeight();
            g.setColour (ink (0.45f));
            g.drawText (juce::String (tn, 2), plot.getX() - 26.0f, y - 6.0f, 24.0f, 12.0f, juce::Justification::right);
        }

        g.setFont (juce::Font (juce::FontOptions (11.5f)).boldened());
        g.setColour (ink (0.7f));
        g.drawText ("QUEFRENCY", plot.getCentreX() - 50.0f, plot.getBottom() + 16.0f, 100.0f, 14.0f,
                    juce::Justification::centred);
    }
}

void EcoSeeAudioProcessorEditor::drawVocalSignatureRadar (juce::Graphics& g, juce::Rectangle<float> area)
{
    // A circular ("spider"/radar) viewer of five timbral descriptors, eased
    // frame-to-frame in timerCallback for a smooth, continuously-breathing shape.
    const float userScale = *processorRef.apvts.getRawParameterValue ("VS_SCALE");
    const auto centre = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.40f * userScale;
    const bool showAxes = *processorRef.apvts.getRawParameterValue ("VS_SHOW_AXES") > 0.5f;

    static const juce::String labels[5] = { "SPECTRAL SKEWNESS", "SPECTRAL ENTROPY", "SPECTRAL CREST",
                                             "SPECTRAL SLOPE", "SPECTRAL FLATNESS" };

    auto angleFor = [] (int k)
    {
        return -juce::MathConstants<float>::halfPi + (float) k * (juce::MathConstants<float>::twoPi / 5.0f);
    };

    if (showAxes)
    {
        for (int ring = 1; ring <= 4; ++ring)
        {
            const float rr = radius * (float) ring / 4.0f;
            juce::Path ringPath;
            for (int k = 0; k <= 5; ++k)
            {
                const float a = angleFor (k % 5);
                const juce::Point<float> pt (centre.x + std::cos (a) * rr, centre.y + std::sin (a) * rr);
                if (k == 0) ringPath.startNewSubPath (pt); else ringPath.lineTo (pt);
            }
            g.setColour (ink (0.14f));
            g.strokePath (ringPath, juce::PathStrokeType (1.0f));
        }

        for (int k = 0; k < 5; ++k)
        {
            const float a = angleFor (k);
            const juce::Point<float> axisEnd (centre.x + std::cos (a) * radius, centre.y + std::sin (a) * radius);

            g.setColour (ink (0.22f));
            g.drawLine ({ centre, axisEnd }, 1.0f);

            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.setColour (ink (0.4f));
            for (int t = 1; t <= 4; ++t)
            {
                const float tn = (float) t / 4.0f;
                const juce::Point<float> tp (centre.x + std::cos (a) * radius * tn, centre.y + std::sin (a) * radius * tn);
                g.drawText (juce::String (tn, 2), tp.x - 15.0f, tp.y - 11.0f, 30.0f, 12.0f, juce::Justification::centred);
            }

            g.setFont (juce::Font (juce::FontOptions (10.5f)).boldened());
            g.setColour (ink (0.75f));
            const juce::Point<float> labelPos (axisEnd.x + std::cos (a) * 24.0f, axisEnd.y + std::sin (a) * 14.0f);
            g.drawText (labels[(size_t) k], labelPos.x - 62.0f, labelPos.y - 8.0f, 124.0f, 16.0f,
                        juce::Justification::centred);
        }
    }

    const juce::Colour radarColour = juce::Colour::fromRGB (216, 60, 205);

    juce::Path shape;
    for (int k = 0; k <= 5; ++k)
    {
        const int kk = k % 5;
        const float a = angleFor (kk);
        const float v = juce::jlimit (0.0f, 1.0f, radarSmoothed[(size_t) kk]);
        const juce::Point<float> pt (centre.x + std::cos (a) * radius * v, centre.y + std::sin (a) * radius * v);
        if (k == 0) shape.startNewSubPath (pt); else shape.lineTo (pt);
    }

    g.setColour (radarColour.withAlpha (0.32f));
    g.fillPath (shape);
    g.setColour (radarColour.withAlpha (0.9f));
    g.strokePath (shape, juce::PathStrokeType (1.6f));

    for (int k = 0; k < 5; ++k)
    {
        const float a = angleFor (k);
        const float v = juce::jlimit (0.0f, 1.0f, radarSmoothed[(size_t) k]);
        const juce::Point<float> pt (centre.x + std::cos (a) * radius * v, centre.y + std::sin (a) * radius * v);
        glowDot (g, pt, 2.2f, radarColour);

        if (*processorRef.apvts.getRawParameterValue ("VS_LABEL_POINTS") > 0.5f)
        {
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.setColour (ink (0.7f));
            g.drawText (juce::String (v, 2), pt.x - 15.0f, pt.y - 14.0f, 30.0f, 12.0f, juce::Justification::centred);
        }
    }
}
//==============================================================================
// Custom 3D Grid: lets the user assign any already-computed field to each of
// the X / Y / Z axes (VS_GRID_X/Y/Z), rather than a fixed pairing of fields.
float EcoSeeAudioProcessorEditor::getFieldValue (int fieldIndex, const SpectralFrame& f, int frameIndex,
                                                  float freqMin, float freqMax, float spreadMax) const
{
    juce::ignoreUnused (freqMin, freqMax, spreadMax);

    switch (fieldIndex)
    {
        case 0: return f.centroidHz;
        case 1: return f.spreadHz;
        case 2: return f.entropy01;
        case 3: return f.flatness01;
        case 4: return f.amplitude01;
        case 5: return f.skewness01;
        case 6: return f.crest01;
        case 7: return f.slope01;
        case 8:
        default: return (float) frameIndex; // Time
    }
}

void EcoSeeAudioProcessorEditor::getFieldRange (int fieldIndex, float freqMin, float freqMax, float spreadMax,
                                                 int totalFrames, float& outMin, float& outMax, juce::String& outLabel) const
{
    switch (fieldIndex)
    {
        case 0: outMin = freqMin; outMax = freqMax;  outLabel = "Spectral Centroid (Hz)"; break;
        case 1: outMin = 0.0f;    outMax = spreadMax; outLabel = "Spectral Spread (Hz)";   break;
        case 2: outMin = 0.0f;    outMax = 1.0f;      outLabel = "Spectral Entropy";       break;
        case 3: outMin = 0.0f;    outMax = 1.0f;      outLabel = "Spectral Flatness";      break;
        case 4: outMin = 0.0f;    outMax = 1.0f;      outLabel = "Amplitude";              break;
        case 5: outMin = 0.0f;    outMax = 1.0f;      outLabel = "Spectral Skewness";      break;
        case 6: outMin = 0.0f;    outMax = 1.0f;      outLabel = "Spectral Crest";         break;
        case 7: outMin = 0.0f;    outMax = 1.0f;      outLabel = "Spectral Slope";         break;
        case 8:
        default: outMin = 0.0f; outMax = (float) juce::jmax (1, totalFrames - 1); outLabel = "Time"; break;
    }
}

void EcoSeeAudioProcessorEditor::drawCustomGrid3D (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto origin = area.getCentre();
    const float userScale = *processorRef.apvts.getRawParameterValue ("VS_SCALE");
    const float scale = juce::jmin (area.getWidth(), area.getHeight()) * 0.85f * userScale * singlePanelScaleBoost;
    const int n = EcoSeeAudioProcessor::kHistorySize;

    const float freqMin = *processorRef.apvts.getRawParameterValue ("RG_FREQ_MIN");
    const float freqMax = juce::jmax (freqMin + 1.0f, (float) *processorRef.apvts.getRawParameterValue ("RG_FREQ_MAX"));
    const float spreadMax = *processorRef.apvts.getRawParameterValue ("RG_SPREAD_MAX");
    const bool showAxes = *processorRef.apvts.getRawParameterValue ("VS_SHOW_AXES") > 0.5f;

    const int xField = (int) *processorRef.apvts.getRawParameterValue ("VS_GRID_X");
    const int yField = (int) *processorRef.apvts.getRawParameterValue ("VS_GRID_Y");
    const int zField = (int) *processorRef.apvts.getRawParameterValue ("VS_GRID_Z");

    float xMin, xMax, yMin, yMax, zMin, zMax;
    juce::String xLabel, yLabel, zLabel;
    getFieldRange (xField, freqMin, freqMax, spreadMax, n, xMin, xMax, xLabel);
    getFieldRange (yField, freqMin, freqMax, spreadMax, n, yMin, yMax, yLabel);
    getFieldRange (zField, freqMin, freqMax, spreadMax, n, zMin, zMax, zLabel);

    // Overrides the nominal ranges above with each axis's actual observed
    // min/max when Normalize is on -- this is the case the toggle matters
    // most for, since here the user can freely assign any field (including
    // ones that spend most of their time pinned near one end of their
    // nominal range) to any axis.
    adaptiveRange ([this, xField, freqMin, freqMax, spreadMax] (const SpectralFrame& f, int i)
                   { return getFieldValue (xField, f, i, freqMin, freqMax, spreadMax); }, xMin, xMax, xMin, xMax);
    adaptiveRange ([this, yField, freqMin, freqMax, spreadMax] (const SpectralFrame& f, int i)
                   { return getFieldValue (yField, f, i, freqMin, freqMax, spreadMax); }, yMin, yMax, yMin, yMax);
    adaptiveRange ([this, zField, freqMin, freqMax, spreadMax] (const SpectralFrame& f, int i)
                   { return getFieldValue (zField, f, i, freqMin, freqMax, spreadMax); }, zMin, zMax, zMin, zMax);

    // Reuses panel A's rotation state (rotationA/userRotYA/userRotXA), since
    // this view replaces panel A whenever it's shown.
    const float rotY = rotationA + userRotYA;
    const float rotX = 0.5f + userRotXA;

    if (showAxes)
        drawAxisGuides3D (g, origin, scale, rotY, rotX, xLabel, yLabel, zLabel,
                           xMin, xMax, yMin, yMax, zMin, zMax, true);

    auto normalise01 = [] (float v, float mn, float mx)
    {
        return (mx > mn) ? juce::jlimit (0.0f, 1.0f, (v - mn) / (mx - mn)) : 0.5f;
    };

    const int startIdx = visibleHistoryStart();
    const bool snakeMode = *processorRef.apvts.getRawParameterValue ("VS_SNAKE_MODE") > 0.5f;

    std::vector<juce::Point<float>> runPts;
    std::vector<juce::Colour> runColours;
    std::vector<float> runRadii;

    for (int i = 0; i < n; ++i)
    {
        if (i < startIdx)
            continue; // hidden by the Visible Points slider

        const auto& f = history[(size_t) i];

        if (! isAudible (f))
        {
            flushTrailRun (g, runPts, runColours, runRadii, snakeMode, true, 1.2f);
            continue;
        }

        const float xVal = getFieldValue (xField, f, i, freqMin, freqMax, spreadMax);
        const float yVal = getFieldValue (yField, f, i, freqMin, freqMax, spreadMax);
        const float zVal = getFieldValue (zField, f, i, freqMin, freqMax, spreadMax);

        const float xN = normalise01 (xVal, xMin, xMax);
        const float yN = normalise01 (yVal, yMin, yMax);
        const float zN = normalise01 (zVal, zMin, zMax);

        Vec3f p3 { (xN - 0.5f) * 2.0f, (yN - 0.5f) * 2.0f, (zN - 0.5f) * 2.0f };

        const auto sp = project (p3, rotY, rotX, origin, scale);
        auto colour = frequencyToColour (f.amplitude01);
        const bool isNewest = (i == n - 1);
        if (isNewest && newPointFlash > 0.001f)
            colour = colour.interpolatedWith (juce::Colours::white, newPointFlash);

        runPts.push_back (sp);
        runColours.push_back (colour);
        runRadii.push_back (1.9f + f.amplitude01 * 3.0f);

        maybeLabelPoint (g, sp, Vec3f { xVal, yVal, zVal }, i);
    }

    flushTrailRun (g, runPts, runColours, runRadii, snakeMode, true, 1.2f);
}
