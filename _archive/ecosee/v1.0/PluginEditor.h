#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <functional>

struct Vec3f
{
    float x = 0, y = 0, z = 0;
};

//==============================================================================
// A scrollable column of grouped controls (Analysis / Range / Motion / Visual).
// Each row is either a Slider (for float params) or a ComboBox (for choice params),
// each bound to the processor's APVTS via an attachment.
class ControlSidebar : public juce::Component
{
public:
    explicit ControlSidebar (juce::AudioProcessorValueTreeState& state);

    void resized() override;
    void paint (juce::Graphics&) override;

    int getPreferredHeight() const { return totalHeight; }

    // Shows/hides every row in the named section (used to reveal the X/Y/Z
    // field pickers only while the Custom 3D Grid view is active) and
    // relayouts so hidden sections take up no space.
    void setSectionVisible (const juce::String& title, bool visible);

    // Re-applies dark/light colours to every row if VS_WHITE_BG has changed
    // since the last call; cheap no-op otherwise, so the editor can just
    // call this every timer tick rather than needing its own change-detection.
    void updateTheme();

    // The click-drag mode (rotate vs. move) toggle lives visually in the
    // sidebar (VIEW section) but its state/behaviour is owned by the editor,
    // so the editor wires up onClick / updates the label through this.
    juce::TextButton& getDragModeButton() { return *dragModeButton; }

private:
    struct Row
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::TextButton> button;
        std::unique_ptr<juce::Component> colourSwatch;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    struct Section
    {
        juce::String title;
        std::vector<std::unique_ptr<Row>> rows;
        bool visible = true;
    };

    void addSlider (Section& section, juce::AudioProcessorValueTreeState& state,
                     const juce::String& paramId, const juce::String& labelText);
    void addCombo  (Section& section, juce::AudioProcessorValueTreeState& state,
                     const juce::String& paramId, const juce::String& labelText);
    void addToggle (Section& section, juce::AudioProcessorValueTreeState& state,
                     const juce::String& paramId, const juce::String& labelText);
    void addColourSwatch (Section& section, juce::AudioProcessorValueTreeState& state,
                           const juce::String& paramId, const juce::String& labelText);
    // Plain action button, not bound to any APVTS parameter (used for the
    // click-drag mode toggle, whose state lives on the editor). Returns the
    // button so the caller (the editor) can wire up onClick / set its text.
    juce::TextButton& addActionButton (Section& section, const juce::String& labelText);

    // Single source of truth for a row's height, used by both paint() (to
    // position section titles) and resized() (to position controls) so the
    // two never drift apart. Toggle/button rows are shorter (no separate
    // label above them) than label+control rows.
    int rowHeightFor (const Row& row) const;

    std::vector<Section> sections;
    int totalHeight = 0;
    juce::TextButton* dragModeButton = nullptr; // owned by a Row in the VIEW section
    juce::AudioProcessorValueTreeState& apvtsForTheme;
    bool themeIsWhite = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlSidebar)
};

