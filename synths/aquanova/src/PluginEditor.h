#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "RandomPreset.h"

namespace aquanova {

//==============================================================================
/** One labelled control bound to a parameter. */
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState& state, const ParamDesc& desc);

    void resized() override;
    void paint (juce::Graphics&) override;

    // Generous cells: the captions wrap to two lines and the knob still has to
    // be readable once the whole panel is scaled to the window width.
    static constexpr int kWidth    = 102;
    static constexpr int kHeight   = 104;
    static constexpr int kCaptionH = 30;

private:
    juce::String title;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttach;
};

//==============================================================================
/** A titled panel.

    A section may hold several *banks* of controls - oscillator 1, 2 and 3, say -
    with a selector in its header, so only one set is on screen at a time.
    Within a bank, controls are collected into named *groups* separated by fine
    rules, which is what makes it possible to see at a glance which knob feeds
    what.
*/
class Section : public juce::Component
{
public:
    Section (juce::String titleIn, juce::Colour accentIn, const juce::StringArray& bankNames);

    void add (juce::AudioProcessorValueTreeState& state, const ParamDesc& desc,
              int bank, const juce::String& groupName);

    /** Works out every control's position. Call once after adding them all. */
    void performLayout (int columns);

    int  getPreferredWidth() const noexcept  { return preferredWidth; }
    int  getPreferredHeight() const noexcept { return preferredHeight; }
    int  numControls() const noexcept        { return controls.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHeaderHeight = 30;
    static constexpr int kGroupHeader  = 17;
    static constexpr int kPad          = 9;

private:
    void setBank (int newBank);

    struct Entry
    {
        ParamControl* control;
        int bank;
        int group;                       // index into groupNames
        juce::Rectangle<int> bounds;
    };

    juce::String title;
    juce::Colour accent;
    juce::StringArray groupNames;
    juce::OwnedArray<ParamControl> controls;
    juce::Array<Entry> entries;

    juce::ComboBox bankSelector;
    bool hasBanks = false;
    int currentBank = 0;

    // Per bank: the rules to draw, and each group's caption position.
    struct Rule { int bank, y; };
    juce::Array<Rule> rules;
    struct Caption { int bank, group, x, y; };
    juce::Array<Caption> captions;

    int preferredWidth = 200, preferredHeight = 100;
};

//==============================================================================
/** A keyboard you can play with the mouse, drawn in the same card chrome as
    the parameter panels but holding a juce::MidiKeyboardComponent instead of
    a control grid. Bound to the processor's MidiKeyboardState, so a click
    plays a note and an incoming MIDI note lights the matching key. */
class KeyboardCard : public juce::Component
{
public:
    KeyboardCard (juce::MidiKeyboardState& state, juce::String titleIn, juce::Colour accentIn);

    int getPreferredHeight() const noexcept
    {
        return kHeaderHeight + kPad + kKeyboardHeight + kPad;
    }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kHeaderHeight   = 30;
    static constexpr int kPad            = 9;
    static constexpr int kKeyboardHeight = 90;

private:
    juce::String title;
    juce::Colour accent;
    juce::MidiKeyboardComponent keyboard;
};

//==============================================================================
class AquaNovaEditor : public juce::AudioProcessorEditor
{
public:
    explicit AquaNovaEditor (AquaNovaProcessor&);
    ~AquaNovaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buildSections();
    void buildKeyboardCard();
    void buildHeader();
    void layoutCanvas();
    void refreshPatchName();

    void loadPatch();
    void savePatch();
    void randomise();
    void updateTooltips();

    AquaNovaProcessor& processor;
    AquaNovaLookAndFeel lnf;

    // The panel is always scaled to fit the window's width; anything that does
    // not fit vertically is simply scrolled to. No zoom to get lost in.
    juce::Viewport viewport;
    juce::Component canvasHolder;
    juce::Component canvas;
    juce::OwnedArray<Section> sections;
    std::unique_ptr<KeyboardCard> keyboardCard;

    // The header row is laid out once at a fixed "design" width (below) and
    // then this whole container is scaled down to fit the actual window,
    // the same way the canvas scales - so a narrower window shrinks the row
    // instead of letting its controls overlap or clip their own labels.
    juce::Component headerControls;
    static constexpr int kHeaderDesignWidth   = 1280;
    static constexpr int kHeaderContentHeight = 30;
    static constexpr int kHeaderInsetX = 10, kHeaderInsetY = 8;

    juce::Label titleLabel;
    juce::TextEditor patchNameEditor;
    juce::ComboBox characterBox;    juce::TextButton loadButton { "Load" },
                     saveButton { "Save" },
                     randomButton { "Randomise" };
    juce::ToggleButton tooltipButton { "Hints" };
    juce::ToggleButton dcBlockButton { "DC Block" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dcBlockAttach;
    juce::Label dcSpeedLabel;
    juce::Slider dcSpeedSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dcSpeedAttach;

    // Not an APVTS parameter: the mod wheel is performance data, not part of
    // the patch, so it is not saved and does not appear on undo. Moving it
    // sends an ordinary CC1 straight to the engine, same as a hardware wheel.
    juce::Label modWheelLabel;
    juce::Slider modWheelSlider;

    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Random rng;

    static constexpr int kCanvasWidth  = 1680;
    static constexpr int kHeaderHeight = 46;
    int canvasHeight = 900;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaNovaEditor)
};

} // namespace aquanova