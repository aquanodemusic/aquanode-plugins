/*
  ==============================================================================
    FrequencyDrawer -- PluginEditor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
 * The 2-D canvas where users paint frequency events.
 *
 *  X axis  ->  time        (linear,      0-30 s)
 *  Y axis up   frequency   (logarithmic, 20 Hz - 20 kHz)
 *
 * VISUAL CACHE MODEL
 *   drawCache_  is the live working image.  During a stroke, dots are painted
 *   directly onto it (fast; one Graphics context for all harmonics).
 *
 *   On mouseUp (stroke commit), commitCurrentImage() bakes the full drawCache_
 *   into committedImage_ (stored as a JUCE ARGB image in RAM; can be PNG-
 *   compressed if memory becomes a concern) and clears stampHistory_ +
 *   interpPaths_.  The raw data-point lists are no longer needed because:
 *     - The audio is baked into the processor's committedBuffer_.
 *     - The visual is baked into committedImage_.
 *
 *   rebuildCache() (called on resize / clear) reconstructs drawCache_ by
 *   scaling committedImage_ to the new size, then replaying any still-pending
 *   history on top (will be empty right after a commit).
 *
 * DRAW MODE
 *   Stamps individual frequency events (fundamental + harmonics) as coloured
 *   dots.  Audio events are buffered locally during the drag and committed to
 *   the processor in one batch on mouseUp.
 *
 * INTERPOLATE MODE
 *   Collects waypoints; on mouseUp forwards a single DrawnPath to the
 *   processor for phase-continuous synthesis.
 */
class DrawingCanvas : public juce::Component
{
public:
    //==========================================================================
    struct StampRecord
    {
        double t, f;
        bool   blurEnabled;
        float  blurSecs;
    };

    struct InterpPathRecord
    {
        std::vector<std::pair<double, double>> waypoints;
        int   numHarmonics = 1;
        bool  blurEnabled = false;
        float blurSecs = 0.0f;
    };

    //==========================================================================
    explicit DrawingCanvas(FrequencyDrawerAudioProcessor& p);

    void paint(juce::Graphics& g)            override;
    void resized()                            override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    //==========================================================================
    void setDrawMode(bool b) { drawMode_ = b; }
    void setPlayheadMode(bool b) { playheadMode_ = b; }
    void setInterpMode(bool b) { interpMode_ = b; }
    void setScaleMode(int  m) { scaleMode_ = m; }

    /** Update blur state for future strokes.  blurOn = (blurSecs > 0). */
    void setBlurViz(bool blurOn, float blurSecs)
    {
        showBlur_ = blurOn;
        blurSecs_ = blurSecs;
    }

    void invalidateCache() { cacheDirty_ = true; repaint(); }

    /** Clear all visual history (including committed snapshot) and force a
        blank repaint. */
    void clearHistory();

    //==========================================================================
    // Callbacks set by the editor

    std::function<void(std::vector<DrawnEvent>)> onEventsCommitted;
    std::function<void(DrawnPath)>               onPathAdded;
    std::function<void()>                        onStrokeFinished;
    std::function<void(double t)>                onPlayheadMoved;

private:
    FrequencyDrawerAudioProcessor& proc_;

    bool  drawMode_ = true;
    bool  playheadMode_ = false;
    bool  interpMode_ = false;
    bool  showBlur_ = false;
    float blurSecs_ = 0.0f;   // 0 = no blur
    int   scaleMode_ = 0;       // 0=none 1=C Major 2=C Minor 3=C Pent Major
    bool  cacheDirty_ = true;

    juce::Image drawCache_;

    // Committed visual state -- baked on each stroke commit and stored as an
    // ARGB image in RAM.  rebuildCache() scales this to the current size so
    // raw data-point history is not needed after a commit.
    // (Could be PNG-compressed with juce::PNGImageFormat if memory matters.)
    juce::Image committedImage_;

    // Pixel margins for the axes
    static constexpr int kML = 54;   // left  (frequency labels)
    static constexpr int kMB = 24;   // bottom (time labels)
    static constexpr int kMT = 6;    // top

    bool             hasPrevDrag_ = false;
    juce::Point<int> prevDrag_;

    // Visual history -- only contains UNCOMMITTED strokes after a commit
    std::vector<StampRecord>      stampHistory_;
    std::vector<InterpPathRecord> interpPaths_;

    std::vector<std::pair<double, double>> currentInterpStroke_;
    std::vector<DrawnEvent>                pendingAudioEvents_;

    double lastAudioEventTime_ = -1.0;

    //==========================================================================
    juce::Point<float>  tfToXY(double t, double f) const noexcept;
    juce::Point<double> xyToTF(int x, int y)       const noexcept;

    void rebuildCache();

    /** Snapshot drawCache_ → committedImage_ and clear stampHistory_/
        interpPaths_.  Call this after a stroke has been committed to the
        processor so the raw data lists can be discarded. */
    void commitCurrentImage();

    void stampEventGfx(juce::Graphics& cg, double t, double f,
        bool blurEnabled, float blurSecs) const noexcept;
    void stampEvent(double t, double f, bool blurEnabled, float blurSecs);

    void drawGrid(juce::Graphics& g);

    void drawInterpPolyline(juce::Graphics& g,
        const std::vector<std::pair<double, double>>& wps,
        float strokeWidth) const;

    void   addDrawPoint(int x, int y);
    double snapFrequency(double freq) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawingCanvas)
};

//==============================================================================
class FrequencyDrawerAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit FrequencyDrawerAudioProcessorEditor(FrequencyDrawerAudioProcessor&);
    ~FrequencyDrawerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized()               override;

private:
    void timerCallback() override;

    FrequencyDrawerAudioProcessor& audioProcessor_;

    //==========================================================================
    // All controls live in a single toolbar row
    juce::TextButton btnPlayheadMode{ "Playhead" };
    juce::TextButton btnDraw{ "Draw" };
    juce::TextButton btnInterp{ "Interp" };
    juce::TextButton btnPlay{ "> Play" };
    juce::TextButton btnPause{ "|| Pause" };
    juce::TextButton btnClear{ "Clear" };
    juce::TextButton btnExport{ "Export FLAC" };

    juce::Label    lblHarmonics{ "", "Harm" };
    juce::Slider   sldHarmonics;
    juce::Label    lblBlurStrength{ "", "Blur (s)" };
    juce::Slider   sldBlurStrength;
    juce::Label    lblScale{ "", "Scale" };
    juce::ComboBox cmbScale;

    DrawingCanvas canvas_;

    bool isDrawMode_ = true;
    bool isPlayheadMode_ = false;
    bool isInterpMode_ = false;

    double prevPlayheadPos_ = -1.0;
    bool   prevRendering_ = false;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyDrawerAudioProcessorEditor)
};