//==============================================================================
class EcoSeeAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit EcoSeeAudioProcessorEditor (EcoSeeAudioProcessor&);
    ~EcoSeeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void drawSpreadEntropySpace (juce::Graphics& g, juce::Rectangle<float> area);
    void drawToneMap            (juce::Graphics& g, juce::Rectangle<float> area);
    void drawFmAmCube           (juce::Graphics& g, juce::Rectangle<float> area);
    void drawCepstralPeriodogram (juce::Graphics& g, juce::Rectangle<float> area);
    void drawVocalSignatureRadar (juce::Graphics& g, juce::Rectangle<float> area);
    void drawCustomGrid3D       (juce::Graphics& g, juce::Rectangle<float> area);
    void drawPanelFrame (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title);

    // The set of already-computed per-frame fields the user can assign to
    // any of the Custom 3D Grid's X / Y / Z axes (VS_GRID_X/Y/Z choices).
    // Keep in sync with the field list built in PluginProcessor::createParams().
    float getFieldValue (int fieldIndex, const SpectralFrame& f, int frameIndex, float freqMin, float freqMax, float spreadMax) const;
    void  getFieldRange (int fieldIndex, float freqMin, float freqMax, float spreadMax, int totalFrames,
                          float& outMin, float& outMax, juce::String& outLabel) const;

    // Theme helpers: everything that isn't a data-colour (chrome, axis text,
    // fine connector lines) goes through these so the white-background
    // option can flip them without touching every draw call individually.
    juce::Colour paperColour() const;
    juce::Colour ink (float alpha) const;

    // Optional per-point [x, y, z] coordinate label, shown next to a subset
    // of points when "Label Data Points" is enabled. `coords` carries the
    // real (physical/normalised) values to display, `index` is only used to
    // decide which points get labelled (every Nth) when everyN is true.
    void maybeLabelPoint (juce::Graphics& g, juce::Point<float> pos, Vec3f coords, int index, bool everyN = true);

    juce::Point<float> project (Vec3f p, float rotY, float rotX, juce::Point<float> origin, float scale) const;
    void glowDot   (juce::Graphics& g, juce::Point<float> pos, float radius, juce::Colour colour);
    void glowLine  (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour, float thickness);
    void fineWhiteLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b);
    void drawAxisGuides3D (juce::Graphics& g, juce::Point<float> origin, float scale, float rotY, float rotX,
                            const juce::String& xLabel, const juce::String& yLabel, const juce::String& zLabel,
                            float xMin = 0.0f, float xMax = 1.0f, float yMin = 0.0f, float yMax = 1.0f,
                            float zMin = 0.0f, float zMax = 1.0f, bool zNumeric = false);
    juce::Colour   frequencyToColour (float normalised01) const;
    juce::Colour   flatnessToColour  (float normalised01) const;
    juce::Colour   cepstrumColour    (float normalised01) const;
    // Shared low/mid/high gradient (user-configurable via the COLOUR SCHEME
    // sidebar section) that backs all three colour mappings above, so every
    // diagram's data-colour follows the same picked palette.
    juce::Colour   schemeColour      (float normalised01) const;

    // True unless "Hide Dots If Quiet" is on and this frame's level is below
    // the (generous, -90dB) silence-gating threshold -- used by the scatter/
    // trail panels to only draw dots for frames with actual signal in them.
    bool isAudible (const SpectralFrame& f) const;

    // Backs the "Normalize Axes To Data" toggle (VS_NORMALIZE). When that's
    // off, just returns nominalMin/nominalMax unchanged. When it's on, scans
    // the current history buffer (skipping inaudible frames the same way the
    // panels themselves do) and returns the field's actual observed min/max
    // instead, so a field that happens to sit nearly flat against its
    // nominal range still fills the axis rather than collapsing onto a thin
    // sliver. Falls back to the nominal range if there's no audible data yet
    // or the field is genuinely constant (avoids a zero-width range).
    void adaptiveRange (const std::function<float (const SpectralFrame&, int)>& getter,
                         float nominalMin, float nominalMax,
                         float& outMin, float& outMax) const;

    EcoSeeAudioProcessor& processorRef;

    std::array<SpectralFrame, EcoSeeAudioProcessor::kHistorySize> history;

    // The periodogram displays this, not the raw latest-frame cepstrum directly:
    // each tick it's eased toward the newest analysed frame's cepstrum, so the
    // curve animates continuously as new data arrives rather than snapping
    // frame-to-frame or needing a separate time axis. Sized/filled dynamically
    // to match the current frame's cepstrumCount (which tracks fftSize/2), up
    // to the fixed kMaxCepstrumSize backing storage.
    std::array<float, kMaxCepstrumSize> displayCepstrum {};
    int displayCepstrumCount = 0;

    juce::Image trailImage;

    // Newest analysed frame flashes white for a moment before easing into
    // its data colour, so freshly-arriving points read as "arriving" rather
    // than just popping into place.
    SpectralFrame newestSeenFrame;
    float newPointFlash = 0.0f;

    // Eased values for the Vocal Signature radar (skew, entropy, crest, slope, flatness).
    std::array<float, 5> radarSmoothed { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

    float rotationA = 0.0f;
    float rotationB = 0.0f;
    int   lastFps = -1;

    // Extra multiplier applied to the two 3D panels' projection scale when
    // a single panel is shown full-screen via VS_VIEW_MODE, so the diagram
    // reads like it's being viewed through a macro lens rather than just
    // "the same picture, bigger".
    float singlePanelScaleBoost = 1.0f;

    // Tracks whether the sidebar's X/Y/Z field-picker section is currently
    // shown, so we only push a visibility change (and relayout) when the
    // view mode actually crosses in/out of Custom 3D Grid.
    bool customGridControlsVisible = false;

    // --- Interactive rotate (drag) & zoom (scroll) per panel --------------
    // Panel indices: 0 = Spread-Entropy Space, 1 = Tone Map, 2 = FM-AM Cube, 3 = Cepstrogram.
    juce::Rectangle<float> panelBoundsA, panelBoundsB, panelBoundsC, panelBoundsD;
    float userRotYA = 0.0f, userRotXA = 0.0f; // extra drag-driven rotation, panel A (added to rotationA / base tilt)
    float userRotYC = 0.0f, userRotXC = 0.0f; // extra drag-driven rotation, panel C
    // Panel B (top-right, Tone Map) starts slightly zoomed out by default --
    // at 1.0 its axis labels get clipped against the panel edge.
    float zoomA = 1.0f, zoomB = 0.8f, zoomC = 1.0f, zoomD = 1.0f;
    juce::Point<float> panA, panB, panC, panD; // drag-driven pan offset per panel, applied inside the clip
    int   dragPanelIndex = -1; // -1 = none, 0 = A, 1 = B, 2 = C, 3 = D
    juce::Point<float> lastDragPos;

    // While true, that panel's auto-rotation is paused so the drag can grab
    // the rotation directly instead of adding a phase offset on top of a
    // still-spinning base angle.
    bool draggingRotateA = false, draggingRotateC = false;

    // Click-and-drag can either rotate the 3D panels or pan (move) any
    // panel's content; a button in the sidebar's VIEW section cycles this
    // (see ControlSidebar::getDragModeButton()).
    enum class DragMode { Rotate, Move };
    DragMode dragMode = DragMode::Rotate;
    void cycleDragMode();

    // --- Sidebar (always visible, fixed width) ---------------------------
    juce::Viewport sidebarViewport;
    ControlSidebar sidebar;
    static constexpr float kSidebarWidth = 270.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EcoSeeAudioProcessorEditor)
};