#pragma once

#include "PluginProcessor.h"
#include <set>
#include <vector>

//==============================================================================
// Shared "analog display" background: a dark glass panel with a subtle
// gradient glow (brighter near the top-centre, fading to near-black at the
// edges) plus a faint vignette, used by all three black display windows
// (peaks visualizer, volume bar editor, frequency bar editor) so they read
// as backlit analog meter glass rather than flat matte rectangles.
//==============================================================================
static inline void paintAnalogDisplayBackground(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(juce::Colour(0xff0a1a1c));
    g.fillRect(bounds);

    juce::ColourGradient glow(
        juce::Colour(0xff153f3c).withAlpha(0.9f), bounds.getCentreX(), bounds.getY(),
        juce::Colour(0xff050f0e), bounds.getCentreX(), bounds.getBottom(),
        false);
    glow.addColour(0.45, juce::Colour(0xff0d2a28));
    g.setGradientFill(glow);
    g.fillRect(bounds);

    juce::ColourGradient vignette(
        juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
        juce::Colours::black.withAlpha(0.5f), bounds.getX(), bounds.getY(),
        true);
    g.setGradientFill(vignette);
    g.fillRect(bounds);
}

//==============================================================================
// Draws one vertical line per bell: x = log-mapped frequency, height = gain.
// No FFT, no spectral history — just reads the processor's BASE VOICE
// per-bell frequency/gain snapshot and redraws it a few times a second.
// (With polyphony, other voices are sounding too, but showing "the note
// you're currently holding" keeps this simple and readable.)
//==============================================================================
class BellVisualizer : public juce::Component,
    private juce::Timer
{
public:
    explicit BellVisualizer(FullFilterAudioProcessor& p) : processor(p)
    {
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        paintAnalogDisplayBackground(g, bounds);

        const float midY = bounds.getBottom(); // 0 dB / silence baseline sits at the bottom

        constexpr float minFreq = 20.0f;
        constexpr float maxFreq = 20000.0f;
        constexpr float minDb = -36.0f;  // quietest a bell can be pushed by the tilt
        constexpr float maxDb = 48.0f;   // loudest via the level knob

        auto xForFreq = [&](float freq)
            {
                const float logMin = std::log10(minFreq);
                const float logMax = std::log10(maxFreq);
                const float logF = std::log10(juce::jlimit(minFreq, maxFreq, freq));
                return bounds.getX() + bounds.getWidth() * (logF - logMin) / (logMax - logMin);
            };

        // Oscilloscope-style grid: vertical decade lines at 100 Hz / 1 kHz /
        // 10 kHz (labelled), plus a few evenly-spaced horizontal divisions.
        {
            g.setColour(juce::Colour(0xff2fffb0).withAlpha(0.16f));
            for (int i = 1; i < 4; ++i)
            {
                const float y = bounds.getY() + bounds.getHeight() * (float)i / 4.0f;
                g.drawHorizontalLine((int)y, bounds.getX(), bounds.getRight());
            }

            g.setFont(juce::Font(juce::FontOptions(10.0f)));

            const float decadeFreqs[] = { 100.0f, 1000.0f, 10000.0f };
            const char* decadeLabels[] = { "100", "1k", "10k" };

            for (int i = 0; i < 3; ++i)
            {
                const float x = xForFreq(decadeFreqs[i]);

                g.setColour(juce::Colour(0xff2fffb0).withAlpha(0.28f));
                g.drawVerticalLine((int)x, bounds.getY(), bounds.getBottom());

                g.setColour(juce::Colour(0xff2fffb0).withAlpha(0.55f));
                g.drawText(decadeLabels[i],
                    juce::Rectangle<float>(x + 3.0f, bounds.getY() + 1.0f, 30.0f, 11.0f),
                    juce::Justification::centredLeft, false);
            }
        }

        g.setColour(juce::Colour(0xff00e5ff).withAlpha(0.85f)); // cyan bell lines

        const int count = processor.activeBellCount.load();

        for (int n = 0; n < count; ++n)
        {
            const float freq = processor.bellFrequencies[(size_t)n].load();
            const float db = processor.bellGainsDb[(size_t)n].load();

            if (db <= minDb - 1.0f) // marked inactive/muted
                continue;

            const float x = xForFreq(freq);
            const float normalised = juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            const float lineHeight = normalised * bounds.getHeight();

            g.drawLine(x, midY, x, midY - lineHeight, 1.5f);
        }
    }

private:
    void timerCallback() override { repaint(); }

    FullFilterAudioProcessor& processor;
};

