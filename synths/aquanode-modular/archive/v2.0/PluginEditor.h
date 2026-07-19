#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class PatchCanvas;

//==============================================================================
// Generic module rectangle - built entirely from the instance's
// ModuleDescriptor. No module-specific UI code lives here.
class ModuleComponent : public juce::Component
{
public:
    ModuleComponent (AquanodeModularAudioProcessor& proc, PatchCanvas& canvas, int instanceId);

    int getInstanceId() const { return instanceId; }

    struct SocketRef
    {
        juce::String socketId;
        bool isInput { false };
        aquanode::SocketKind kind { aquanode::SocketKind::Audio };
        juce::Point<int> centre;   // local coords
    };

    // returns nullptr if no socket is near the given local point
    const SocketRef* findSocketNear (juce::Point<int> localPos, int radius = 14) const;
    juce::Point<int> socketCentreInParent (const juce::String& socketId, bool isInput) const;

    // knob modulation: hit-test rotary knobs that accept param cables
    const aquanode::ParamSpec* findModulatableKnobNear (juce::Point<int> localPos) const;
    juce::Point<int> knobCentreInParent (const juce::String& paramId) const;
    void showKnobModMenu (const juce::String& paramId);   // right-click: depth / remove
    bool knobHasModulation (const juce::String& paramId) const;

    // touch-only long-press state (desktop uses right-click and never arms these)
    juce::String longPressParamId;
    bool longPressMoved { false };

    void paintOverChildren (juce::Graphics& g) override;   // modulation rings

    void refreshLayout();          // recompute (visibility may have changed) and resize
    void refreshFromModel();       // pull all knob/combo values from the DSP (mutator etc.)

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr int moduleWidth = 250;

private:
    struct ControlEntry
    {
        const aquanode::ParamSpec* spec { nullptr };
        std::unique_ptr<juce::Component> component;
        juce::Rectangle<int> labelArea;
        bool visible { true };
    };

    bool isParamVisible (const aquanode::ParamSpec& spec) const;
    int layoutEverything (bool apply);   // returns total height
    void buildControls();

    AquanodeModularAudioProcessor& processor;
    PatchCanvas& canvas;
    const int instanceId;

    std::vector<ControlEntry> controls;
    std::vector<SocketRef> sockets;
    std::unique_ptr<juce::Component> extraContent;
    int headerHeight { 0 };

    juce::ComponentDragger dragger;
    bool draggingCable { false };
    bool draggingBody { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleComponent)
};

//==============================================================================
// The patch map: gray background with a "+" grid, holds the module
// rectangles, draws all cables, handles cable dragging and selection.
class PatchCanvas : public juce::Component
{
public:
    explicit PatchCanvas (AquanodeModularAudioProcessor& proc);

    void rebuild();                // sync child components with the processor model
    void refreshAllModuleValues(); // pull every module's knob values from the DSP

    int getSelectedModuleId() const { return selectedModuleId; }
    void selectModule (int instanceId);

    // cable dragging (driven by ModuleComponent mouse events)
    void beginCableDrag (int moduleId, const juce::String& socketId, bool isInput,
                         aquanode::SocketKind kind, juce::Point<int> canvasPos);
    void updateCableDrag (juce::Point<int> canvasPos);
    void endCableDrag (juce::Point<int> canvasPos);
    bool isDraggingCable() const { return dragActive; }

    void moduleMoved();            // repaint cables while a module is dragged

    juce::Point<int> getPanOffset() const { return panOffset; }
    void resetView();              // recentre the patch (pan back to origin)

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void resized() override;

private:
    ModuleComponent* findModuleComponent (int instanceId) const;
    juce::Path cablePath (juce::Point<float> from, juce::Point<float> to) const;
    bool getCableEndpoints (const CableInfo& c, juce::Point<float>& from, juce::Point<float>& to) const;
    int cableIndexNear (juce::Point<float> pos, float maxDistance = 9.0f) const;
    int paramCableIndexNear (juce::Point<float> pos, float maxDistance = 6.0f) const;
    void applyPanToChildren();

