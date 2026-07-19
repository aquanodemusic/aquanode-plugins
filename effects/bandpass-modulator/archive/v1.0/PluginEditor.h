#pragma once
// #pragma once is a compiler instruction.
// It tells the compiler to include this header file only ONCE per compilation unit.
// This prevents "redefinition" and multiple-inclusion errors.
// It is a modern and simpler alternative to traditional include guards
// (#ifndef as if not defined / #define / #endif).

#include <JuceHeader.h>
// Includes the entire JUCE framework in one go.
// This gives access to:
// - GUI classes (Slider, Label, Button, Graphics, ComboBox, etc.)
// - Audio processing utilities
// - ValueTree, Timer, DSP, math helpers, containers, and more
// In JUCE projects, this is typically the only JUCE include you need.

#include "PluginProcessor.h"
// Includes the processor header file.
// This allows the Editor (GUI) to:
// - Hold a reference to the AudioProcessor
// - Access parameters via the APVTS (Audio Processor Value Tree State)
// - Communicate with the DSP "brain" of the plugin
// Without this include, the Editor would not know what the processor class is.

// This class defines the Graphical User Interface (GUI) of the plugin.
// It does NOT process audio — it only handles visuals and user interaction.
class BandpassModulatorAudioProcessorEditor
    : public juce::AudioProcessorEditor,
    // The ':' introduces inheritance.
    // This means the Editor is a juce::AudioProcessorEditor.
    // juce::AudioProcessorEditor is the base class for all JUCE plugin UIs.
    public juce::Timer
    // This class also inherits from juce::Timer.
    // juce::Timer allows us to run code periodically via timerCallback().
{
    public:
        // CONSTRUCTOR AND DESTRUCTOR
        BandpassModulatorAudioProcessorEditor(BandpassModulatorAudioProcessor& processor);
        // This is the constructor.
        // It is called when the plugin UI is created.
        // The '&' symbol means "reference".
        // A reference is NOT a copy.
        // The editor receives a reference to the already existing processor
        // that was created and is owned by the DAW.
        // This avoids copying the processor and guarantees both
        // the UI and DSP refer to the same object.

        ~BandpassModulatorAudioProcessorEditor() override;
        // This is the destructor.
        // The '~' (tilde) means "destructor" in C++.
        // It is called automatically when the editor is destroyed.
        // 'override' ensures this function overrides a virtual destructor
        // in the base class.
        // If it does NOT override correctly, the compiler will throw an error.

        // STANDARD JUCE CALLBACKS
        void paint(juce::Graphics& g) override;
        // paint() is called whenever the UI needs to be redrawn.
        // 'juce::Graphics' is a class inside the 'juce' namespace.
        // The '::' symbol is the scope resolution operator.
        // It means: "Graphics that belongs to the juce namespace".
        // The '&' means the Graphics object is passed by reference:
        // - No copy is made
        // - Drawing operations affect the real screen buffer

        void resized() override;
        // resized() is called when the editor window is created or resized.
        // This is where you position and size all UI components.
        // No drawing should happen here.

        void timerCallback() override;
        // timerCallback() is inherited from juce::Timer.
        // It is called repeatedly at a fixed interval.
        // Common uses include animations and dynamic visual updates.

    private:
        // HELPER FUNCTIONS
        void drawFilterCurve(juce::Graphics& g);
        // Helper function used to draw the filter curve visualization.
        // Keeping this logic outside of paint() makes the code cleaner.

        // PROCESSOR REFERENCE
        BandpassModulatorAudioProcessor& audioProcessor;
        // This is a reference to the AudioProcessor.
        // The '&' ensures:
        // - No ownership
        // - No copying
        // - The processor must always exist
        // The editor uses this reference to read parameters
        // and stay in sync with the DSP.

        // UI COMPONENTS
        juce::Slider minFreqSlider, maxFreqSlider, glideTimeSlider, stayTimeSlider,
            panningSlider, dryWetSlider, widthSlider, wetGainSlider;
        // juce::Slider represents knobs or faders.
        // Each slider will later be connected to a processor parameter.
        juce::Label minFreqLabel, maxFreqLabel, glideTimeLabel, stayTimeLabel,
            panningLabel, dryWetLabel, widthLabel, wetGainLabel;
        // juce::Label displays static text such as parameter names.
        juce::ComboBox modeSelector;
        // juce::ComboBox is a dropdown menu for selecting modes.
        juce::ToggleButton panningLfoSwitch;
        juce::ToggleButton noteLockSwitch;
        // juce::ToggleButton represents an on/off switch.
        juce::ToggleButton btnC, btnD, btnE, btnF, btnG, btnA, btnB;
        // Individual toggle buttons used as musical note selectors.
        juce::Label brandingLabel;
        // Label used for branding text.

        // VISUAL EFFECTS
        juce::DropShadowEffect minFreqShadow, maxFreqShadow, glideTimeShadow, stayTimeShadow,
            panningShadow, dryWetShadow, widthShadow, wetGainShadow,
            toggleShadow, brandingShadow;
        // juce::DropShadowEffect adds depth and glow to UI elements.
        // This is purely visual and does not affect audio.

        // PARAMETER ATTACHMENTS
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        // 'using' creates a type alias.
        // This makes long type names shorter and easier to read.
        // 'juce::AudioProcessorValueTreeState::SliderAttachment' means:
        // - SliderAttachment is a class
        // - It lives inside AudioProcessorValueTreeState
        // - Which itself lives inside the juce namespace

        using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        // These aliases do the same thing for buttons and combo boxes.

        std::unique_ptr<SliderAttachment> minFreqAttachment;
        std::unique_ptr<SliderAttachment> maxFreqAttachment;
        std::unique_ptr<SliderAttachment> glideTimeAttachment;
        std::unique_ptr<SliderAttachment> stayTimeAttachment;
        std::unique_ptr<SliderAttachment> panningAttachment;
        std::unique_ptr<SliderAttachment> dryWetAttachment;
        std::unique_ptr<SliderAttachment> widthAttachment;
        std::unique_ptr<SliderAttachment> wetGainAttachment;
        // std::unique_ptr is a smart pointer.
        // It owns exactly one object and deletes it automatically.
        // This prevents memory leaks and manual delete calls.

        std::unique_ptr<ButtonAttachment> lfoActiveAttachment;
        std::unique_ptr<ButtonAttachment> noteLockAttachment;
        std::unique_ptr<ButtonAttachment> attC, attD, attE, attF, attG, attA, attB;
        // Button attachments connect ToggleButtons to boolean parameters.

        std::unique_ptr<ComboBoxAttachment> modeAttachment;
        // Attachment connecting the ComboBox to a choice parameter.

        // SAFETY
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandpassModulatorAudioProcessorEditor)
            // This JUCE macro:
            // - Prevents copying and assigning this class
            // - Adds a memory leak detector in debug builds
};