//==============================================================================
// A row of glowing green bars, one per currently active bell (only as many
// bars glow/appear as processor.activeBellCount, per the Amount knob).
// Click-and-drag a bar to edit that bell:
//   - Mode::Volume:    bar height 0 (bottom) .. 2x (top) volume multiplier,
//                       1x (unedited) rests at the middle.
//   - Mode::Frequency: bar height 0.5x (bottom) .. 2x (top) frequency
//                       multiplier relative to where the harmonic naturally
//                       sits, 1x (unedited) rests at the middle.
// Bells that are muted (manually, via the square-wave button, or because
// their effective frequency fell outside 10 Hz-20 kHz) are drawn as dim
// grey instead of glowing green. Double-click a bar to reset it to 1x.
//==============================================================================
class BellBarEditor : public juce::Component,
    private juce::Timer
{
public:
    enum class Mode { Volume, Frequency };

    BellBarEditor(FullFilterAudioProcessor& p, Mode m) : processor(p), mode(m)
    {
        startTimerHz(30); // keeps the mute/out-of-range shading live
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        paintAnalogDisplayBackground(g, bounds);

        const int count = juce::jlimit(1, FullFilterAudioProcessor::maxBells, processor.activeBellCount.load());
        const float barSlotWidth = bounds.getWidth() / (float)count;

        // Faint centre guide line — the default/unedited (1x) resting position.
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        const float midY = bounds.getCentreY();
        g.drawHorizontalLine((int)midY, bounds.getX(), bounds.getRight());

        for (int n = 0; n < count; ++n)
        {
            const float x = bounds.getX() + barSlotWidth * (float)n;
            const float barWidth = juce::jmax(1.0f, barSlotWidth - 2.0f);

            const bool muted = processor.isBellManuallyMuted(n) || isOutOfAudibleRange(n);
            const bool selected = selectModeActive && mode == Mode::Frequency && selectedIndices.count(n) > 0;
            const float norm = normalisedValueForBell(n); // 0 (bottom) .. 1 (top)

            const float barTop = bounds.getY() + (1.0f - norm) * bounds.getHeight();
            const float barBottom = bounds.getBottom();

            const juce::Colour colour = selected ? juce::Colour(0xff4aa3ff)  // selected: blue
                : muted ? juce::Colours::grey.withAlpha(0.35f)
                : juce::Colour(0xff39ff6a); // glowing green

            if (!muted)
            {
                // Soft glow: a few widened, faded copies behind the core bar.
                for (int spread = 3; spread >= 1; --spread)
                {
                    g.setColour(colour.withAlpha(0.10f / (float)spread));
                    g.fillRect(x + 1.0f - (float)spread, barTop - (float)spread,
                        barWidth + 2.0f * (float)spread, (barBottom - barTop) + 2.0f * (float)spread);
                }
            }

            g.setColour(colour);
            g.fillRect(x + 1.0f, barTop, barWidth, barBottom - barTop);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (selectModeActive && mode == Mode::Frequency)
        {
            const int index = indexForX((float)e.position.x);
            if (index < 0)
                return;

            // Whichever way this bar's selection flips (on -> off or
            // off -> on) becomes the action for the rest of this drag
            // gesture, so a click-and-drag paints a whole range of bars
            // into/out of the selection instead of needing one click per bar.
            dragIsAdding = selectedIndices.count(index) == 0;
            lastDraggedIndex = -1;
            applyDragSelection(index);
            return;
        }

        updateFromMouse(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (selectModeActive && mode == Mode::Frequency)
        {
            const int index = indexForX((float)e.position.x);
            if (index < 0)
                return;

            applyDragSelection(index);
            return;
        }

        updateFromMouse(e);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (selectModeActive && mode == Mode::Frequency)
            return;

        const int index = indexForX((float)e.position.x);
        if (index < 0)
            return;

        if (mode == Mode::Volume)
            processor.setBellVolumeMultiplier(index, 1.0f);
        else
            processor.setBellFrequencyMultiplier(index, 1.0f);

        repaint();
    }

    // Only meaningful for the Frequency-mode bar editor. When select mode is
    // active, clicking a bar toggles it into/out of the selection (drawn
    // blue) instead of dragging its value; the selection can then be bulk
    // frequency-multiplied via applyFrequencyMultiplierToSelection().
    void setSelectModeActive(bool shouldBeActive)
    {
        selectModeActive = shouldBeActive;
        if (!selectModeActive)
            selectedIndices.clear();
        repaint();
    }

    bool isSelectModeActive() const { return selectModeActive; }

    void applyFrequencyMultiplierToSelection(float factor)
    {
        if (mode != Mode::Frequency)
            return;

        for (int index : selectedIndices)
        {
            const float current = processor.getBellFrequencyMultiplier(index);
            processor.setBellFrequencyMultiplier(index, current * factor);
        }

        repaint();
    }

private:
    void timerCallback() override { repaint(); }

    int indexForX(float x) const
    {
        const int count = juce::jlimit(1, FullFilterAudioProcessor::maxBells, processor.activeBellCount.load());
        const float barSlotWidth = (float)getWidth() / (float)count;
        if (barSlotWidth <= 0.0f)
            return -1;
        return juce::jlimit(0, count - 1, (int)(x / barSlotWidth));
    }

    // Adds or removes a single bar from the selection as part of an
    // in-progress drag gesture (see mouseDown/mouseDrag), skipping bars
    // already handled during this same drag so re-entering a bar doesn't
    // flip it back and forth.
    void applyDragSelection(int index)
    {
        if (index == lastDraggedIndex)
            return;
        lastDraggedIndex = index;

        if (dragIsAdding)
            selectedIndices.insert(index);
        else
            selectedIndices.erase(index);

        repaint();
    }

    bool isOutOfAudibleRange(int index) const
    {
        const float freq = processor.bellFrequencies[(size_t)index].load();
        return freq < 10.0f || freq > 20000.0f;
    }

    float normalisedValueForBell(int index) const
    {
        if (mode == Mode::Volume)
            return juce::jlimit(0.0f, 1.0f, processor.getBellVolumeMultiplier(index) / 2.0f);

        // Frequency multiplier is 0.5x-2x; map that log-symmetrically so 1x sits dead centre.
        return juce::jlimit(0.0f, 1.0f, (std::log2(processor.getBellFrequencyMultiplier(index)) + 1.0f) / 2.0f);
    }

    void updateFromMouse(const juce::MouseEvent& e)
    {
        const int index = indexForX((float)e.position.x);
        if (index < 0)
            return;

        const float norm = juce::jlimit(0.0f, 1.0f, 1.0f - (float)e.position.y / (float)juce::jmax(1, getHeight()));

        if (mode == Mode::Volume)
        {
            processor.setBellVolumeMultiplier(index, norm * 2.0f);
        }
        else
        {
            const float multiplier = std::pow(2.0f, norm * 2.0f - 1.0f); // 0.5x .. 2x, 1x at centre
            processor.setBellFrequencyMultiplier(index, multiplier);
        }

        repaint();
    }

    FullFilterAudioProcessor& processor;
    Mode mode;

    bool selectModeActive = false;
    std::set<int> selectedIndices;

    bool dragIsAdding = true;
    int lastDraggedIndex = -1;
};

//==============================================================================
// A horizontal slider whose thumb always snaps to the nearest of a fixed
// set of points, regardless of where along the track it's dragged/clicked.
// Used in place of a combobox where the choices are numeric (e.g. frequency
// multiply factors) so they can be scrubbed through directly.
//==============================================================================
class SnappingSlider : public juce::Slider
{
public:
    explicit SnappingSlider(std::vector<double> snapPointsIn) : snapPoints(std::move(snapPointsIn)) {}

    double snapValue(double attemptedValue, juce::Slider::DragMode) override
    {
        double nearest = snapPoints.front();
        double nearestDistance = std::abs(attemptedValue - nearest);

        for (double point : snapPoints)
        {
            const double distance = std::abs(attemptedValue - point);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = point;
            }
        }

        return nearest;
    }

private:
    std::vector<double> snapPoints;
};

//==============================================================================
// All the actual GUI content, laid out at a fixed native resolution: peaks
// visualizer, volume/mute bar editor, a "square wave" button, a frequency
// bar editor plus its reset/select/multiply controls, and eight rotary
// knobs. Hosted inside FullFilterAudioProcessorEditor at a native size and
// uniformly scaled (via AffineTransform) to fill whatever size the window
// is resized to, so resizing scales the whole GUI as one piece rather than
// reflowing it.
//==============================================================================
class FullFilterEditorContent : public juce::Component
{
public:
    static constexpr int nativeWidth = 900;
    static constexpr int nativeHeight = 500;
    void paintOverChildren(juce::Graphics& g) override;

    explicit FullFilterEditorContent(FullFilterAudioProcessor&);
    ~FullFilterEditorContent() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& text);

    FullFilterAudioProcessor& processor;

    BellVisualizer visualizer;
    BellBarEditor volumeBarEditor;
    BellBarEditor freqBarEditor;

    juce::TextButton squareWaveButton{ "Square Wave (mute even harmonics)" };
    juce::TextButton resetFreqButton{ "Reset Freq" };
    juce::TextButton selectFreqButton{ "Select" };

    // Replaces the old "x multiply" combobox: a slider whose thumb snaps to
    // the same discrete factors the combobox used to offer, so they can be
    // scrubbed through directly. Applies on drag-release (see .cpp) and
    // then resets to 1x (no-op) so the same factor can be re-applied.
    SnappingSlider freqMultiplySlider{ { 0.5, 0.75, 1.0, 1.5, 2.0 } };

    // Acoustic-object presets: reshapes the volume/frequency bars to give
    // the harmonic stack the character of a struck acoustic object.
    juce::ComboBox   acousticPresetCombo;
    juce::TextButton acousticApplyButton{ "Apply" };
    juce::Label      acousticPresetLabel;

    // Section headings, drawn directly in paint() into bounds computed by resized().
    juce::Rectangle<int> volumeLabelArea, freqLabelArea;

    juce::Slider rootSlider, levelSlider, lowpassSlider, qSlider, amountSlider, glideSlider, polyphonySlider, attackSlider;
    juce::Label  rootLabel, levelLabel, lowpassLabel, qLabel, amountLabel, glideLabel, polyphonyLabel, attackLabel;

    // Wavetable position: sits in the bottom knob row, to the left of
    // Amount, styled and attached exactly like the other main knobs (so it
    // is DAW-automatable through the normal APVTS SliderAttachment path).
    juce::Slider positionSlider;
    juce::Label  positionLabel;

    // Per-voice ADSR row: Decay/Sustain/Release knobs (Attack is already one
    // of the eight knobs above, shared with the simplified attack-only mode)
    // plus a toggle that switches extra voices between the full envelope and
    // the original simple attack-fade/fixed-release behaviour.
    juce::Slider decaySlider, sustainSlider, releaseSlider;
    juce::Label  decayLabel, sustainLabel, releaseLabel;
    juce::ToggleButton adsrEnabledButton{ "ADSR" };

    // Lower-left corner: big "Wavetable Import" button, opens a file chooser
    // for a .wav wavetable and analyzes every frame it contains. Scanning
    // through the analyzed frames happens via positionSlider above, in the
    // main knob grid. wavetableModeCombo picks what the analysis drives
    // (bell volume, bell frequency/"filter position", or both).
    juce::TextButton wavetableImportButton{ "WAVETABLE IMPORT" };
    juce::ComboBox wavetableModeCombo;
    std::unique_ptr<juce::FileChooser> wavetableChooser;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<Attachment> rootAttachment, levelAttachment, lowpassAttachment, qAttachment, amountAttachment, glideAttachment,
        polyphonyAttachment, attackAttachment, decayAttachment, sustainAttachment, releaseAttachment, positionAttachment;
    std::unique_ptr<ButtonAttachment> adsrEnabledAttachment;
    std::unique_ptr<ComboAttachment> wavetableModeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullFilterEditorContent)
};

//==============================================================================
// Thin resizable window shell. Holds one FullFilterEditorContent laid out at
// its native resolution and uniformly scales it (same factor on both axes,
// aspect ratio locked by the constrainer) to fit whatever size the user
// resizes the plugin window to — so dragging the corner scales the entire
// GUI as a whole rather than reflowing/stretching individual elements.
//==============================================================================
class FullFilterAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit FullFilterAudioProcessorEditor(FullFilterAudioProcessor&);
    ~FullFilterAudioProcessorEditor() override = default;

    void resized() override;

private:
    FullFilterEditorContent content;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullFilterAudioProcessorEditor)
};