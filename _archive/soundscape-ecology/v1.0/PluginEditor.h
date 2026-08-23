/*
  ==============================================================================

    PluginEditor.h
    Soundscape Ecology — interface.

    Visual approach: translucent frosted panels floating on a painted ecological
    gradient. No photographic assets — everything is drawn, so it scales cleanly
    and the background can RESPOND to the synthesis. The gradient warms with the
    temperature control and darkens with time of day, drifting slowly enough
    that the user notices it only subliminally.

    Layout (one screen, no tabs — switching panels is the main usability
    complaint against deep synths of this kind):

      +------------------------------------------------------------------+
      |  header: name, preset, temperature, time of day, output          |
      +----------------+----------------+--------------------------------+
      |  SLOT I        |  SLOT II       |  SLOT III                      |
      |  BIOPHONY      |  GEOPHONY      |  ANTHROPHONY                   |
      |  generator     |  generator     |  generator                     |
      |  8 physics     |  8 physics     |  8 physics                     |
      |  articulation  |  articulation  |  articulation                  |
      |  draw canvas   |  draw canvas   |  draw canvas                   |
      +----------------+----------------+--------------------------------+
      |  THE FIELD (population map)      |  HABITAT      |  NICHE        |
      +------------------------------------------------------------------+

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace soundeco
{
    //==========================================================================
    // Palette. Green-brown earth through to soft blue water, with warm light.
    //==========================================================================
    struct Palette
    {
        // Lush saturated jungle: deep emerald through teal to cyan, with a
        // cool bark accent. Deliberately NO desert/ochre browns — earlier
        // versions drifted to burnt soil because the browns were load-bearing
        // in the gradient rather than an accent.
        static juce::Colour canopy()    { return juce::Colour (0xff2fd17a); }  // vivid leaf
        static juce::Colour moss()      { return juce::Colour (0xff118f56); }  // deep emerald
        static juce::Colour deepJade()  { return juce::Colour (0xff064f3d); }  // shadow under canopy
        static juce::Colour earth()     { return juce::Colour (0xff3d5a3a); }  // cool damp bark
        // Anthrophony accent. earth() is deliberately a green-grey (it sits in
        // the background gradient, where a true brown read as burnt soil), but
        // brightened as a slot accent it came out plainly grey. This is a real
        // warm brown, used ONLY as the anthrophony accent, never in the
        // gradient.
        static juce::Colour bark()      { return juce::Colour (0xffa9713f); }  // warm brown
        static juce::Colour loam()      { return juce::Colour (0xff123a30); }  // wet dark green-brown
        static juce::Colour water()     { return juce::Colour (0xff17b8d4); }  // saturated lagoon
        static juce::Colour sky()       { return juce::Colour (0xff5fe3e0); }  // bright aqua
        static juce::Colour lightWarm() { return juce::Colour (0xffa8f05a); }  // sunlit new growth
        static juce::Colour glass()     { return juce::Colours::white.withAlpha (0.16f); }
        static juce::Colour glassEdge() { return juce::Colours::white.withAlpha (0.30f); }
        static juce::Colour text()      { return juce::Colours::white.withAlpha (0.92f); }
        static juce::Colour textDim()   { return juce::Colours::white.withAlpha (0.62f); }
    };

    //==========================================================================
    // Frosted-glass panel: a translucent rounded rectangle with a soft edge.
    //==========================================================================
    class GlassPanel : public juce::Component
    {
    public:
        explicit GlassPanel (juce::String titleText = {}) : title (std::move (titleText)) { }
        void paint (juce::Graphics& g) override;
        void setTitle (const juce::String& t) { title = t; repaint(); }
        void setAccent (juce::Colour c) { accent = c; repaint(); }
        int  titleHeight() const { return title.isEmpty() ? 8 : 30; }

    private:
        juce::String title;
        juce::Colour accent { Palette::canopy() };
    };

    //==========================================================================
    // Knob styling: a soft glass disc with a luminous arc.
    //==========================================================================
    class EcoLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        EcoLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                               float pos, float startAngle, float endAngle,
                               juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h,
                               float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool isDown,
                           int bx, int by, int bw, int bh, juce::ComboBox&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool shouldDrawAsHighlighted, bool shouldDrawAsDown) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void setAccent (juce::Colour c) { accent = c; }

    private:
        juce::Colour accent { Palette::canopy() };
    };

    //==========================================================================
    // Labelled rotary control.
    //==========================================================================
    class EcoKnob : public juce::Component,
                    public juce::SettableTooltipClient
    {
    public:
        EcoKnob();
        void resized() override;
        void paint (juce::Graphics&) override;
        void setLabel (const juce::String& t);
        // Horizontal bar mode is used inside the three category panels, where
        // it buys back the width that rotary labels were wasting and lets the
        // type be legible.
        void setHorizontal (bool h) { horizontal = h; rebuildStyle(); }
        juce::Slider slider;

    private:
        void rebuildStyle();
        juce::String caption;
        bool horizontal = false;
    };

    // Plain-language description for a control, shown as a tooltip.
    juce::String describeControl (const juce::String& label);

    //==========================================================================
    // The draw canvas. Pitch or amplitude across one event, drawn freehand.
    //
    // For the bird models this is the gesture canvas: the closed loop in the
    // (pressure, tension) plane that Mindlin's group use to synthesise
    // syllables. For everything else it shapes the event contour.
    //==========================================================================
    class DrawCanvas : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        DrawCanvas (std::array<float, kDrawPoints>& target, juce::Colour accentColour,
                    juce::String captionText);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void setAccent (juce::Colour c) { accent = c; repaint(); }
        std::function<void()> onEdit;

    private:
        void writePoint (juce::Point<float> p);
        std::array<float, kDrawPoints>& curve;
        juce::Colour accent;
        juce::String caption;
    };

    //==========================================================================
    // The Field: a top-down map of the population. Each individual is a soft
    // dot that pulses when it calls; the listener sits at the centre. This is
    // the most legible possible view of the thing that makes the plugin
    // distinctive, and it is not hard to draw.
    //==========================================================================
    class FieldMap : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
    {
    public:
        explicit FieldMap (SoundscapeEcologyAudioProcessor& p);
        void paint (juce::Graphics&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;
        SoundscapeEcologyAudioProcessor& processor;
        float pulse[kNumSlots] { };
        float zoom = 1.0f;
        juce::Point<float> offset { 0.0f, 0.0f }, dragStart;
    };

    //==========================================================================
    // One slot column.
    //==========================================================================
    class SlotPanel : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        SlotPanel (SoundscapeEcologyAudioProcessor& p, int slotIndex);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void refreshKnobLabels();
        // Rebuilds the title, colour and model list after a class change.
        void refreshClass();
        int  currentPhony() const;

    private:
        juce::Rectangle<int> titleArea() const;
        SoundscapeEcologyAudioProcessor& processor;
        int slot;
        juce::Colour accent;
        bool titleHot = false;
        bool updatingModelBox = false;   // guards the model box against feedback

        juce::ComboBox modelBox;
        juce::ToggleButton enableButton { "" };
        juce::TextButton randomButton { "RND" };

        std::array<EcoKnob, kNumModelKnobs> physicsKnobs;
        EcoKnob rateKnob, phraseKnob, rhythmKnob, jitterKnob, swingKnob;
        EcoKnob attackKnob, decayKnob, durationKnob;
        EcoKnob popKnob, spreadKnob, distanceKnob, diversityKnob, syncKnob, parallaxKnob, elevationKnob;
        EcoKnob levelKnob;

        std::unique_ptr<DrawCanvas> pitchCanvas, ampCanvas;
        juce::ToggleButton drawPitchButton { "Pitch" }, drawAmpButton { "Amp" };
        EcoKnob drawDepthKnob;

        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        std::vector<std::unique_ptr<SA>> sliderAttachments;
        std::vector<std::unique_ptr<BA>> buttonAttachments;
        // NOT a ComboBoxAttachment. That helper assumes the parameter's range
        // spans exactly the number of combo items: it converts an index to a
        // value via index/(numItems-1) scaled across the full range. Our model
        // parameter has a fixed 0..7 range (the largest class), so for any
        // class with fewer than 8 models the mapping skewed -- Anthrophony's
        // 6 items made "Bell" select Machinery, and both Bell and Machinery
        // landed on the same model. ParameterAttachment hands us the raw
        // denormalised value instead, so index and value stay identical.
        std::unique_ptr<juce::ParameterAttachment> modelAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> unusedAttachment;

        void attach (EcoKnob& k, const juce::String& paramID, const juce::String& label);

        // Rule positions are COMPUTED in resized() and read by paint(), so the
        // two can never disagree. Hardcoding them was why section headings
        // printed across the knob rows.
        struct Rule { int y; juce::String text; };
        std::vector<Rule> rules;
    };
}

//==============================================================================
class SoundscapeEcologyAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SoundscapeEcologyAudioProcessorEditor (SoundscapeEcologyAudioProcessor&);
    ~SoundscapeEcologyAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintBackground (juce::Graphics&);
    void paintPetals (juce::Graphics&);

    SoundscapeEcologyAudioProcessor& processor;
    soundeco::EcoLookAndFeel lookAndFeel;

    // header
    soundeco::GlassPanel headerPanel;

    // preset bar
    juce::ComboBox presetBox;
    juce::TextButton prevButton { "PREV" }, nextButton { "NEXT" };
    juce::TextButton saveButton { "Save" }, loadButton { "Load" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::TooltipWindow tooltips { this, 2000 };   // 2 second hover delay
    void refreshPresetBox();
    void doSavePreset();
    void doLoadPreset();
    std::unique_ptr<juce::AlertWindow> saveWindow;
    soundeco::EcoKnob temperatureKnob, timeOfDayKnob, humidityKnob, nicheKnob,
                      weatherKnob, outputKnob;

    // three slot columns
    std::unique_ptr<soundeco::SlotPanel> slots[soundeco::kNumSlots];

    // footer
    soundeco::GlassPanel fieldPanel, habitatPanel, midiPanel;
    juce::ComboBox midiModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiModeAttachment;
    soundeco::EcoKnob midiAttackKnob, midiReleaseKnob, midiVelKnob, midiPitchKnob, midiRootKnob;
    std::unique_ptr<soundeco::FieldMap> fieldMap;
    soundeco::EcoKnob habSizeKnob, habDampKnob, habMixKnob, widthKnob;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::vector<std::unique_ptr<SA>> attachments;
    void attachGlobal (soundeco::EcoKnob& k, const juce::String& id, const juce::String& label);

    // drifting petals
    struct Petal { float x, y, rot, spin, speed, size, alpha; };
    std::vector<Petal> petals;
    float driftPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundscapeEcologyAudioProcessorEditor)
};
