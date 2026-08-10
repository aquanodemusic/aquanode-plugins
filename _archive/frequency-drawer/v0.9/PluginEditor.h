/*
  ==============================================================================
    FrequencyDrawer — PluginEditor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
 * The 2-D canvas where users paint frequency events.
 *
 *  X axis  →  time        (linear,      0–30 s)
 *  Y axis  ↑  frequency   (logarithmic, 20 Hz – 20 kHz)
 *
 * Harmonics are handled entirely here: addDrawPoint stamps N events
 * (fundamental + overtones) directly into the event list and the cached image.
 * The audio engine sees only flat DrawnEvents and plays one sine each.
 *
 * Events are stamped onto a cached juce::Image for fast repaints.
 * The playhead, axes, and "Rendering…" overlay are drawn live on top.
 */
class DrawingCanvas : public juce::Component
{
public:
    explicit DrawingCanvas(FrequencyDrawerAudioProcessor& p);

    void paint(juce::Graphics& g)               override;
    void resized()                               override;
    void mouseDown(const juce::MouseEvent& e)    override;
    void mouseDrag(const juce::MouseEvent& e)    override;
    void mouseUp  (const juce::MouseEvent& e)    override;

    void setDrawMode    (bool b) { drawMode_     = b; }
    void setPlayheadMode(bool b) { playheadMode_ = b; }
    void setBlurViz(bool on, float secs) { showBlur_ = on; blurSecs_ = secs; cacheDirty_ = true; repaint(); }
    void invalidateCache() { cacheDirty_ = true; repaint(); }

    /** Called once per completed stroke so the editor can trigger a re-render. */
    std::function<void()>                        onStrokeFinished;

    /** Called for each individual event added (fundamental + every harmonic). */
    std::function<void(double t, double f, double amp)> onEventAdded;

    /** Called when the user repositions the playhead. */
    std::function<void(double t)>                onPlayheadMoved;

private:
    FrequencyDrawerAudioProcessor& proc_;

    bool  drawMode_     = true;
    bool  playheadMode_ = false;
    bool  showBlur_     = false;
    float blurSecs_     = 1.0f;
    bool  cacheDirty_   = true;

    juce::Image drawCache_;

    // Pixel margins for the axes
    static constexpr int kML = 54;   // left  (frequency labels)
    static constexpr int kMB = 24;   // bottom (time labels)
    static constexpr int kMT = 6;    // top

    // Track previous drag position for stroke interpolation
    bool             hasPrevDrag_ = false;
    juce::Point<int> prevDrag_;

    // Coordinate helpers
    juce::Point<float>  tfToXY(double t, double f) const noexcept;
    juce::Point<double> xyToTF(int x,   int y)     const noexcept;

    void rebuildCache();
    void stampEvent(double t, double f);
    void drawGrid(juce::Graphics& g);

    /** Adds the fundamental + all harmonics at pixel (x, y). */
    void addDrawPoint(int x, int y);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawingCanvas)
};

//==============================================================================
class FrequencyDrawerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit FrequencyDrawerAudioProcessorEditor(FrequencyDrawerAudioProcessor&);
    ~FrequencyDrawerAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized()                override;

private:
    void timerCallback() override;

    FrequencyDrawerAudioProcessor& audioProcessor_;

    //==========================================================================
    // Top-bar controls
    juce::TextButton btnPlayheadMode{ "Playhead" };
    juce::TextButton btnDraw        { "Draw" };
    juce::TextButton btnPlay        { "\xe2\x96\xb6  Play"  };   // ▶
    juce::TextButton btnPause       { "\xe2\x8f\xb8  Pause" };   // ⏸
    juce::TextButton btnClear       { "Clear" };
    juce::TextButton btnBlur        { "Blur" };
    juce::TextButton btnExport      { "Export FLAC" };

    juce::Label  lblHarmonics   { "", "Harmonics" };
    juce::Slider sldHarmonics;
    juce::Label  lblBlurStrength{ "", "Blur (s)" };
    juce::Slider sldBlurStrength;

    DrawingCanvas canvas_;

    bool isDrawMode_     = true;
    bool isPlayheadMode_ = false;
    bool isBlurMode_     = false;

    double prevPlayheadPos_ = -1.0;
    bool   prevRendering_   = false;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyDrawerAudioProcessorEditor)
};