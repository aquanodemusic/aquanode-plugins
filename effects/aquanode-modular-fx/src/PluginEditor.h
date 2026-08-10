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

    //=== collapse (hide the controls, keep the header and its sockets) ========
    // Collapsed modules shrink to their header, so a big patch stays readable
    // without losing any routing: every socket is still there and every audio
    // cable still lands. Knob-modulation cables are the one thing that cannot
    // survive, since their target has no on-screen position while hidden - so
    // they are simply not drawn, and reappear untouched on the same knob when
    // the module is opened again. The cable itself is never removed.
    bool isCollapsed() const;
    void toggleCollapsed();

    // false while collapsed, or when the knob is hidden by a visibleWhen rule.
    // The canvas asks before drawing (or hit-testing) a param cable.
    bool isKnobVisible (const juce::String& paramId) const;

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

    // the little collapse tab drawn above the title. The drawn rectangle is
    // deliberately smaller than the area this returns: the hit target has to
    // stay thumb-sized on a phone without a chunky bar spoiling the header.
    juce::Rectangle<int> collapseHitArea() const;
    juce::Rectangle<int> collapseTabArea() const;
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

    // triple-click self-cable delete: three rapid clicks anywhere inside the
    // module (without the mouse wandering) remove every cable the module has
    // routed back into itself. Self-modulation cables start and end on the
    // same rectangle, which makes them impossible to select on the canvas -
    // this gesture is their delete path. Regular cables are untouched.
    bool handleRapidClick (const juce::MouseEvent& e);   // true = self cables were deleted
    juce::int64 lastClickTimeMs { 0 };
    juce::Point<int> lastClickScreenPos;
    int rapidClickCount { 0 };

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
    void resetView();              // recentre the patch (pan back to origin, zoom to 100%)

    //=== view zoom ============================================================
    // Zoom is a pure VIEW transform, never a re-layout: module bounds stay in
    // world coordinates and every module carries the same AffineTransform.
    // JUCE inverts that transform for hit-testing and for the coordinates in
    // every MouseEvent, so knobs, cable drags and ComponentDragger all keep
    // working at any zoom with no scaling maths of their own - a 10 px finger
    // move at 50 % becomes a 20 unit world move, and the module still tracks
    // the finger exactly. Nothing is re-measured, so no layout can break.
    static constexpr float minZoom = 0.3f;
    static constexpr float maxZoom = 2.0f;

    float getZoom() const { return zoom; }

    // keeps the world point currently under focusScreenPos pinned there
    void setZoom (float newZoom, juce::Point<float> focusScreenPos);
    void zoomBy (float factor);    // about the centre of the visible canvas

    juce::AffineTransform worldToScreen() const
    {
        return juce::AffineTransform::scale (zoom)
                   .translated ((float) panOffset.x, (float) panOffset.y);
    }

    juce::Point<float> screenToWorld (juce::Point<float> p) const
    {
        return { (p.x - (float) panOffset.x) / zoom,
                 (p.y - (float) panOffset.y) / zoom };
    }

    // called by the editor when a module is collapsed/expanded
    void moduleCollapsedChanged() { repaint(); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

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
    void applyViewToChildren();    // push world position + view transform onto every module
    void rebuildGrid();
    float gridScreenSpacing() const;

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

    // view panning (drag on empty canvas) + zoom
    float zoom { 1.0f };
    juce::Point<int> panOffset;
    bool panning { false };
    juce::Point<int> panStartOffset;
    juce::Point<int> panMouseStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchCanvas)
};

//==============================================================================
// The sidebar's hover tooltip: a floating panel of wrapped text. Lives on the
// top-level component rather than inside the sidebar, so it can extend past
// the sidebar's Viewport instead of being clipped by it.
class SidebarTooltip : public juce::Component
{
public:
    SidebarTooltip() { setInterceptsMouseClicks (false, false); }

    static constexpr int padding = 10;

    // sizes itself to the text; maxWidth caps how wide it grows before wrapping
    void setText (const juce::String& title, const juce::String& body, int maxWidth);

    void paint (juce::Graphics& g) override;

private:
    juce::TextLayout layout;
};

//==============================================================================
// Left sidebar: black, section titles + module names from the factory.
class SidebarComponent : public juce::Component,
                         private juce::Timer
{
public:
    explicit SidebarComponent (std::function<void (const juce::String& typeId)> onModuleClicked);
    ~SidebarComponent() override;

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

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

    //=== hover tooltip ======================================================
    static constexpr int hoverDelayMs = 1500;   // a deliberate rest, not a twitch
    static constexpr int hoverSlackPx = 3;      // hand tremor must not restart the wait

    void timerCallback() override;
    void showTooltipFor (const Row& row);
    void hideTooltip();
    const Row* rowAt (juce::Point<int> pos) const;

    std::vector<Row> rows;
    std::function<void (const juce::String&)> moduleClicked;

    std::unique_ptr<SidebarTooltip> tooltip;
    juce::String hoveredTypeId;
    juce::Point<int> hoverPos;
    juce::uint32 hoverStartMs { 0 };
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

    // Dark bar across the top of the patch area, mirroring the mutator strip at
    // the bottom. Purely the background: the buttons stay editor children so
    // their existing wiring is untouched, and they are added after this so they
    // sit in front of it.
    struct ToolbarStrip : public juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xdd141210)); }
    };
    ToolbarStrip toolbarStrip;

    juce::TextButton initButton    { "Initialize Patch" };
    juce::TextButton cloneButton   { "Clone Selected" };
    juce::TextButton deleteButton  { "Delete Selected" };
    juce::TextButton exportButton  { "Export Patch" };
    juce::TextButton importButton  { "Import Patch" };
    juce::TextButton mutatorButton { "Mutator" };

    // Panel toggles. Hiding the sidebar and the keyboard gives the patch area
    // the whole screen, which is the difference between usable and unusable on
    // a phone. Both are pure layout changes - resized() re-runs and the canvas
    // simply gets more room.
    juce::TextButton sidebarButton { "Sidebar" };
    juce::TextButton keysButton    { "Keys" };
    bool sidebarVisible { true };
    bool keyboardVisible { true };

    // Floating zoom cluster in the bottom-right of the patch area: [-][100%][+].
    // Kept out of the toolbar so it stays reachable by a thumb and does not
    // squeeze the six patch buttons any further on a narrow screen. Tapping
    // the readout resets the view to 100 % at the origin.
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomInButton  { "+" };
    juce::TextButton zoomResetButton { "100%" };
    void updateZoomReadout();

    MutatorPanel mutatorPanel;

    // Bottom on-screen keyboard. Constructed always (cheap), but only shown and
    // given layout space on Android; desktop keeps its host/hardware MIDI path.
    std::unique_ptr<juce::MidiKeyboardComponent> midiKeyboard;

    std::unique_ptr<juce::FileChooser> fileChooser;
    int placementCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquanodeModularAudioProcessorEditor)
};