    AquanodeModularAudioProcessor& processor;
    std::vector<std::unique_ptr<ModuleComponent>> moduleComponents;

    int selectedModuleId { -1 };
    int selectedCableIndex { -1 };
    int selectedParamCableIndex { -1 };   // knob-modulation cable under the mouse

    // live cable drag state
    bool dragActive { false };
    int dragModuleId { -1 };
    juce::String dragSocketId;
    bool dragFromInput { false };
    aquanode::SocketKind dragKind { aquanode::SocketKind::Audio };
    juce::Point<int> dragCurrentPos;

    juce::Path gridPath;

    // view panning (drag on empty canvas)
    juce::Point<int> panOffset;
    bool panning { false };
    juce::Point<int> panStartOffset;
    juce::Point<int> panMouseStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchCanvas)
};

//==============================================================================
// Left sidebar: black, section titles + module names from the factory.
class SidebarComponent : public juce::Component
{
public:
    explicit SidebarComponent (std::function<void (const juce::String& typeId)> onModuleClicked);

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr int sidebarWidth = 220;
    static constexpr int scrollBarWidth = 12;   // reserved so the Viewport's scrollbar never covers text

private:
    struct Row
    {
        juce::Rectangle<int> area;
        juce::String text;
        juce::String typeId;       // empty for section titles
        juce::Colour colour;
    };

    void buildRows();

    std::vector<Row> rows;
    std::function<void (const juce::String&)> moduleClicked;
};

//==============================================================================
// Patch Mutator - G2-style interactive evolution over the patch's knob
// values. Topology never changes; Input/Output modules are excluded so a
// mutation can never blast the master level.
class MutatorPanel : public juce::Component
{
public:
    MutatorPanel (AquanodeModularAudioProcessor&, PatchCanvas&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // instanceId -> (paramId -> value): a "genome" of the current patch
    using Snapshot = std::map<int, std::map<juce::String, float>>;

    Snapshot capture() const;
    void apply (const Snapshot&);
    void pushHistory();
    void doMutate();
    void doBreed();
    void doRecall (const Snapshot&, bool exists);
    float mutateValue (const aquanode::ParamSpec&, float current, float amount);

    AquanodeModularAudioProcessor& processor;
    PatchCanvas& canvas;

    juce::Slider amountSlider;
    juce::TextButton mutateButton  { "Mutate" };
    juce::TextButton undoButton    { "Undo" };
    juce::TextButton storeAButton  { "Store A" };
    juce::TextButton storeBButton  { "Store B" };
    juce::TextButton recallAButton { "Recall A" };
    juce::TextButton recallBButton { "Recall B" };
    juce::TextButton breedButton   { "Breed AxB" };

    Snapshot slotA, slotB;
    bool hasA { false }, hasB { false };
    std::vector<Snapshot> history;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutatorPanel)
};

//==============================================================================
class AquanodeModularAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::ChangeListener
{
public:
    explicit AquanodeModularAudioProcessorEditor (AquanodeModularAudioProcessor&);
    ~AquanodeModularAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    AquanodeModularAudioProcessor& processor;

    SidebarComponent sidebar;
    juce::Viewport sidebarViewport;   // makes the sidebar scrollable when its content exceeds the visible height
    PatchCanvas canvas;

    juce::TextButton initButton    { "Initialize Patch" };
    juce::TextButton cloneButton   { "Clone Selected" };
    juce::TextButton deleteButton  { "Delete Selected" };
    juce::TextButton exportButton  { "Export Patch" };
    juce::TextButton importButton  { "Import Patch" };
    juce::TextButton mutatorButton { "Mutator" };

    MutatorPanel mutatorPanel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    int placementCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquanodeModularAudioProcessorEditor)
};