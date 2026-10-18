#include "PluginEditor.h"
#include "Presets/EffectAdders.h"
#include "Presets/PresetGenerators.h"
#include "Plugins/PluginCatalog.h"
#include "Plugins/HostedPlugin.h"

using namespace aquanode;

//==============================================================================
// layout constants
//==============================================================================
namespace layout
{
    constexpr int pad = 10;
    constexpr int socketColW = 76;     // width of each side socket column in the header
    constexpr int socketSlotH = 30;    // vertical space per socket (incl. label)
    constexpr int headerMinH = 52;     // minimum header height (title area)
    constexpr int knobRowH = 82;       // taller -> bigger knobs
    constexpr int wideRowH = 32;       // rows containing only combos/buttons
    constexpr int barRowH = 40;        // rows of horizontal bars (no knobs)
    constexpr int unitsPerRow = 5;
    constexpr int socketSize = 15;
    constexpr int gridSpacing = 24;
}

//==============================================================================
// ModuleComponent
//==============================================================================
ModuleComponent::ModuleComponent (AquanodeModularAudioProcessor& proc, PatchCanvas& owner, int idToUse)
    : processor (proc), canvas (owner), instanceId (idToUse)
{
    buildControls();
    const int h = layoutEverything (false);
    setSize (moduleWidth(), h);

    // Bounds are WORLD coordinates; pan and zoom live in the transform that
    // the canvas pushes onto every module (see PatchCanvas::applyViewToChildren).
    if (auto* inst = processor.getInstance (instanceId))
        setTopLeftPosition (inst->position);
    setTransform (canvas.worldToScreen());

    layoutEverything (true);
}

// the widest row of controls, in knob units (a module with rows of more than
// five knobs grows to fit them rather than letting them spill over its edge)
int ModuleComponent::maxRowUnits() const
{
    std::map<int, int> units;
    if (auto* inst = processor.getInstance (instanceId))
        for (const auto& spec : inst->descriptor().params)
            if (! spec.hidden)
                units[spec.row] += juce::jlimit (1, layout::unitsPerRow, spec.widthUnits);
    int most = layout::unitsPerRow;
    for (const auto& [row, n] : units)
        most = juce::jmax (most, n);
    return most;
}

int ModuleComponent::moduleWidth() const
{
    int w = defaultModuleWidth;
    if (auto* inst = processor.getInstance (instanceId))
        if (const int pw = inst->dsp->preferredModuleWidth(); pw > 0)
            w = pw;
    const int unitW = (defaultModuleWidth - 2 * layout::pad) / layout::unitsPerRow;
    return juce::jmax (w, 2 * layout::pad + maxRowUnits() * unitW);
}

bool ModuleComponent::isParamVisible (const ParamSpec& spec) const
{
    if (spec.visibleWhenParamId.isEmpty())
        return true;

    if (auto* inst = processor.getInstance (instanceId))
        return std::abs (inst->dsp->getParameter (spec.visibleWhenParamId) - spec.visibleWhenEquals) < 0.5f;

    return true;
}

void ModuleComponent::buildControls()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    const auto& desc = inst->descriptor();
    auto* dsp = inst->dsp.get();
    juce::WeakReference<SynthModule> weakDsp (dsp);

    for (const auto& spec : desc.params)
    {
        if (spec.hidden)
            continue;   // owned by the module's custom extra-content component

        ControlEntry entry;
        entry.spec = &spec;

        switch (spec.type)
        {
            case ParamType::Rotary:
            case ParamType::RotarySteppedList:
            {
                auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                              juce::Slider::TextBoxBelow);
                slider->setRange (spec.minValue, spec.maxValue, spec.interval);
                if (spec.logarithmic && spec.minValue > 0.0f)
                    slider->setSkewFactorFromMidPoint (std::sqrt ((double) spec.minValue * spec.maxValue));

                if (spec.type == ParamType::RotarySteppedList)
                {
                    const auto choices = spec.choices;
                    slider->textFromValueFunction = [choices] (double v)
                    {
                        return choices[juce::jlimit (0, choices.size() - 1, (int) std::round (v))];
                    };
                    slider->valueFromTextFunction = [choices] (const juce::String& t)
                    {
                        const auto trimmed = t.trim();
                        for (int i = 0; i < choices.size(); ++i)
                            if (choices[i].equalsIgnoreCase (trimmed))
                                return (double) i;
                        return t.getDoubleValue();   // typed a raw number instead
                    };
                }
                else
                {
                    if (spec.unitSuffix.isNotEmpty())
                        slider->setTextValueSuffix (" " + spec.unitSuffix);
                    const float range = spec.maxValue - spec.minValue;
                    int dp = spec.interval == 1.0f ? 0
                             : range > 1000.0f ? 0
                             : range > 20.0f ? 1 : 2;
                    if (spec.logarithmic && spec.minValue < 0.1f)
                        dp = 3;   // e.g. FM Ratio 0.001..100 needs fine resolution near the bottom
                    slider->setNumDecimalPlacesToDisplay (dp);

                    // frequencies: "1.80k" fits the box where "1800 Hz" did not
                    if (spec.unitSuffix == "Hz")
                    {
                        slider->setTextValueSuffix ({});
                        slider->textFromValueFunction = [] (double v)
                        {
                            if (v >= 10000.0) return juce::String (v / 1000.0, 1) + "k";
                            if (v >= 1000.0)  return juce::String (v / 1000.0, 2) + "k";
                            return juce::String (v, v < 10.0 ? 2 : (v < 100.0 ? 1 : 0)) + " Hz";
                        };
                        slider->valueFromTextFunction = [] (const juce::String& t)
                        {
                            const auto s = t.trim().toLowerCase();
                            const double x = s.getDoubleValue();
                            return s.containsChar ('k') ? x * 1000.0 : x;
                        };
                    }
                }

                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 17);
                slider->setDoubleClickReturnValue (true, spec.defaultValue);   // double-click resets
                slider->setValue (dsp->getParameter (spec.id), juce::dontSendNotification);

                // setValue() does not redraw the text when the value is unchanged,
                // and the box was filled in by setRange() before the choice names
                // existed - a stepped knob sitting on its first choice used to
                // read "0" instead of "-LIN"
                slider->updateText();

                // does any other parameter's visibility depend on this knob?
                // (e.g. Glide only exists while Voices == 1) - if so the knob
                // has to trigger a re-layout, exactly like a combo does
                bool drivesVisibility = false;
                for (const auto& other : desc.params)
                    if (other.visibleWhenParamId == spec.id)
                    {
                        drivesVisibility = true;
                        break;
                    }

                slider->onValueChange = [this, weakDsp, paramId = spec.id,
                                         s = slider.get(), drivesVisibility]
                {
                    if (auto* m = weakDsp.get())
                        m->setParameter (paramId, (float) s->getValue());
                    if (drivesVisibility)
                        refreshLayout();
                };
                entry.component = std::move (slider);
                break;
            }

            case ParamType::HBar:
            {
                auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                              juce::Slider::TextBoxRight);
                slider->setRange (spec.minValue, spec.maxValue, spec.interval);
                if (spec.unitSuffix.isNotEmpty())
                    slider->setTextValueSuffix (" " + spec.unitSuffix);
                if (spec.logarithmic && spec.minValue > 0.0f)
                    slider->setSkewFactorFromMidPoint (std::sqrt ((double) spec.minValue * spec.maxValue));
                const float hrange = spec.maxValue - spec.minValue;
                slider->setNumDecimalPlacesToDisplay (spec.interval == 1.0f || hrange > 1000.0f ? 0
                                                      : hrange > 20.0f ? 1 : 2);
                slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 16);
                slider->setDoubleClickReturnValue (true, spec.defaultValue);
                slider->setValue (dsp->getParameter (spec.id), juce::dontSendNotification);
                slider->onValueChange = [weakDsp, paramId = spec.id, sl = slider.get()]
                {
                    if (auto* m = weakDsp.get())
                        m->setParameter (paramId, (float) sl->getValue());
                };
                entry.component = std::move (slider);
                break;
            }

            case ParamType::Combo:
            {
                auto combo = std::make_unique<juce::ComboBox>();
                combo->addItemList (spec.choices, 1);
                combo->setSelectedItemIndex ((int) dsp->getParameter (spec.id), juce::dontSendNotification);
                combo->onChange = [this, weakDsp, paramId = spec.id, c = combo.get()]
                {
                    if (auto* m = weakDsp.get())
                        m->setParameter (paramId, (float) c->getSelectedItemIndex());
                    refreshLayout();   // a combo may drive another param's visibility
                };
                entry.component = std::move (combo);
                break;
            }

            case ParamType::Button:
            {
                auto button = std::make_unique<juce::TextButton> (spec.label);
                button->onClick = [weakDsp, paramId = spec.id,
                                   safeThis = juce::Component::SafePointer<ModuleComponent> (this)]
                {
                    if (auto* m = weakDsp.get())
                        m->uiButtonClicked (paramId);

                    // a button may set the module's own knobs (Rand Tone)
                    if (safeThis != nullptr)
                        safeThis->refreshFromModel();
                };
                entry.component = std::move (button);
                break;
            }
        }

        // right-clicks on modulatable knobs open the modulation menu
        if ((spec.type == ParamType::Rotary || spec.type == ParamType::RotarySteppedList
             || spec.type == ParamType::HBar) && spec.modulatable)
            entry.component->addMouseListener (this, false);
        addAndMakeVisible (entry.component.get());
        controls.push_back (std::move (entry));
    }

    extraContent = dsp->createExtraContentComponent();
    if (extraContent != nullptr)
    {
        addAndMakeVisible (extraContent.get());

        // custom cable targets (e.g. Pitch Lock Filter keys) need their mouse
        // events mirrored here so right-click opens the modulation menu and
        // rapid clicks count toward the self-cable delete gesture
        if (dynamic_cast<CustomParamCableTargets*> (extraContent.get()) != nullptr)
            extraContent->addMouseListener (this, false);

        // custom UIs that set ordinary params themselves (Piano Roll's editor)
        // ask for the generic knobs to be refreshed afterwards
        if (auto* listener = dynamic_cast<ExtraContentParamListener*> (extraContent.get()))
        {
            listener->paramsChangedByContent = [this] { refreshFromModel(); };
            listener->paramValuesChangedByContent = [this] { refreshControlValues(); };
        }
    }
}

int ModuleComponent::layoutEverything (bool apply)
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return 40;

    const auto& desc = inst->descriptor();
    const int innerW = moduleWidth() - 2 * layout::pad;
    int y = layout::pad;

    if (apply)
        sockets.clear();

    // ---- 3-column header: input column (left) | title (centre) | output column (right)
    const auto inList = desc.inputs();
    const auto outList = desc.outputs();
    const int headerRows = juce::jmax ((int) inList.size(), (int) outList.size());
    const int headerH = juce::jmax (layout::headerMinH, headerRows * layout::socketSlotH + layout::pad);

    if (apply)
    {
        const int leftX = layout::pad + layout::socketSize / 2 + 2;
        const int rightX = moduleWidth() - layout::pad - layout::socketSize / 2 - 2;

        auto placeColumn = [&] (const std::vector<const SocketSpec*>& list, bool isInput, int cx)
        {
            const int n = (int) list.size();
            for (int i = 0; i < n; ++i)
            {
                const int slotTop = y + (headerH - n * layout::socketSlotH) / 2 + i * layout::socketSlotH;
                SocketRef ref;
                ref.socketId = list[(size_t) i]->id;
                ref.isInput = isInput;
                ref.kind = list[(size_t) i]->kind;
                ref.centre = { cx, slotTop + layout::socketSlotH / 2 };
                sockets.push_back (ref);
            }
        };

        placeColumn (inList, true, leftX);
        placeColumn (outList, false, rightX);
    }

    headerHeight = headerH;
    y += headerH + 2;

    // Collapsed: stop at the header. Every control is hidden (so it cannot be
    // clicked through the shrunken rectangle) but nothing is destroyed, and
    // the sockets placed above stay exactly where they were relative to the
    // header - so audio cables neither move nor break.
    if (isCollapsed())
    {
        if (apply)
        {
            for (auto& c : controls)
            {
                c.visible = false;
                c.component->setVisible (false);
            }
            if (extraContent != nullptr)
                extraContent->setVisible (false);
        }
        return y + layout::pad - 4;
    }

    if (apply && extraContent != nullptr)
        extraContent->setVisible (true);

    // 4. knob rows per explicit descriptor row/widthUnits (max 5 units per row)
    int currentRow = -1;
    bool first = true;
    for (auto rowIt = 0;; ++rowIt)
    {
        // gather visible params of the next existing row index
        int nextRow = std::numeric_limits<int>::max();
        for (auto& c : controls)
            if (isParamVisible (*c.spec) && c.spec->row > currentRow)
                nextRow = juce::jmin (nextRow, c.spec->row);

        if (nextRow == std::numeric_limits<int>::max())
            break;

        currentRow = nextRow;
        juce::ignoreUnused (rowIt, first);

        std::vector<ControlEntry*> rowControls;
        bool hasKnob = false, hasBar = false, hasTitledCombo = false;
        for (auto& c : controls)
        {
            const bool visible = isParamVisible (*c.spec);
            if (c.spec->row == currentRow)
            {
                if (apply)
                {
                    c.visible = visible;
                    c.component->setVisible (visible);
                }
                if (visible)
                {
                    rowControls.push_back (&c);
                    if (c.spec->type == ParamType::Rotary || c.spec->type == ParamType::RotarySteppedList)
                        hasKnob = true;
                    else if (c.spec->type == ParamType::HBar)
                        hasBar = true;
                    else if (c.spec->type == ParamType::Combo && c.spec->label.isNotEmpty())
                        hasTitledCombo = true;
                }
            }
        }

        if (rowControls.empty())
            continue;

        // a row of horizontal bars only needs a label and the bar itself
        // a list (combo box) carries its name above it like a knob does, so a
        // row of lists only grows by that title
        const int rowH = hasKnob ? layout::knobRowH : hasBar ? layout::barRowH
                       : layout::wideRowH + (hasTitledCombo ? 14 : 0);
        const int unitW = innerW / maxRowUnits();

        if (apply)
        {
            int x = layout::pad;
            for (auto* c : rowControls)
            {
                const int w = unitW * juce::jlimit (1, layout::unitsPerRow, c->spec->widthUnits);
                auto area = juce::Rectangle<int> (x, y, w, rowH);

                if (c->spec->type == ParamType::Rotary || c->spec->type == ParamType::RotarySteppedList
                    || c->spec->type == ParamType::HBar)
                {
                    c->labelArea = area.removeFromTop (14);
                    c->component->setBounds (area.reduced (1));
                }
                else if (c->spec->type == ParamType::Combo && c->spec->label.isNotEmpty())
                {
                    // the list's name on top, in line with the knobs' names
                    c->labelArea = area.removeFromTop (14);
                    c->component->setBounds (area.withSizeKeepingCentre (w - 6, 24)
                                                 .withY (hasKnob ? y + (rowH - 24) / 2 + 6 : y + 16));
                }
                else
                {
                    c->labelArea = {};
                    c->component->setBounds (area.withSizeKeepingCentre (w - 6, 24)
                                                 .withY (y + (rowH - 24) / 2 + (hasKnob ? 6 : 0)));
                }
                x += w;
            }
        }
        y += rowH + 2;
    }

    // hide params of rows we never visited (fully hidden rows)
    if (apply)
        for (auto& c : controls)
            if (! isParamVisible (*c.spec))
            {
                c.visible = false;
                c.component->setVisible (false);
            }

    // 5. optional custom content (Sampler waveform display)
    if (auto* dsp = inst->dsp.get(); dsp != nullptr && extraContent != nullptr)
    {
        const int h = dsp->extraContentHeight();
        if (apply)
            extraContent->setBounds (layout::pad, y, innerW, h);
        y += h + 4;
    }

    return y + layout::pad - 2;
}

bool ModuleComponent::showsConvertPill() const
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return false;
    auto* hosted = dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get());
    if (hosted == nullptr)
        return false;
    auto* entry = aquanode::plugins::findEntry (hosted->getSpec().pluginId);
    return entry != nullptr && entry->hasPatchVersion();
}

juce::Rectangle<int> ModuleComponent::convertPillArea() const
{
    // right of the centred title, clear of the output socket column
    return { getWidth() - layout::socketColW - 96, layout::pad + 4, 84, 22 };
}

bool ModuleComponent::isCollapsed() const
{
    if (auto* inst = processor.getInstance (instanceId))
        return inst->collapsed;
    return false;
}

void ModuleComponent::toggleCollapsed()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    inst->collapsed = ! inst->collapsed;
    refreshLayout();                  // re-measures and resizes the rectangle
    canvas.moduleCollapsedChanged();  // param cables to hidden knobs stop drawing
}

juce::Rectangle<int> ModuleComponent::collapseTabArea() const
{
    // centred in the top margin, above the title
    return juce::Rectangle<int> (moduleWidth() / 2 - 15, 3, 30, 6);
}

juce::Rectangle<int> ModuleComponent::collapseHitArea() const
{
    // generous enough for a fingertip, but confined to the top margin so it
    // never swallows a press meant for the title area or a socket
    return collapseTabArea().withSizeKeepingCentre (54, 18).withY (0);
}

bool ModuleComponent::isKnobVisible (const juce::String& paramId) const
{
    if (isCollapsed())
        return false;

    for (const auto& entry : controls)
        if (entry.spec->id == paramId)
            return entry.visible;

    // hidden cable-target params live inside the custom UI
    if (extraContent != nullptr && extraContent->isVisible())
        if (dynamic_cast<const CustomParamCableTargets*> (extraContent.get()) != nullptr)
            return true;

    return true;
}

void ModuleComponent::refreshLayout()
{
    const int h = layoutEverything (false);
    setSize (moduleWidth(), h);
    layoutEverything (true);
    repaint();
    canvas.moduleMoved();
}

void ModuleComponent::refreshControlValues()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    for (auto& c : controls)
    {
        const float v = inst->dsp->getParameter (c.spec->id);
        if (auto* slider = dynamic_cast<juce::Slider*> (c.component.get()))
        {
            if (! slider->isMouseButtonDown())
                slider->setValue (v, juce::dontSendNotification);
        }
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (c.component.get()))
            combo->setSelectedItemIndex ((int) v, juce::dontSendNotification);
    }
}

void ModuleComponent::refreshFromModel()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    for (auto& c : controls)
    {
        const float v = inst->dsp->getParameter (c.spec->id);
        if (auto* slider = dynamic_cast<juce::Slider*> (c.component.get()))
            slider->setValue (v, juce::dontSendNotification);
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (c.component.get()))
            combo->setSelectedItemIndex ((int) v, juce::dontSendNotification);
    }

    refreshLayout();   // a mutated combo may change another param's visibility
}

const ModuleComponent::SocketRef* ModuleComponent::findSocketNear (juce::Point<int> localPos, int radius) const
{
    for (const auto& s : sockets)
        if (s.centre.getDistanceFrom (localPos) <= radius)
            return &s;
    return nullptr;
}

juce::Point<int> ModuleComponent::socketCentreInParent (const juce::String& socketId, bool isInput) const
{
    for (const auto& s : sockets)
        if (s.socketId == socketId && s.isInput == isInput)
            return getPosition() + s.centre;
    return getBounds().getCentre();
}

void ModuleComponent::paint (juce::Graphics& g)
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    const auto& desc = inst->descriptor();
    const auto body = desc.colour();
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (body);
    g.fillRoundedRectangle (bounds.reduced (1.0f), 8.0f);

    const bool selected = canvas.getSelectedModuleId() == instanceId;
    g.setColour (selected ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, selected ? 2.5f : 1.2f);

    // divider under the header, separating socket/title band from knob rows
    const float divY = (float) (layout::pad + headerHeight + 1);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.drawLine (bounds.getX() + 6.0f, divY, bounds.getRight() - 6.0f, divY, 1.0f);

    // collapse tab: a flat bar when open, a raised "grabber" split in two when
    // collapsed, so the state reads at a glance from across a zoomed-out patch
    {
        const auto tab = collapseTabArea().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        if (isCollapsed())
        {
            g.fillRoundedRectangle (tab.withWidth (tab.getWidth() * 0.45f), 2.5f);
            g.fillRoundedRectangle (tab.withWidth (tab.getWidth() * 0.45f)
                                       .withX (tab.getRight() - tab.getWidth() * 0.45f), 2.5f);
        }
        else
        {
            g.fillRoundedRectangle (tab, 2.5f);
        }
    }

    // module title, centred in the header column between the socket columns
    g.setColour (juce::Colours::black);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    // longer names ("Chord Wavetable", "3-Band Compressor") wrap onto a
    // second line rather than being cut short between the socket columns
    g.drawFittedText (desc.displayName + inst->dsp->titleSuffix(),
                      layout::socketColW, layout::pad,
                      getWidth() - 2 * layout::socketColW, headerHeight,
                      juce::Justification::centred, 2, 0.9f);

    if (showsConvertPill())
    {
        const auto r = convertPillArea().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("To Patch", convertPillArea(), juce::Justification::centred);
    }

    // sockets: circular = Audio, square = Modulation, round = MIDI
    for (const auto& s : sockets)
    {
        const auto c = s.centre.toFloat();
        const float r = layout::socketSize * 0.5f;

        juce::Path shape;
        if (s.kind == SocketKind::Audio)
            shape.addEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        else if (s.kind == SocketKind::Midi)
            shape.addRoundedRectangle(c.x - r, c.y - r, 2 * r, 2 * r, r * 0.5f);
        else
            shape.addRectangle (c.x - r, c.y - r, 2 * r, 2 * r);

        g.setColour (s.kind == SocketKind::Midi ? juce::Colour (0xff3a1730) : juce::Colours::black);
        g.fillPath (shape);
        g.setColour (s.kind == SocketKind::Midi ? juce::Colour (0xff00ffff) : juce::Colours::white);
        g.strokePath (shape, juce::PathStrokeType (1.3f));

        const SocketSpec* spec = nullptr;
        for (const auto& sk : desc.sockets)
            if (sk.id == s.socketId && (sk.direction == SocketDirection::Input) == s.isInput)
                spec = &sk;

        if (spec != nullptr)
        {
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            if (s.isInput)   // input labels sit to the right of the left column
                g.drawText (spec->label, s.centre.x + (int) r + 3, s.centre.y - 7,
                            layout::socketColW - (int) r - 4, 14, juce::Justification::centredLeft);
            else             // output labels sit to the left of the right column
                g.drawText (spec->label, s.centre.x - (int) r - 3 - (layout::socketColW - (int) r - 4),
                            s.centre.y - 7, layout::socketColW - (int) r - 4, 14,
                            juce::Justification::centredRight);
        }
    }

    // knob labels
    g.setColour (juce::Colours::black);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    for (const auto& c : controls)
        if (c.visible && ! c.labelArea.isEmpty())
            g.drawText (c.spec->label, c.labelArea, juce::Justification::centred);
}


const aquanode::ParamSpec* ModuleComponent::findModulatableKnobNear (juce::Point<int> localPos) const
{
    for (const auto& entry : controls)
    {
        if (! entry.visible || (entry.spec->type != aquanode::ParamType::Rotary
                                && entry.spec->type != aquanode::ParamType::RotarySteppedList
                                && entry.spec->type != aquanode::ParamType::HBar)
            || ! entry.spec->modulatable || entry.component == nullptr)
            continue;
        if (entry.component->getBounds().expanded (2).contains (localPos))
            return entry.spec;
    }

    // custom drop targets inside the extra-content component (e.g. Pitch Lock
    // Filter keys): each maps to a hidden parameter flagged hiddenCableTarget
    if (extraContent != nullptr && extraContent->isVisible()
        && extraContent->getBounds().contains (localPos))
    {
        if (auto* targets = dynamic_cast<const CustomParamCableTargets*> (extraContent.get()))
        {
            const auto id = targets->paramTargetAt (localPos - extraContent->getPosition());
            if (id.isNotEmpty())
                if (auto* inst = processor.getInstance (instanceId))
                    if (const auto* spec = inst->descriptor().findParam (id))
                        if ((spec->type == aquanode::ParamType::Rotary
                             || spec->type == aquanode::ParamType::HBar)
                            && spec->modulatable && spec->hiddenCableTarget)
                            return spec;
        }
    }

    return nullptr;
}

juce::Point<int> ModuleComponent::knobCentreInParent (const juce::String& paramId) const
{
    for (const auto& entry : controls)
        if (entry.spec->id == paramId && entry.component != nullptr)
            return getPosition() + entry.component->getBounds().getCentre();

    // hidden cable-target params attach at the point the custom UI reports
    // (a Pitch Lock Filter cable snaps to the centre of its key)
    if (extraContent != nullptr)
        if (auto* targets = dynamic_cast<const CustomParamCableTargets*> (extraContent.get()))
        {
            const auto centre = targets->paramTargetCentre (paramId);
            if (centre.x >= 0)
                return getPosition() + extraContent->getPosition() + centre;
        }

    return getPosition() + juce::Point<int> (moduleWidth() / 2, headerHeight / 2);
}

void ModuleComponent::showKnobModMenu (const juce::String& paramId)
{
    const auto& pcs = processor.getParamCables();
    juce::PopupMenu menu;
    bool any = false;

    // the menu is async: the patch may be reloaded (and this component
    // destroyed) before a choice is made, so callbacks go through a
    // SafePointer and re-find the cable by identity instead of by index
    juce::Component::SafePointer<ModuleComponent> safeThis (this);

    for (int i = 0; i < (int) pcs.size(); ++i)
    {
        if (pcs[(size_t) i].toModule != instanceId || pcs[(size_t) i].paramId != paramId)
            continue;
        any = true;

        juce::String srcName = "module " + juce::String (pcs[(size_t) i].fromModule);
        if (auto* srcInst = processor.getInstance (pcs[(size_t) i].fromModule))
            srcName = srcInst->descriptor().displayName + " / " + pcs[(size_t) i].fromSocket;

        // identify the cable by its contents, not its index: indices shift
        // whenever any other cable is added or removed
        const auto key = pcs[(size_t) i];
        auto findIndex = [safeThis, key]() -> int
        {
            if (safeThis == nullptr)
                return -1;
            const auto& live = safeThis->processor.getParamCables();
            for (int k = 0; k < (int) live.size(); ++k)
                if (live[(size_t) k].fromModule == key.fromModule
                    && live[(size_t) k].fromSocket == key.fromSocket
                    && live[(size_t) k].toModule == key.toModule
                    && live[(size_t) k].paramId == key.paramId)
                    return k;
            return -1;
        };

        juce::PopupMenu sub;
        static const float depths[] = { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f };
        for (int d = 0; d < 8; ++d)
        {
            const float depth = depths[d];
            const bool current = std::abs (key.depth - depth) < 0.011f;
            sub.addItem (juce::String ((int) (depth * 100.0f)) + " %", true, current,
                         [safeThis, findIndex, depth]
                         {
                             const int k = findIndex();
                             if (safeThis != nullptr && k >= 0)
                                 safeThis->processor.setParamCableDepth (k, depth);
                         });
        }
        sub.addSeparator();
        sub.addItem ("Remove", [safeThis, findIndex]
        {
            const int k = findIndex();
            if (safeThis != nullptr && k >= 0)
            {
                safeThis->processor.removeParamCable (k);
                safeThis->canvas.repaint();
            }
        });

        menu.addSubMenu (srcName + "  (" + juce::String ((int) (key.depth * 100.0f)) + " %)", sub);
    }

    if (any)
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

bool ModuleComponent::knobHasModulation (const juce::String& paramId) const
{
    for (const auto& pc : processor.getParamCables())
        if (pc.toModule == instanceId && pc.paramId == paramId)
            return true;
    return false;
}

void ModuleComponent::paintOverChildren (juce::Graphics& g)
{
    if (isCollapsed())
        return;   // the knobs the rings belong to are hidden

    // ring around every modulated knob
    const auto& pcs = processor.getParamCables();
    for (const auto& entry : controls)
    {
        if (! entry.visible || (entry.spec->type != aquanode::ParamType::Rotary
                                && entry.spec->type != aquanode::ParamType::RotarySteppedList
                                && entry.spec->type != aquanode::ParamType::HBar))
            continue;

        bool modulated = false;
        for (const auto& pc : pcs)
            if (pc.toModule == instanceId && pc.paramId == entry.spec->id)
            {
                modulated = true;
                break;
            }

        if (modulated && entry.component != nullptr)
        {
            auto b = entry.component->getBounds().toFloat();
            const float d = juce::jmin (b.getWidth(), b.getHeight()) * 0.95f;
            juce::Rectangle<float> ring (b.getCentreX() - d * 0.5f,
                                         b.getCentreY() - d * 0.5f - 6.0f, d, d);
            g.setColour (juce::Colour (0xffd970b0).withAlpha (0.85f));
            g.drawEllipse (ring, 1.6f);
        }
    }
}

bool ModuleComponent::handleRapidClick (const juce::MouseEvent& e)
{
    const juce::int64 now = e.eventTime.toMilliseconds();

    // the same physical click can arrive twice (own handler + child mouse
    // listener) - only count each event once
    if (now == lastClickTimeMs && e.getScreenPosition() == lastClickScreenPos)
        return false;

    const bool continues = rapidClickCount > 0
        && now - lastClickTimeMs <= 500
        && e.getScreenPosition().getDistanceFrom (lastClickScreenPos) <= 4;

    rapidClickCount = continues ? rapidClickCount + 1 : 1;
    lastClickTimeMs = now;
    lastClickScreenPos = e.getScreenPosition();

    if (rapidClickCount < 3)
        return false;

    rapidClickCount = 0;

    // delete every cable this module routes back into itself (self-modulation
    // loops); cables to or from OTHER modules are deliberately left alone
    const auto& cables = processor.getCables();
    std::vector<int> selfCables;
    for (int i = 0; i < (int) cables.size(); ++i)
        if (cables[(size_t) i].fromModule == instanceId
            && cables[(size_t) i].toModule == instanceId)
            selfCables.push_back (i);

    if (selfCables.empty())
        return false;

    for (auto it = selfCables.rbegin(); it != selfCables.rend(); ++it)
        processor.removeCable (*it);

    canvas.repaint();
    return true;
}

void ModuleComponent::mouseDown (const juce::MouseEvent& e)
{
    canvas.selectModule (instanceId);

    // The collapse tab is checked first, before the rapid-click and drag
    // gestures: tapping it repeatedly must open and close the module rather
    // than read as a triple-click that deletes its self-modulation cables.
    if (e.eventComponent == this && ! e.mods.isPopupMenu()
        && collapseHitArea().contains (e.getPosition()))
    {
        rapidClickCount = 0;
        toggleCollapsed();
        return;
    }

    // a plugin's GUI card: the To Patch pill swaps it for its patch version
    if (e.eventComponent == this && ! e.mods.isPopupMenu() && showsConvertPill()
        && convertPillArea().contains (e.getPosition()))
    {
        canvas.convertPluginCard (instanceId);
        return;
    }

    // three rapid clicks (same spot, +-4 px) anywhere in the module delete
    // its self-modulation cables - the one cable type the canvas can't select
    if (! e.mods.isPopupMenu() && handleRapidClick (e))
        return;

    // right-click on a modulated knob (event may arrive via the knob's own
    // mouse listener, so translate into our coordinate space first)
    const auto knobPos = e.eventComponent == this
        ? e.getPosition()
        : e.getEventRelativeTo (this).getPosition();

    if (e.mods.isPopupMenu())
    {
        if (const auto* spec = findModulatableKnobNear (knobPos))
        {
            showKnobModMenu (spec->id);
            return;
        }
    }

    // Touch devices have no right-click, so a long press stands in for it.
    // This is deliberately TOUCH ONLY: on desktop, resting on a knob before a
    // slow fine-tune is a completely normal gesture and must never pop a
    // menu. It also only arms on knobs that actually have a modulation cable,
    // since the menu would be empty otherwise.
    longPressParamId.clear();
    longPressMoved = false;

    if (e.source.isTouch())
    {
        if (const auto* spec = findModulatableKnobNear (knobPos))
        {
            if (knobHasModulation (spec->id))
            {
                longPressParamId = spec->id;

                juce::Component::SafePointer<ModuleComponent> safeThis (this);
                const auto id = spec->id;
                juce::Timer::callAfterDelay (500, [safeThis, id]
                {
                    // still the same un-moved press? (a drag cancels it)
                    if (safeThis != nullptr && ! safeThis->longPressMoved
                        && safeThis->longPressParamId == id)
                        safeThis->showKnobModMenu (id);
                });
            }
        }
    }

    if (e.eventComponent != this)
        return;   // child listener event: knobs handle their own left-clicks

    if (const auto* socket = findSocketNear (e.getPosition()))
    {
        draggingCable = true;
        canvas.beginCableDrag (instanceId, socket->socketId, socket->isInput, socket->kind,
                               getPosition() + socket->centre);
        return;
    }

    draggingBody = true;
    dragger.startDraggingComponent (this, e);
}

void ModuleComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (longPressParamId.isNotEmpty() && e.getDistanceFromDragStart() > 8)
        longPressMoved = true;   // this is a knob drag, not a long press

    if (draggingCable)
    {
        canvas.updateCableDrag (e.getEventRelativeTo (&canvas).getPosition());
        return;
    }

    if (draggingBody)
    {
        dragger.dragComponent (this, e, nullptr);
        // getPosition() is already the world position: the view transform is
        // applied on top of the bounds, not baked into them.
        processor.setModulePosition (instanceId, getPosition());
        canvas.moduleMoved();
    }
}

void ModuleComponent::mouseUp (const juce::MouseEvent& e)
{
    longPressParamId.clear();   // press finished: nothing left to fire

    if (draggingCable)
    {
        canvas.endCableDrag (e.getEventRelativeTo (&canvas).getPosition());
        draggingCable = false;
    }
    draggingBody = false;
}

//==============================================================================
// PatchCanvas
//==============================================================================
PatchCanvas::PatchCanvas (AquanodeModularAudioProcessor& proc)
    : processor (proc)
{
    setWantsKeyboardFocus (true);
}

// Screen-space spacing of the "+" grid at the current zoom. The world spacing
// is doubled as often as needed to keep the on-screen spacing near its usual
// value: without that floor, zooming out to 30 % would quadruple the number of
// crosses drawn on every pan frame, which is exactly when a phone can least
// afford it. Visually the grid coarsens as you pull back, which is what a
// map is expected to do anyway.
float PatchCanvas::gridScreenSpacing() const
{
    float s = (float) layout::gridSpacing * zoom;
    while (s < (float) layout::gridSpacing * 0.75f)
        s *= 2.0f;
    return s;
}

void PatchCanvas::rebuildGrid()
{
    // decorative "+" grid. Extend one cell beyond every edge so it can be
    // translated by the pan offset (mod spacing) and still cover the viewport.
    const float s = gridScreenSpacing();
    const float arm = juce::jlimit (2.0f, 3.0f, 3.0f * zoom);

    gridPath.clear();
    for (float x = 0.0f; x <= (float) getWidth() + s; x += s)
    {
        for (float y = 0.0f; y <= (float) getHeight() + s; y += s)
        {
            gridPath.startNewSubPath (x - arm, y);
            gridPath.lineTo (x + arm, y);
            gridPath.startNewSubPath (x, y - arm);
            gridPath.lineTo (x, y + arm);
        }
    }
}

void PatchCanvas::resized()
{
    rebuildGrid();
}

void PatchCanvas::convertPluginCard (int instanceId)
{
    juce::Component::SafePointer<PatchCanvas> safe (this);
    juce::MessageManager::callAsync ([safe, instanceId]
    {
        if (safe == nullptr)
            return;
        const auto name = aquanode::plugins::convertGuiToPatch (safe->processor, instanceId);
        if (name.isNotEmpty())
            safe->processor.patchName = name;
        safe->rebuild();
    });
}

void PatchCanvas::rebuild()
{
    moduleComponents.clear();
    selectedCableIndex = -1;

    bool selectedStillExists = false;
    for (const int id : processor.getModuleIds())
    {
        auto comp = std::make_unique<ModuleComponent> (processor, *this, id);
        addAndMakeVisible (comp.get());
        moduleComponents.push_back (std::move (comp));
        if (id == selectedModuleId)
            selectedStillExists = true;
    }

    if (! selectedStillExists)
        selectedModuleId = -1;

    applyViewToChildren();   // new components need the current pan/zoom
    repaint();
}

void PatchCanvas::refreshAllModuleValues()
{
    for (auto& m : moduleComponents)
        m->refreshFromModel();
    repaint();
}

ModuleComponent* PatchCanvas::findModuleComponent (int instanceId) const
{
    for (const auto& m : moduleComponents)
        if (m->getInstanceId() == instanceId)
            return m.get();
    return nullptr;
}

void PatchCanvas::selectModule (int instanceId)
{
    selectedModuleId = instanceId;
    selectedCableIndex = -1;
    repaint();
}

void PatchCanvas::moduleMoved()
{
    repaint();
}

juce::Path PatchCanvas::cablePath (juce::Point<float> from, juce::Point<float> to) const
{
    juce::Path p;
    const float sag = 30.0f + from.getDistanceFrom (to) * 0.15f;
    p.startNewSubPath (from);
    p.cubicTo (from.translated (0.0f, sag), to.translated (0.0f, sag), to);
    return p;
}

bool PatchCanvas::getCableEndpoints (const CableInfo& c, juce::Point<float>& from, juce::Point<float>& to) const
{
    auto* fromComp = findModuleComponent (c.fromModule);
    auto* toComp = findModuleComponent (c.toModule);
    if (fromComp == nullptr || toComp == nullptr)
        return false;

    from = fromComp->socketCentreInParent (c.fromSocket, false).toFloat();
    to = toComp->socketCentreInParent (c.toSocket, true).toFloat();
    return true;
}

int PatchCanvas::cableIndexNear (juce::Point<float> pos, float maxDistance) const
{
    // pos is a world point; the tolerance is quoted in screen pixels, so it has
    // to grow as the world shrinks or cables become unselectable when zoomed out
    maxDistance /= juce::jmax (0.01f, zoom);

    const auto& cables = processor.getCables();
    for (int i = 0; i < (int) cables.size(); ++i)
    {
        juce::Point<float> from, to;
        if (! getCableEndpoints (cables[(size_t) i], from, to))
            continue;

        juce::Point<float> nearest;
        cablePath (from, to).getNearestPoint (pos, nearest);
        if (nearest.getDistanceFrom (pos) <= maxDistance)
            return i;
    }
    return -1;
}

int PatchCanvas::paramCableIndexNear (juce::Point<float> pos, float maxDistance) const
{
    maxDistance /= juce::jmax (0.01f, zoom);

    const auto& pcs = processor.getParamCables();
    for (int i = 0; i < (int) pcs.size(); ++i)
    {
        auto* srcComp = findModuleComponent (pcs[(size_t) i].fromModule);
        auto* dstComp = findModuleComponent (pcs[(size_t) i].toModule);
        if (srcComp == nullptr || dstComp == nullptr)
            continue;

        if (! dstComp->isKnobVisible (pcs[(size_t) i].paramId))
            continue;   // not drawn, so it must not be selectable either

        const auto from = srcComp->socketCentreInParent (pcs[(size_t) i].fromSocket, false).toFloat();
        const auto to = dstComp->knobCentreInParent (pcs[(size_t) i].paramId).toFloat();

        juce::Point<float> nearest;
        cablePath (from, to).getNearestPoint (pos, nearest);
        if (nearest.getDistanceFrom (pos) <= maxDistance)
            return i;
    }
    return -1;
}

void PatchCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2723));            // warm dark brown-grey patch map
    g.setColour (juce::Colour (0xff3c3730));          // subtle warmer "+" grid

    const float s = gridScreenSpacing();
    const float ox = std::fmod (std::fmod ((float) panOffset.x, s) + s, s) - s;
    const float oy = std::fmod (std::fmod ((float) panOffset.y, s) + s, s) - s;
    g.strokePath (gridPath, juce::PathStrokeType (1.0f),
                  juce::AffineTransform::translation (ox, oy));
}

void PatchCanvas::paintOverChildren (juce::Graphics& g)
{
    // Everything below is drawn in WORLD coordinates - the same coordinates the
    // module bounds are in - so the existing endpoint maths needs no change at
    // all: one transform puts the whole cable layer in the right place at the
    // right size. Stroke widths scale with it, which is what we want: cables
    // thin out as you zoom away instead of turning into a solid mat.
    juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (worldToScreen());

    // knob-modulation cables: dashed, source socket -> target knob
    {
        const auto& pcs = processor.getParamCables();
        for (int i = 0; i < (int) pcs.size(); ++i)
        {
            const auto& pc = pcs[(size_t) i];
            auto* srcComp = findModuleComponent (pc.fromModule);
            auto* dstComp = findModuleComponent (pc.toModule);
            if (srcComp == nullptr || dstComp == nullptr)
                continue;

            // target knob is hidden (collapsed module, or a visibleWhen rule):
            // the cable still exists and still modulates, it simply has nowhere
            // sensible to land, so it waits until the knob is on screen again
            if (! dstComp->isKnobVisible (pc.paramId))
                continue;

            const auto from = srcComp->socketCentreInParent (pc.fromSocket, false).toFloat();
            const auto to = dstComp->knobCentreInParent (pc.paramId).toFloat();

            auto path = cablePath (from, to);
            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.8f).createDashedStroke (dashed, path, dashes, 2);
            g.setColour (i == selectedParamCableIndex ? juce::Colours::white
                                                      : juce::Colour (0xffd970b0).withAlpha (0.8f));
            g.fillPath (dashed);
            g.fillEllipse (to.x - 3.0f, to.y - 3.0f, 6.0f, 6.0f);
        }
    }

    const auto& cables = processor.getCables();

    for (int i = 0; i < (int) cables.size(); ++i)
    {
        const auto& c = cables[(size_t) i];

        juce::Point<float> from, to;
        if (! getCableEndpoints (c, from, to))
            continue;

        auto* src = processor.getInstance (c.fromModule);
        auto* dst = processor.getInstance (c.toModule);
        if (src == nullptr || dst == nullptr)
            continue;

        // cable colour always follows the source module's card colour
        auto colour = src->descriptor().colour();
        if (i == selectedCableIndex)
            colour = juce::Colours::white;

        const int sockIdx = src->descriptor().outputIndexOf (c.fromSocket);
        const bool isAudio = sockIdx >= 0
            && src->descriptor().outputs()[(size_t) sockIdx]->kind == SocketKind::Audio;

        const auto path = cablePath (from, to);
        g.setColour (colour.withAlpha (0.92f));

        if (isAudio)
        {
            g.strokePath (path, juce::PathStrokeType (3.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            juce::Path dashed;
            const float dashes[] = { 6.0f, 5.0f };
            juce::PathStrokeType (1.8f).createDashedStroke (dashed, path, dashes, 2);
            g.fillPath (dashed);
        }
    }

    if (dragActive)
    {
        ModuleComponent* comp = findModuleComponent (dragModuleId);
        if (comp != nullptr)
        {
            const auto anchor = comp->socketCentreInParent (dragSocketId, dragFromInput).toFloat();
            const auto pos = dragCurrentPos.toFloat();
            const auto path = dragFromInput ? cablePath (pos, anchor) : cablePath (anchor, pos);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
    }
}

//==============================================================================
// Two-finger pinch on empty canvas: zoom about the fingers' midpoint, and
// move the view with it. (Android delivers no magnify gesture, only the
// separate touches.)
bool PatchCanvas::pinchTouch (const juce::MouseEvent& e, bool down, bool up)
{
    if (! e.source.isTouch())
        return false;

    const int index = e.source.getIndex();
    auto it = std::find_if (touches.begin(), touches.end(), [index] (const Touch& t) { return t.index == index; });

    if (up)
    {
        if (it != touches.end())
            touches.erase (it);
        const bool was = pinching;
        if (touches.size() < 2)
            pinching = false;
        return was;
    }
    if (down)
    {
        if (it == touches.end())
            touches.push_back ({ index, e.position });
        if (touches.size() == 2 && ! pinching)
        {
            pinching = true;
            panning = false;
            pinchStartZoom = zoom;
            pinchStartPan = panOffset;
            pinchStartDist = juce::jmax (1.0f, touches[0].pos.getDistanceFrom (touches[1].pos));
            pinchStartMid = (touches[0].pos + touches[1].pos) * 0.5f;
        }
        return pinching;
    }
    if (it != touches.end())
        it->pos = e.position;
    if (! pinching || touches.size() < 2)
        return pinching;

    const float dist = juce::jmax (1.0f, touches[0].pos.getDistanceFrom (touches[1].pos));
    const auto mid = (touches[0].pos + touches[1].pos) * 0.5f;
    const float newZoom = juce::jlimit (minZoom, maxZoom, pinchStartZoom * dist / pinchStartDist);

    // the world point that was under the fingers' midpoint stays under it
    const juce::Point<float> anchor { (pinchStartMid.x - (float) pinchStartPan.x) / pinchStartZoom,
                                      (pinchStartMid.y - (float) pinchStartPan.y) / pinchStartZoom };
    const bool zoomChanged = std::abs (newZoom - zoom) > 1.0e-4f;
    zoom = newZoom;
    panOffset = juce::Point<int> (juce::roundToInt (mid.x - anchor.x * zoom), juce::roundToInt (mid.y - anchor.y * zoom));
    applyViewToChildren();
    if (zoomChanged)
    {
        rebuildGrid();
        if (auto* parent = getParentComponent())
            parent->resized();   // refresh the zoom readout
    }
    repaint();
    return true;
}

void PatchCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (pinchTouch (e, true, false))
        return;

    grabKeyboardFocus();

    const auto world = screenToWorld (e.position);
    const int hit = cableIndexNear (world);
    selectedCableIndex = hit;
    selectedParamCableIndex = hit >= 0 ? -1 : paramCableIndexNear (world);
    selectedModuleId = -1;

    // pressing empty space (not on any cable) starts panning the view
    panning = (hit < 0 && selectedParamCableIndex < 0);
    panStartOffset = panOffset;
    panMouseStart = e.getPosition();

    setMouseCursor (panning ? juce::MouseCursor::DraggingHandCursor
                            : juce::MouseCursor::NormalCursor);
    repaint();
}

void PatchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (pinchTouch (e, false, false) || ! panning)
        return;

    panOffset = panStartOffset + (e.getPosition() - panMouseStart);
    applyViewToChildren();
    repaint();
}

void PatchCanvas::mouseUp (const juce::MouseEvent& e)
{
    pinchTouch (e, false, true);
    panning = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PatchCanvas::applyViewToChildren()
{
    const auto t = worldToScreen();

    for (auto& m : moduleComponents)
        if (auto* inst = processor.getInstance (m->getInstanceId()))
        {
            m->setTopLeftPosition (inst->position);   // world coordinates
            m->setTransform (t);                      // pan + zoom, view only
        }
}

void PatchCanvas::setZoom (float newZoom, juce::Point<float> focusScreenPos)
{
    newZoom = juce::jlimit (minZoom, maxZoom, newZoom);
    if (std::abs (newZoom - zoom) < 1.0e-4f)
        return;

    // Keep whatever is under the pointer (or under the middle of the screen)
    // pinned in place, so zooming feels like moving a camera rather than like
    // the patch jumping somewhere else and having to be hunted down again.
    const auto anchor = screenToWorld (focusScreenPos);

    zoom = newZoom;

    panOffset = juce::Point<int> (juce::roundToInt (focusScreenPos.x - anchor.x * zoom),
                                  juce::roundToInt (focusScreenPos.y - anchor.y * zoom));

    applyViewToChildren();
    rebuildGrid();
    repaint();

    if (auto* parent = getParentComponent())
        parent->resized();   // refresh the zoom readout
}

void PatchCanvas::zoomBy (float factor)
{
    setZoom (zoom * factor, getLocalBounds().getCentre().toFloat());
}

void PatchCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Plain wheel over empty canvas zooms about the pointer. Sliders consume
    // the wheel themselves, so a knob under the cursor still adjusts normally.
    if (std::abs (w.deltaY) < 1.0e-5f)
        return;

    setZoom (zoom * std::exp (w.deltaY * (w.isReversed ? -1.2f : 1.2f)), e.position);
}

void PatchCanvas::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    // Pinch, where the platform reports it. Android does not deliver magnify
    // gestures through JUCE, which is exactly why the on-screen +/- cluster
    // exists rather than being a convenience.
    if (scaleFactor > 0.0f)
        setZoom (zoom * scaleFactor, e.position);
}

void PatchCanvas::fitToContent()
{
    // the union of every module card, in world coordinates
    juce::Rectangle<int> world;
    for (auto& mc : moduleComponents)
        world = world.isEmpty() ? mc->getBounds() : world.getUnion (mc->getBounds());
    if (world.isEmpty() || getWidth() <= 0 || getHeight() <= 0)
    {
        resetView();
        return;
    }

    // leave room for the toolbar across the top of the patch area
    const int topMargin = 46, margin = 16;
    const float availW = (float) juce::jmax (100, getWidth() - 2 * margin);
    const float availH = (float) juce::jmax (100, getHeight() - topMargin - margin);
    zoom = juce::jlimit (minZoom, 1.0f, juce::jmin (availW / (float) world.getWidth(),
                                                    availH / (float) world.getHeight()));

    const float usedW = (float) world.getWidth() * zoom;
    panOffset = { juce::roundToInt ((float) margin + (availW - usedW) * 0.5f - (float) world.getX() * zoom),
                  juce::roundToInt ((float) topMargin - (float) world.getY() * zoom) };
    panning = false;
    applyViewToChildren();
    rebuildGrid();
    repaint();

    if (auto* parent = getParentComponent())
        parent->resized();
}

void PatchCanvas::resetView()
{
    panOffset = {};
    zoom = 1.0f;
    panning = false;
    applyViewToChildren();
    rebuildGrid();
    repaint();

    if (auto* parent = getParentComponent())
        parent->resized();
}

void PatchCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-clicking a cable removes it
    const auto world = screenToWorld (e.position);
    const int hit = cableIndexNear (world);
    if (hit >= 0)
    {
        processor.removeCable (hit);
        selectedCableIndex = -1;
        repaint();
        return;
    }

    // same gesture for a cable that lands on a knob
    const int pHit = paramCableIndexNear (world);
    if (pHit >= 0)
    {
        processor.removeParamCable (pHit);
        selectedParamCableIndex = -1;
        repaint();
    }
}

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey)
        return false;

    // a selected cable wins over a selected module: it is the more specific
    // (and more recent) click, so Delete removes only the cable
    if (selectedCableIndex >= 0)
    {
        processor.removeCable (selectedCableIndex);
        selectedCableIndex = -1;
        repaint();
        return true;
    }

    if (selectedParamCableIndex >= 0)
    {
        processor.removeParamCable (selectedParamCableIndex);
        selectedParamCableIndex = -1;
        repaint();
        return true;
    }

    if (selectedModuleId >= 0)
    {
        processor.removeModule (selectedModuleId);   // takes its cables with it
        selectedModuleId = -1;
        rebuild();
        return true;
    }

    return false;
}

void PatchCanvas::beginCableDrag (int moduleId, const juce::String& socketId, bool isInput,
                                  SocketKind kind, juce::Point<int> canvasPos)
{
    dragActive = true;
    dragModuleId = moduleId;
    dragSocketId = socketId;
    dragFromInput = isInput;
    dragKind = kind;
    dragCurrentPos = canvasPos;
    repaint();
}

void PatchCanvas::updateCableDrag (juce::Point<int> canvasPos)
{
    // arrives in canvas (screen) space; everything downstream works in world
    dragCurrentPos = screenToWorld (canvasPos.toFloat()).roundToInt();
    repaint();
}

void PatchCanvas::endCableDrag (juce::Point<int> screenPos)
{
    dragActive = false;

    // module bounds are world coordinates, so the drop point must be too
    const auto canvasPos = screenToWorld (screenPos.toFloat()).roundToInt();

    // Walk the modules TOPMOST FIRST (reverse creation order = reverse
    // z-order) and only consider a module if the drop point is actually on
    // it. The old forward scan let any OLDER module that overlapped the
    // target intercept the drop - connecting the cable to a knob hidden
    // underneath, or cancelling on a covered socket - which made knobs on
    // later-created modules (typically effects) randomly refuse cables.
    for (auto it = moduleComponents.rbegin(); it != moduleComponents.rend(); ++it)
    {
        auto& m = *it;

        // small margin so sockets right at the module edge stay droppable
        if (! m->getBounds().expanded (12).contains (canvasPos))
            continue;

        const auto localPos = canvasPos - m->getPosition();
        if (const auto* target = m->findSocketNear (localPos))
        {
            // direction is normalized to output -> input regardless of which
            // end the drag started from; output->output / input->input cancels
            if (target->isInput == dragFromInput)
                break;

            if (dragFromInput)
                processor.addCable (m->getInstanceId(), target->socketId, dragModuleId, dragSocketId);
            else
                processor.addCable (dragModuleId, dragSocketId, m->getInstanceId(), target->socketId);
            break;
        }

        // dropping an OUTPUT cable onto a rotary knob modulates that parameter.
        // Hidden on/off targets (e.g. Pitch Lock Filter keys) default to full
        // depth so a unipolar LFO or DAW automation can actually cross the
        // 0.5 on-threshold; ordinary knobs keep the gentle 30 % default.
        // Sources that set a knob absolutely (Curve CV: 0..1 = the knob's whole
        // range) start at 100 %, so the curve really is the knob.
        if (! dragFromInput)
        {
            if (const auto* spec = m->findModulatableKnobNear (localPos))
            {
                const auto* src = processor.getInstance (dragModuleId);
                const bool absolute = src != nullptr && src->dsp != nullptr && src->dsp->drivesKnobsAbsolutely();
                processor.addParamCable (dragModuleId, dragSocketId,
                                         m->getInstanceId(), spec->id,
                                         (spec->hiddenCableTarget || absolute) ? 1.0f : 0.3f);
                break;
            }
        }

        // the topmost module directly under the pointer owns the drop: if it
        // has no target here, a module covered underneath must not steal it
        if (m->getBounds().contains (canvasPos))
            break;
    }

    repaint();
}

//==============================================================================
// SidebarComponent
//==============================================================================
//==============================================================================
// SidebarTooltip
//==============================================================================
void SidebarTooltip::setText (const juce::String& title, const juce::String& body, int maxWidth)
{
    juce::AttributedString s;
    s.setLineSpacing (1.5f);

    s.append (title + "\n", juce::Font (juce::FontOptions (15.5f, juce::Font::bold)),
              juce::Colours::white);
    s.append (body, juce::Font (juce::FontOptions (14.0f)),
              juce::Colours::white.withAlpha (0.82f));

    const int textWidth = maxWidth - 2 * padding;
    layout.createLayout (s, (float) textWidth);

    setSize (maxWidth, (int) std::ceil (layout.getHeight()) + 2 * padding);
}

void SidebarTooltip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xf01a1a1a));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    layout.draw (g, bounds.reduced ((float) padding).translated (0.0f, 1.0f));
}

//==============================================================================
// SidebarComponent
//==============================================================================
SidebarComponent::SidebarComponent (std::function<void (const juce::String&, bool)> onModuleClicked)
    : moduleClicked (std::move (onModuleClicked))
{
    buildRows();
    startTimerHz (10);   // hover watchdog; the wait itself is timed in ms
}

SidebarComponent::~SidebarComponent()
{
    hideTooltip();
}

const SidebarComponent::Row* SidebarComponent::rowAt (juce::Point<int> pos) const
{
    for (const auto& row : rows)
        if (row.typeId.isNotEmpty() && row.area.contains (pos))
            return &row;
    return nullptr;
}

void SidebarComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto* row = rowAt (e.getPosition());
    const auto typeId = row != nullptr ? row->typeId : juce::String();

    // moving to another module, or off the list, starts the wait over; a few
    // pixels of drift within the same row does not
    if (typeId != hoveredTypeId
        || e.getPosition().getDistanceFrom (hoverPos) > hoverSlackPx)
    {
        if (typeId != hoveredTypeId)
            hideTooltip();

        hoveredTypeId = typeId;
        hoverPos = e.getPosition();
        hoverStartMs = juce::Time::getMillisecondCounter();
    }
}

void SidebarComponent::mouseExit (const juce::MouseEvent&)
{
    hoveredTypeId = {};
    hideTooltip();
}

void SidebarComponent::timerCallback()
{
    if (tooltip != nullptr || hoveredTypeId.isEmpty())
        return;

    // the mouse may have left without a mouseExit (dragged away, another
    // window took over): if it is no longer over the row, drop the hover
    if (! isMouseOver (true))
    {
        hoveredTypeId = {};
        return;
    }

    if (juce::Time::getMillisecondCounter() - hoverStartMs < (juce::uint32) hoverDelayMs)
        return;

    for (const auto& row : rows)
        if (row.typeId == hoveredTypeId && row.area.contains (hoverPos))
        {
            showTooltipFor (row);
            return;
        }
}

void SidebarComponent::showTooltipFor (const Row& row)
{
    juce::String title, body;
    if (row.isToggle && row.typeId == "toggle.autoConnect")
    {
        title = "Auto-Connect";
        body = "When on, the patch starts again from the Init patch, and every module you add is wired up "
               "for you: sound sources (and synth plugins) in parallel into the Polyphony Sum, effects and "
               "filters (and effect plugins) one after another between the Polyphony Sum and Audio Out. "
               "Switch it off to patch by hand again.";
    }
    else if (row.isToggle)
    {
        title = "With Mutation";
        body = "When on, every patch Random Preset builds is then run through the Mutator: three "
               "mutations in a row at 50 %, so the sound wanders further from the engine's own. The "
               "Mutator's Undo brings back the patch as it was built.";
    }
    else if (const auto* gen = aquanode::presets::findGenerator (row.typeId))
    {
        title = gen->tooltipTitle;
        body = gen->tooltipBody;
    }
    else if (const auto* adder = aquanode::presets::findEffectAdder (row.typeId))
    {
        title = "Add " + adder->label;
        body = adder->tooltipBody;
    }
    else
    {
        auto* reg = ModuleFactory::instance().find (row.typeId);
        if (reg == nullptr || reg->descriptor.description.isEmpty())
            return;
        title = reg->descriptor.displayName;
        body = reg->descriptor.description;
    }

    auto* top = getTopLevelComponent();
    if (top == nullptr)
        return;

    tooltip = std::make_unique<SidebarTooltip>();
    tooltip->setText (title, body, juce::jmin (340, juce::jmax (200, top->getWidth() - sidebarWidth - 24)));

    // sit right of the sidebar, vertically centred on the row, and keep
    // the whole panel on screen - the long entries are taller than a row
    const auto rowTopLeft = top->getLocalPoint (this, row.area.getTopLeft());
    const int x = juce::jmin (sidebarWidth + 6, top->getWidth() - tooltip->getWidth() - 6);
    const int y = juce::jlimit (6, juce::jmax (6, top->getHeight() - tooltip->getHeight() - 6),
                                rowTopLeft.y - tooltip->getHeight() / 2 + row.area.getHeight() / 2);

    tooltip->setTopLeftPosition (x, y);
    top->addAndMakeVisible (tooltip.get());
    tooltip->toFront (false);
}

void SidebarComponent::hideTooltip()
{
    tooltip.reset();
}

void SidebarComponent::buildRows()
{
    rows.clear();
    int y = 10;

    // leave room on the right for the Viewport's scrollbar so it never covers text
    const int contentWidth = sidebarWidth - scrollBarWidth;

    // the Auto-Connect switch, above everything
    {
        Row toggle { { 10, y, contentWidth - 20, 24 }, "Auto-Connect", "toggle.autoConnect",
                     juce::Colour (0xff4fc3f7) };
        toggle.isToggle = true;
        rows.push_back (toggle);
        y += 34;
    }

    // Every category is a header that folds its list away (all start folded,
    // so the sidebar stays short); a tap opens or closes it. The plugins sit in
    // their own group right below the category they belong to.
    auto header = [&] (const juce::String& key, const juce::String& text, juce::Colour colour)
    {
        Row r { { 10, y, contentWidth - 20, 22 }, text, {}, colour };
        r.headerKey = key;
        rows.push_back (r);
        y += 26;
        return expandedHeaders.contains (key);
    };

    struct PluginGroup
    {
        ModuleSection section;
        juce::String title;
        std::vector<const RegisteredModule*> rows;
    };
    std::vector<PluginGroup> pluginGroups;

    for (const auto section : allSections())
    {
        const juce::String title = section == ModuleSection::Filter  ? "Filters"
                                 : section == ModuleSection::Effect  ? "Effects"
                                 : section == ModuleSection::Utility ? "Utilities"
                                 : juce::String (sectionName (section));

        // the categories keep their fixed running order (I/O first, then sound
        // generation, ...) but the modules inside each are listed A-Z
        // the modules the plugins are taken apart into ("Aquanova - Oscillator")
        // get a group of their own right below the category
        std::vector<const RegisteredModule*> plain, spectral, fromPlugins;
        for (const auto& r : ModuleFactory::instance().all())
        {
            if (r.descriptor.section != section || ! r.descriptor.listedInSidebar)
                continue;
            if (r.descriptor.typeId.startsWith ("plugin."))
                fromPlugins.push_back (&r);
            // the spectral effects keep their own sub-heading within Effect
            else
                (r.descriptor.typeId.startsWith ("fx.spec") ? spectral : plain).push_back (&r);
        }

        auto byName = [] (const RegisteredModule* a, const RegisteredModule* b)
        {
            return a->descriptor.displayName.compareNatural (b->descriptor.displayName) < 0;
        };
        std::sort (plain.begin(), plain.end(), byName);
        std::sort (spectral.begin(), spectral.end(), byName);
        std::sort (fromPlugins.begin(), fromPlugins.end(), byName);

        auto emitRow = [&] (const RegisteredModule* r)
        {
            rows.push_back ({ { 18, y, contentWidth - 28, 18 },
                              r->descriptor.displayName, r->descriptor.typeId, juce::Colours::white });
            y += 20;
        };

        if (header ("section." + juce::String ((int) section), title, sectionColour (section)))
        {
            for (const auto* r : plain)
                emitRow (r);

            if (! spectral.empty())
            {
                rows.push_back ({ { 14, y + 4, contentWidth - 24, 18 },
                                  "Spectral", {}, sectionColour (section) });
                y += 26;
                for (const auto* r : spectral)
                    emitRow (r);
            }
            y += 8;
        }

        if (! fromPlugins.empty())
            pluginGroups.push_back ({ section, title, std::move (fromPlugins) });
    }

    // then the same categories for the modules the plugins are taken apart
    // into, in the same order
    for (auto& group : pluginGroups)
    {
        const juce::String pluginTitle = group.section == ModuleSection::Oscillator ? "Plugin Generators"
                                       : "Plugin " + group.title;
        if (header ("plugins." + juce::String ((int) group.section), pluginTitle, pluginSectionColour()))
        {
            for (const auto* r : group.rows)
            {
                rows.push_back ({ { 18, y, contentWidth - 28, 18 },
                                  r->descriptor.displayName, r->descriptor.typeId, juce::Colours::white });
                y += 20;
            }
            y += 8;
        }
    }

    // Plugins: the plugins merged into Aquanode Modular, all in one block.
    // Tapping a name (or its GUI pill) adds the original plugin as one card;
    // the Patch pill opens a menu that builds it out of its own modules instead.
    if (header ("plugins", "Plugins", pluginSectionColour()))
    {
        // A-Z, like every list in the sidebar
        std::vector<const aquanode::plugins::CatalogEntry*> sortedPlugins;
        for (const auto& entry : aquanode::plugins::catalog())
            sortedPlugins.push_back (&entry);
        std::sort (sortedPlugins.begin(), sortedPlugins.end(), [] (auto* a, auto* b)
                   { return a->name.compareNatural (b->name) < 0; });

        for (const auto* entryPtr : sortedPlugins)
        {
            const auto& entry = *entryPtr;
            if (ModuleFactory::instance().find (entry.guiTypeId) == nullptr)
                continue;   // its sources are not part of this build

            Row row { { 14, y, contentWidth - 24, 22 }, entry.name, entry.guiTypeId, juce::Colours::white };
            row.isPlugin = true;
            row.pluginHasPatch = entry.hasPatchVersion();
            if (auto* reg = ModuleFactory::instance().find (entry.guiTypeId))
                row.colour = reg->descriptor.colour();
            rows.push_back (row);
            y += 25;
        }
        y += 8;
    }
    y += 6;

    // Preset Generation: one row per patch generator. Each REPLACES the whole
    // patch; the instrument and the FX build both offer them (an FX build
    // plays a generated patch from the on-screen keys or the track's MIDI).
    {
        // the With Mutation toggle, at the right end of the title row (listed
        // before the header, so a tap on it is the toggle's)
        Row toggle { { contentWidth - 14 - 72, y + 2, 72, 18 }, "With Mutation", "toggle.withMutation",
                     presetSectionColour() };
        toggle.isToggle = true;
        rows.push_back (toggle);
    }
    const auto& gens = aquanode::presets::generators();
    if (header ("presets", "Random Preset", presetSectionColour()))
    {
        // Random (the generators' last) first, as a button of its own, then
        // the categories A-Z
        auto genRow = [&] (size_t i, bool apart)
        {
            Row row { { 14, y, contentWidth - 28, 24 }, gens[i].label, gens[i].id, presetSectionColour() };
            row.isGenerator = true;
            row.hasMenu = gens[i].hasMenu();
            row.centreFullWidth = apart;
            rows.push_back (row);
            y += apart ? 33 : 29;
        };
        if (! gens.empty())
            genRow (gens.size() - 1, true);
        for (size_t i = 0; i + 1 < gens.size(); ++i)
            genRow (i, false);
        y += 4;
    }

    // Add Effect: rows that ADD to the loaded patch instead of replacing it -
    // effects in front of Audio Out, Midi Add at the start
    if (header ("addEffect", "Add Effect", presetSectionColour()))
    {
        for (const auto& adder : aquanode::presets::effectAdders())
        {
            Row add { { 14, y, contentWidth - 28, 24 }, adder.label, adder.id, presetSectionColour() };
            add.isGenerator = true;
            add.centreFullWidth = true;
            rows.push_back (add);
            y += 29;
        }
    }

    y += 10;

    // size ourselves to fit every row; the parent Viewport takes care of clipping + scrolling
    setSize (contentWidth, y);
}

void SidebarComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    // a first tap (touch screens): the row waits for the second one
    if (pendingTapKey.isNotEmpty() && juce::Time::getMillisecondCounter() - pendingTapMs < (juce::uint32) doubleTapMs)
        for (const auto& row : rows)
            if (row.typeId.isNotEmpty() && pendingTapKey.startsWith (row.typeId + "|"))
            {
                g.setColour (juce::Colours::white.withAlpha (0.16f));
                g.fillRoundedRectangle (row.area.toFloat().expanded (2.0f, 1.0f), 4.0f);
            }

    for (const auto& row : rows)
    {
        if (row.isToggle)
        {
            const auto r = row.area.toFloat().reduced (0.5f);
            const bool on = row.typeId == "toggle.autoConnect" ? autoConnect : withMutation;
            if (on)
            {
                g.setColour (row.colour);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (juce::Colours::black);
            }
            else
            {
                g.setColour (row.colour.withAlpha (0.55f));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.6f));
            }
            g.setFont (juce::Font (juce::FontOptions (row.typeId == "toggle.autoConnect" ? 12.5f : 10.5f,
                                                      on ? juce::Font::bold : juce::Font::plain)));
            g.drawText (row.text, row.area, juce::Justification::centred);
            continue;
        }

        if (row.isGenerator)
        {
            auto r = row.area.toFloat();
            g.setColour (row.colour.withAlpha (0.20f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (row.colour);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.2f);

            // the menu arrow, set off by a thin divider (rows with a choice only)
            auto textArea = row.area.withTrimmedLeft (6);
            if (row.hasMenu)
            {
                const auto arrowArea = generatorMenuArea (row).toFloat();
                g.setColour (row.colour.withAlpha (0.55f));
                g.drawVerticalLine ((int) arrowArea.getX(), r.getY() + 5.0f, r.getBottom() - 5.0f);
                juce::Path tri;
                const auto c = arrowArea.getCentre();
                tri.addTriangle (c.x - 5.0f, c.y - 2.5f, c.x + 5.0f, c.y - 2.5f, c.x, c.y + 3.5f);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillPath (tri);
                textArea = textArea.withTrimmedRight (30);
            }
            else
            {
                // keep the names of the category rows lined up, menu or not
                textArea = textArea.withTrimmedRight (row.centreFullWidth ? 6 : 30);
            }

            // the name, and the ticked engine (if any) small and dim after it
            juce::String ticked;
            if (const auto* gen = aquanode::presets::findGenerator (row.typeId))
                if (gen->selection != nullptr)
                    ticked = gen->selection();

            juce::AttributedString text;
            text.setJustification (juce::Justification::centred);
            text.setWordWrap (juce::AttributedString::none);
            text.append (row.text, juce::Font (juce::FontOptions (12.5f, juce::Font::bold)), juce::Colours::white);
            if (ticked.isNotEmpty())
                text.append ("  " + ticked, juce::Font (juce::FontOptions (11.0f)), juce::Colours::white.withAlpha (0.55f));
            text.draw (g, textArea.toFloat());
            continue;
        }

        if (row.isPlugin)
        {
            // accent dot, name, and the GUI / Patch pills
            const auto dot = juce::Rectangle<float> (5.0f, 5.0f)
                                 .withCentre ({ (float) row.area.getX() + 3.5f, (float) row.area.getCentreY() });
            g.setColour (row.colour);
            g.fillEllipse (dot);

            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            g.drawText (row.text, row.area.withTrimmedLeft (11).withTrimmedRight (88),
                        juce::Justification::centredLeft);

            auto pill = [&g, &row] (juce::Rectangle<int> area, const juce::String& label, bool enabled)
            {
                const auto r = area.toFloat().reduced (0.5f);
                g.setColour (row.colour.withAlpha (enabled ? 0.25f : 0.08f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (row.colour.withAlpha (enabled ? 0.9f : 0.3f));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                g.setColour (juce::Colours::white.withAlpha (enabled ? 0.95f : 0.35f));
                g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
                g.drawText (label, area, juce::Justification::centred);
            };
            pill (pluginGuiPillArea (row), "GUI", true);
            pill (pluginPatchPillArea (row), "Patch", row.pluginHasPatch);
            continue;
        }

        const bool isTitle = row.typeId.isEmpty();
        g.setColour (row.colour);
        g.setFont (juce::Font (juce::FontOptions (isTitle ? 14.0f : 12.5f,
                                                  isTitle ? juce::Font::bold : juce::Font::plain)));
        if (row.headerKey.isNotEmpty())
        {
            // a fold arrow: pointing right when folded, down when open
            const bool open = expandedHeaders.contains (row.headerKey);
            const auto c = juce::Point<float> ((float) row.area.getX() + 5.0f, (float) row.area.getCentreY());
            juce::Path tri;
            if (open) tri.addTriangle (c.x - 4.5f, c.y - 2.5f, c.x + 4.5f, c.y - 2.5f, c.x, c.y + 3.5f);
            else      tri.addTriangle (c.x - 2.5f, c.y - 4.5f, c.x - 2.5f, c.y + 4.5f, c.x + 3.5f, c.y);
            g.fillPath (tri);
            g.drawText (row.text, row.area.withTrimmedLeft (16), juce::Justification::centredLeft);
            continue;
        }
        g.drawText (row.text, row.area, juce::Justification::centredLeft);
    }
}

void SidebarComponent::mouseUp (const juce::MouseEvent& e)
{
    // a finger that moved was scrolling the list, not choosing from it
    if (e.mouseWasDraggedSinceMouseDown())
        return;

    // a category header folds or opens its list (unless the tap is on a
    // switch sitting in the header's row)
    for (const auto& row : rows)
        if (row.headerKey.isNotEmpty() && row.area.contains (e.getPosition()))
        {
            const bool onToggle = std::any_of (rows.begin(), rows.end(), [&e] (const Row& r)
                                               { return r.isToggle && r.area.contains (e.getPosition()); });
            if (onToggle)
                break;
            if (expandedHeaders.contains (row.headerKey)) expandedHeaders.removeString (row.headerKey);
            else                                          expandedHeaders.add (row.headerKey);
            hideTooltip();
            buildRows();
            repaint();
            if (onExpandedChanged != nullptr)
                onExpandedChanged (expandedHeaders);
            return;
        }

    for (const auto& row : rows)
    {
        if (row.typeId.isNotEmpty() && row.area.contains (e.getPosition()))
        {
            // Touch screens add on tapping twice: a single tap while scrolling
            // the list is too easy to make by accident.
            if (e.source.isTouch() && ! row.isToggle)
            {
                const juce::String part = row.isPlugin ? (pluginPatchPillArea (row).contains (e.getPosition()) ? "patch" : "gui")
                                        : (row.isGenerator && row.hasMenu && generatorMenuArea (row).contains (e.getPosition()) ? "menu" : "row");
                const auto key = row.typeId + "|" + part;
                const auto now = juce::Time::getMillisecondCounter();
                if (key != pendingTapKey || now - pendingTapMs > (juce::uint32) doubleTapMs)
                {
                    pendingTapKey = key;
                    pendingTapMs = now;
                    repaint();
                    juce::Component::SafePointer<SidebarComponent> safe (this);
                    juce::Timer::callAfterDelay (doubleTapMs + 20, [safe] { if (safe != nullptr) safe->repaint(); });
                    return;
                }
                pendingTapKey.clear();
                repaint();
            }

            // the module is being added; the description has served its purpose
            hideTooltip();
            hoverStartMs = juce::Time::getMillisecondCounter();

            if (row.isToggle)
            {
                if (row.typeId == "toggle.autoConnect")
                {
                    autoConnect = ! autoConnect;
                    repaint();
                    if (onAutoConnectChanged != nullptr)
                        onAutoConnectChanged (autoConnect);
                    return;
                }
                withMutation = ! withMutation;
                repaint();
                if (onWithMutationChanged != nullptr)
                    onWithMutationChanged (withMutation);
                return;
            }

            if (row.isPlugin)
            {
                const auto pluginId = row.typeId.fromFirstOccurrenceOf ("plugin.", false, false);
                const bool patch = pluginPatchPillArea (row).contains (e.getPosition()) || e.mods.isPopupMenu();
                if (patch && ! row.pluginHasPatch)
                    return;
                if (moduleClicked != nullptr)
                    moduleClicked ((patch ? "pluginpatch:" : "plugingui:") + pluginId, patch);
                return;
            }

            const bool openMenu = row.isGenerator && row.hasMenu
                && (generatorMenuArea (row).contains (e.getPosition()) || e.mods.isPopupMenu());

            if (moduleClicked != nullptr)
                moduleClicked (row.typeId, openMenu);
            return;
        }
    }
}

//==============================================================================
// MutatorPanel
//==============================================================================
MutatorPanel::MutatorPanel (AquanodeModularAudioProcessor& p, PatchCanvas& c)
    : processor (p), canvas (c)
{
    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setRange (1.0, 100.0, 1.0);
    amountSlider.setValue (25.0, juce::dontSendNotification);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
    amountSlider.setTextValueSuffix (" %");
    addAndMakeVisible (amountSlider);

    for (auto* b : { &mutateButton, &undoButton, &storeAButton, &storeBButton,
                     &recallAButton, &recallBButton, &breedButton })
        addAndMakeVisible (b);

    mutateButton.onClick  = [this] { doMutate(); };
    undoButton.onClick    = [this]
    {
        if (! history.empty())
        {
            apply (history.back());
            history.pop_back();
            canvas.refreshAllModuleValues();
        }
    };
    storeAButton.onClick  = [this] { slotA = capture(); hasA = true; };
    storeBButton.onClick  = [this] { slotB = capture(); hasB = true; };
    recallAButton.onClick = [this] { doRecall (slotA, hasA); };
    recallBButton.onClick = [this] { doRecall (slotB, hasB); };
    breedButton.onClick   = [this] { doBreed(); };
}

void MutatorPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xdd141210));   // dark strip over the canvas
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Amount", 8, 0, 52, getHeight(), juce::Justification::centredLeft);
}

void MutatorPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    area.removeFromLeft (56);                              // "Amount" label
    amountSlider.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (8);

    const int bw = (area.getWidth() - 6 * 4) / 7;
    for (auto* b : { &mutateButton, &undoButton, &storeAButton, &storeBButton,
                     &recallAButton, &recallBButton, &breedButton })
    {
        b->setBounds (area.removeFromLeft (bw));
        area.removeFromLeft (4);
    }
}

MutatorPanel::Snapshot MutatorPanel::capture() const
{
    Snapshot snap;
    for (const int id : processor.getModuleIds())
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr)
            continue;

        auto& values = snap[id];
        for (const auto& p : inst->descriptor().params)
            if (p.type != ParamType::Button)
                values[p.id] = inst->dsp->getParameter (p.id);
    }
    return snap;
}

void MutatorPanel::apply (const Snapshot& snap)
{
    for (const auto& [id, values] : snap)
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr)
            continue;   // topology changed since capture - skip gracefully

        for (const auto& [paramId, value] : values)
            inst->dsp->setParameter (paramId, value);
    }
}

void MutatorPanel::pushHistory()
{
    history.push_back (capture());
    if (history.size() > 64)
        history.erase (history.begin());
}

float MutatorPanel::mutateValue (const ParamSpec& p, float v, float amount)
{
    // approximate Gaussian from four uniforms
    auto gauss = [this]
    {
        return (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 2.0f) * 0.7f;
    };

    switch (p.type)
    {
        case ParamType::Button:
            return v;

        case ParamType::Combo:
        case ParamType::RotarySteppedList:
            // discrete params jump to a random choice with low probability -
            // rare but drastic, which is where the fun mutations come from
            if (p.choices.size() > 1 && rng.nextFloat() < amount * 0.25f)
                return (float) rng.nextInt (p.choices.size());
            return v;

        default:
        {
            if (p.interval == 1.0f)   // integer rotary (grain count, note, stages...)
            {
                const float range = p.maxValue - p.minValue;
                const float nv = v + std::round (gauss() * amount * range * 0.2f);
                return juce::jlimit (p.minValue, p.maxValue, nv);
            }

            // continuous: perturb in normalised, taper-aware space so log
            // knobs (cutoff, times) mutate musically instead of linearly
            const bool logTaper = p.logarithmic && p.minValue > 0.0f;
            float norm;
            if (logTaper)
                norm = std::log (juce::jmax (p.minValue, v) / p.minValue)
                       / std::log (p.maxValue / p.minValue);
            else
                norm = (v - p.minValue) / juce::jmax (1.0e-9f, p.maxValue - p.minValue);

            norm = juce::jlimit (0.0f, 1.0f, norm + gauss() * amount * 0.25f);

            return logTaper ? p.minValue * std::pow (p.maxValue / p.minValue, norm)
                            : p.minValue + norm * (p.maxValue - p.minValue);
        }
    }
}

void MutatorPanel::doMutate()
{
    pushHistory();
    mutateOnce ((float) amountSlider.getValue() * 0.01f);
    canvas.refreshAllModuleValues();
}

void MutatorPanel::mutateSeveral (float amount, int times)
{
    pushHistory();
    for (int i = 0; i < times; ++i)
        mutateOnce (amount);
    canvas.refreshAllModuleValues();
}

void MutatorPanel::mutateOnce (float amount)
{
    for (const int id : processor.getModuleIds())
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr || inst->descriptor().section == ModuleSection::InputOutput)
            continue;   // never mutate Audio In/Out levels

        // A Piano Roll's tempo, length and play state are the arrangement,
        // not the sound - mutating them would stop or reshape the sequence.
        if (inst->descriptor().typeId == "util.pianoroll")
            continue;

        for (const auto& p : inst->descriptor().params)
        {
            // FM Ratio sets the oscillator's harmonic relationship to the note being
            // played (DX7-style operator ratio) - randomising it detunes the whole
            // patch's FM structure, so the Mutate button leaves it untouched.
            if (inst->descriptor().typeId == "osc.basic" && p.id == "fmRatio")
                continue;

            // Voices is a polyphony setting, not a sound-design parameter -
            // randomising it would silently change how the patch plays.
            if (p.id == "voices")
                continue;

            const float v = inst->dsp->getParameter (p.id);
            inst->dsp->setParameter (p.id, mutateValue (p, v, amount));
        }
    }
}

void MutatorPanel::doBreed()
{
    if (! hasA || ! hasB)
        return;

    pushHistory();
    const float amount = (float) amountSlider.getValue() * 0.01f;

    for (const int id : processor.getModuleIds())
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr || inst->descriptor().section == ModuleSection::InputOutput)
            continue;

        const auto itA = slotA.find (id);
        const auto itB = slotB.find (id);

        for (const auto& p : inst->descriptor().params)
        {
            if (p.type == ParamType::Button)
                continue;

            // uniform crossover: each gene comes from parent A or B; fall
            // back to the current value when a parent doesn't know this param
            float v = inst->dsp->getParameter (p.id);
            const auto* parent = rng.nextBool() ? (itA != slotA.end() ? &itA->second : nullptr)
                                                : (itB != slotB.end() ? &itB->second : nullptr);
            if (parent != nullptr)
            {
                const auto pv = parent->find (p.id);
                if (pv != parent->end())
                    v = pv->second;
            }

            // light mutation on top of the crossover - except Voices, which is
            // inherited as-is (polyphony shouldn't drift with each breed)
            inst->dsp->setParameter (p.id, p.id == "voices" ? v
                                                            : mutateValue (p, v, amount * 0.3f));
        }
    }

    canvas.refreshAllModuleValues();
}

void MutatorPanel::doRecall (const Snapshot& snap, bool exists)
{
    if (! exists)
        return;
    pushHistory();
    apply (snap);
    canvas.refreshAllModuleValues();
}

//==============================================================================
// Editor
//==============================================================================
AquanodeModularAudioProcessorEditor::AquanodeModularAudioProcessorEditor (AquanodeModularAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      sidebar ([this] (const juce::String& typeId, bool openMenu)
      {
          if (typeId.startsWith ("plugingui:"))
          {
              addPluginGui (typeId.fromFirstOccurrenceOf (":", false, false));
              return;
          }

          if (typeId.startsWith ("pluginpatch:"))
          {
              showPluginPatchMenu (typeId.fromFirstOccurrenceOf (":", false, false));
              return;
          }

          if (aquanode::presets::findGenerator (typeId) != nullptr)
          {
              runGenerator (typeId, openMenu);
              return;
          }

          if (const auto* adder = aquanode::presets::findEffectAdder (typeId))
          {
              auto seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
              const auto note = adder->apply (processor, seed == 0 ? 1u : seed);
              if (note.isNotEmpty() && ! processor.patchName.contains ("+ " + note))
                  processor.patchName = processor.patchName + " + " + note;
              canvas.rebuild();
              canvas.fitToContent();
              return;
          }

          // clicking a sidebar name opens one instance in the patch map
          // spawn into the currently visible area, regardless of pan
          const auto viewSpawn = juce::Point<int> (40 + (placementCounter % 4) * 36,
                                                   40 + (placementCounter % 6) * 30);
          const auto pos = viewSpawn - canvas.getPanOffset();
          ++placementCounter;
          const auto before = processor.getModuleIds();
          const int newId = processor.addModule (typeId, pos);
          if (processor.autoConnect)
              autoConnectAdded (before, AutoRole::FromSection);
          canvas.rebuild();
          canvas.selectModule (newId);
      }),
      canvas (p),
      mutatorPanel (p, canvas)
{
    addAndMakeVisible (sidebarViewport);
    sidebar.setWithMutation (processor.generateWithMutation);
    sidebar.onWithMutationChanged = [this] (bool on) { processor.generateWithMutation = on; };
    sidebar.setExpandedHeaders (processor.uiExpandedHeaders);
    sidebar.onExpandedChanged = [this] (const juce::StringArray& open) { processor.uiExpandedHeaders = open; };
    sidebar.setAutoConnect (processor.autoConnect);
    sidebar.onAutoConnectChanged = [this] (bool on)
    {
        processor.autoConnect = on;
        if (on)
        {
            // a fresh start, from which everything added is wired up
            processor.initializePatch();
            canvas.rebuild();
            canvas.resetView();
        }
    };
    sidebarViewport.setViewedComponent (&sidebar, false);   // false: sidebar is a member, viewport must not delete it
    sidebarViewport.setScrollBarsShown (true, false);        // vertical only
    addAndMakeVisible (canvas);
    addChildComponent (mutatorPanel);   // hidden until the Mutator button toggles it
    addAndMakeVisible (toolbarStrip);   // added before the buttons: they draw on top of it

    // On-screen keyboard: driven by (and mirrored onto) the processor's shared
    // MidiKeyboardState, shown along the bottom on every build and toggled by
    // the Keys button. Both panel toggles remember their state in the
    // processor, so a plugin window reopens the way it was left.
    sidebarVisible = processor.uiSidebarVisible;
    keyboardVisible = processor.uiKeyboardVisible;
    sidebarViewport.setVisible (sidebarVisible);

    midiKeyboard = std::make_unique<juce::MidiKeyboardComponent> (
                       processor.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
    midiKeyboard->setOctaveForMiddleC (4);
    midiKeyboard->setKeyPressBaseOctave (5);   // computer keys: 'A' plays middle C
    addChildComponent (*midiKeyboard);
    midiKeyboard->setVisible (keyboardVisible);

    for (auto* b : { &initButton, &cloneButton, &deleteButton, &exportButton, &importButton,
                     &mutatorButton, &sidebarButton, &keysButton })
        addAndMakeVisible (b);

    // the grip that sets the keyboard's height
    addChildComponent (keyboardHandle);
    keyboardHandle.getHeightNow = [this] { return midiKeyboard != nullptr ? midiKeyboard->getHeight() : 0; };
    keyboardHandle.onHeightChanged = [this] (int h)
    {
        processor.uiKeyboardHeight = h;
        resized();
    };

    for (auto* b : { &zoomOutButton, &zoomInButton })
        addAndMakeVisible (b);
    addAndMakeVisible (zoomReadout);
    addAndMakeVisible (patchNameLabel);

    sidebarButton.onClick = [this]
    {
        sidebarVisible = ! sidebarVisible;
        processor.uiSidebarVisible = sidebarVisible;
        sidebarViewport.setVisible (sidebarVisible);
        resized();
    };

    keysButton.onClick = [this]
    {
        keyboardVisible = ! keyboardVisible;
        processor.uiKeyboardVisible = keyboardVisible;
        if (midiKeyboard != nullptr)
            midiKeyboard->setVisible (keyboardVisible);
        resized();
    };

    zoomInButton .onClick = [this] { canvas.zoomBy (1.25f); };
    zoomOutButton.onClick = [this] { canvas.zoomBy (1.0f / 1.25f); };
    zoomReadout.onClick = [this] { canvas.resetView(); };

    mutatorButton.setClickingTogglesState (true);
    mutatorButton.onClick = [this]
    {
        mutatorPanel.setVisible (mutatorButton.getToggleState());
    };

    initButton.onClick = [this]
    {
        processor.initializePatch();
        canvas.rebuild();
    };

    cloneButton.onClick = [this]
    {
        const int sel = canvas.getSelectedModuleId();
        if (sel >= 0)
        {
            const int newId = processor.cloneModule (sel);   // params kept, no cables
            canvas.rebuild();
            canvas.selectModule (newId);
        }
    };

    deleteButton.onClick = [this]
    {
        const int sel = canvas.getSelectedModuleId();
        if (sel >= 0)
        {
            processor.removeModule (sel);                    // removes its cables too
            canvas.rebuild();
        }
    };

    exportButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Export Patch", juce::File(), "*.zip");
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
               #if JUCE_ANDROID
                // The picker returns a document URL (may be a content:// URI).
                // Write the same .zip container through it -> 1:1 compatible.
                const auto url = fc.getURLResult();
                if (url.isEmpty())
                    return;

                auto doc = juce::AndroidDocument::fromDocument (url);
                if (doc.hasValue())
                    if (auto out = doc.createOutputStream())
                        processor.exportPatchToStream (*out);
               #else
                auto file = fc.getResult();
                if (file != juce::File())
                    processor.exportPatchToZip (file.withFileExtension ("zip"));
               #endif
            });
    };

    importButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Import Patch", juce::File(), "*.zip");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
               #if JUCE_ANDROID
                const auto url = fc.getURLResult();
                if (url.isEmpty())
                    return;

                auto doc = juce::AndroidDocument::fromDocument (url);
                if (doc.hasValue())
                {
                    if (auto in = doc.createInputStream())
                    {
                        processor.importPatchFromStream (*in,
                            doc.getInfo().getName().upToLastOccurrenceOf (".", false, false));
                        canvas.rebuild();
                    }
                }
               #else
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    processor.importPatchFromZip (file);
                    canvas.rebuild();
                }
               #endif
            });
    };

    processor.addChangeListener (this);

   #if JUCE_ANDROID
    // Fill the device screen; the standalone wrapper resizes us to the display,
    // and resized() lays everything out responsively (landscape assumed).
    setResizable (true, false);
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setSize (disp->userArea.getWidth(), disp->userArea.getHeight());
    else
        setSize (1280, 720);
   #else
    setSize (1200, 700);
    setResizable (false, false);
   #endif

    canvas.rebuild();

    // a generated patch is bigger than the window: show all of it the first
    // time the editor opens on it (async, once the layout has settled)
    if (processor.uiFitOnOpen)
    {
        processor.uiFitOnOpen = false;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> (this)]
        {
            if (safe != nullptr)
                safe->canvas.fitToContent();
        });
    }
}

AquanodeModularAudioProcessorEditor::~AquanodeModularAudioProcessorEditor()
{
    processor.removeChangeListener (this);
}

void AquanodeModularAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    canvas.resetView();
    canvas.rebuild();
}

void AquanodeModularAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void AquanodeModularAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Bottom on-screen keyboard, height scaled to the window (clamped sane):
    // finger-sized on a phone, a little more compact for a mouse.
   #if JUCE_ANDROID
    const int kbH = juce::jlimit (110, 220, getHeight() / 4);
    const float kbKeys = 30.0f, kbMinKey = 26.0f;
   #else
    const int kbH = juce::jlimit (90, 140, getHeight() / 6);
    const float kbKeys = 36.0f, kbMinKey = 18.0f;
   #endif
    if (midiKeyboard != nullptr && keyboardVisible)
    {
        // the height the grip set, if any (kept within the window)
        const int wanted = processor.uiKeyboardHeight > 0 ? processor.uiKeyboardHeight : kbH;
        const int h = juce::jlimit (50, juce::jmax (60, (int) (getHeight() * 0.7f)), wanted);
        auto kbArea = area.removeFromBottom (h);
        midiKeyboard->setKeyWidth (juce::jmax (kbMinKey, kbArea.getWidth() / kbKeys));
        midiKeyboard->setBounds (kbArea);

       #if JUCE_ANDROID
        const int gripH = 26;   // finger-sized
       #else
        const int gripH = 16;
       #endif
        keyboardHandle.setBounds (kbArea.getCentreX() - 50, kbArea.getY() - gripH, 100, gripH);
        keyboardHandle.setVisible (true);
        keyboardHandle.toFront (false);
    }
    else
    {
        keyboardHandle.setVisible (false);
    }

   #if JUCE_ANDROID
    // Slightly wider sidebar and bigger buttons for touch.
    const int sideW = juce::jlimit (200, 300, getWidth() / 5);
    const int bh = 40, gap = 8, topY = 8;
   #else
    const int sideW = SidebarComponent::sidebarWidth;
    const int bh = 22, gap = 5, topY = 8;
   #endif

    // Hidden sidebar gives its whole width back to the patch area.
    sidebarViewport.setBounds (area.removeFromLeft (sidebarVisible ? sideW : 0));
    canvas.setBounds (area);

    // mutator strip along the bottom of the patch area (above the keyboard)
    mutatorPanel.setBounds (area.getX(), area.getBottom() - 34, area.getWidth(), 34);

    // Toolbar strip along the top of the patch area, laid out the same way as
    // the mutator strip: it spans exactly the patch region, so nothing can
    // reach across the sidebar, and the buttons split the width evenly so all
    // six stay visible however narrow the screen gets.
    const int stripH = topY + bh + topY;
    const auto strip = juce::Rectangle<int> (area.getX(), area.getY(), area.getWidth(), stripH);
    toolbarStrip.setBounds (strip);

    {
        auto bar = strip.reduced (gap, topY);   // editor coordinates

        // [-][+] sit at the very right of the toolbar, right next to Mutator,
        // narrower than the other toolbar buttons since they only ever hold
        // a single glyph. Carved off the bar first so the remaining buttons
        // divide up whatever width is left, same as before.
       #if JUCE_ANDROID
        const int zw = 30;
       #else
        const int zw = 25;
       #endif
        auto zoomArea = bar.removeFromRight (zw * 2 + gap);
        zoomInButton .setBounds (zoomArea.removeFromRight (zw));
        zoomArea.removeFromRight (gap);
        zoomOutButton.setBounds (zoomArea);
        bar.removeFromRight (gap);

        std::vector<juce::TextButton*> buttons {
            &mutatorButton, &importButton, &exportButton,
            &deleteButton,  &cloneButton,  &initButton
        };

        // Panel toggles sit at the far left of the strip, past the six patch
        // buttons, so the order everyone already knows is untouched.
        buttons.push_back (&sidebarButton);
        buttons.push_back (&keysButton);

        const int n = (int) buttons.size();
        const int bw = (bar.getWidth() - (n - 1) * gap) / n;

        // Right to left, so the button order on screen is unchanged.
        for (int i = 0; i < n; ++i)
            buttons[(size_t) i]->setBounds (bar.getRight() - bw - i * (bw + gap),
                                            bar.getY(), bw, bar.getHeight());
    }

    // Zoom percentage: a small text readout in the bottom-right of the patch
    // area, lifted clear of the mutator strip so the two can never collide
    // when the Mutator is open.
    {
        const int lw = 34, lh = 16;
        zoomReadout.setBounds(area.getRight() - lw - 8, 45, lw, lh);

        // the patch name sits at the same height on the left of the patch area
        patchNameLabel.setBounds (area.getX() + 10, 45, juce::jmax (60, area.getWidth() / 2), lh);
    }

    updateZoomReadout();
}

//==============================================================================
// Plugins section
//==============================================================================
void AquanodeModularAudioProcessorEditor::addPluginGui (const juce::String& pluginId)
{
    const auto viewSpawn = juce::Point<int> (40 + (placementCounter % 4) * 36,
                                             40 + (placementCounter % 6) * 30);
    ++placementCounter;
    const auto before = processor.getModuleIds();
    const int card = aquanode::plugins::addGuiVersion (processor, pluginId, viewSpawn - canvas.getPanOffset());
    if (card >= 0 && processor.autoConnect)
        autoConnectAdded (before, aquanode::plugins::isEffectPluginId (pluginId) ? AutoRole::Effect : AutoRole::Source);
    canvas.rebuild();
    if (card >= 0)
        canvas.selectModule (card);
}

void AquanodeModularAudioProcessorEditor::showPluginPatchMenu (const juce::String& pluginId)
{
    auto* entry = aquanode::plugins::findEntry (pluginId);
    if (entry == nullptr || ! entry->hasPatchVersion())
        return;

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safeThis (this);
    const auto id = pluginId;

    juce::PopupMenu menu;
    menu.addSectionHeader (entry->name + " - Patch version");
    menu.addItem ("New patch", [safeThis, id] { if (safeThis) safeThis->buildPluginPatch (id, {}, true); });
    menu.addItem ("Add to this patch", [safeThis, id] { if (safeThis) safeThis->buildPluginPatch (id, {}, false); });

    if (entry->presetPatterns.isNotEmpty() && entry->loadPreset != nullptr)
    {
        menu.addSeparator();
        menu.addItem ("New patch from preset...", [safeThis, id] { if (safeThis) safeThis->pickPluginPreset (id, true); });
        menu.addItem ("Add preset to this patch...", [safeThis, id] { if (safeThis) safeThis->pickPluginPreset (id, false); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sidebarViewport)
                                                  .withMousePosition());
}

void AquanodeModularAudioProcessorEditor::buildPluginPatch (const juce::String& pluginId,
                                                            const juce::File& presetFile, bool replace)
{
    const auto before = processor.getModuleIds();
    const auto name = aquanode::plugins::addPatchVersion (processor, pluginId, presetFile, replace);
    if (name.isNotEmpty() && ! replace && processor.autoConnect)
        autoConnectAdded (before, aquanode::plugins::isEffectPluginId (pluginId) ? AutoRole::Effect : AutoRole::Source);
    if (name.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugins",
            presetFile != juce::File() ? "That preset could not be read." : "The patch version could not be built.");
        return;
    }

    processor.uiFitOnOpen = false;
    canvas.rebuild();
    canvas.fitToContent();
}

void AquanodeModularAudioProcessorEditor::pickPluginPreset (const juce::String& pluginId, bool replace)
{
    auto* entry = aquanode::plugins::findEntry (pluginId);
    if (entry == nullptr)
        return;

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safeThis (this);
    fileChooser = std::make_unique<juce::FileChooser> ("Load " + entry->name + " preset", juce::File(),
                                                       entry->presetPatterns);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, pluginId, replace] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;

           #if JUCE_ANDROID
            // A content:// document has no path, and every plugin's own
            // loader reads a juce::File - so the preset is copied to a
            // scratch file of the same name first.
            const auto url = fc.getURLResult();
            if (url.isEmpty())
                return;
            auto doc = juce::AndroidDocument::fromDocument (url);
            if (! doc.hasValue())
                return;
            const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("PluginPresets").getChildFile (doc.getInfo().getName());
            scratch.getParentDirectory().createDirectory();
            scratch.deleteFile();
            {
                auto in = doc.createInputStream();
                juce::FileOutputStream out (scratch);
                if (in == nullptr || ! out.openedOk())
                    return;
                out.writeFromInputStream (*in, -1);
            }
            safeThis->buildPluginPatch (pluginId, scratch, replace);
           #else
            const auto file = fc.getResult();
            if (file.existsAsFile())
                safeThis->buildPluginPatch (pluginId, file, replace);
           #endif
        });
}

void AquanodeModularAudioProcessorEditor::applyGenerated (std::function<juce::String (AquanodeModularAudioProcessor&)> build)
{
    if (build == nullptr)
        return;
    processor.generatePatch (build);
    processor.uiFitOnOpen = false;
    canvas.rebuild();

    // With Mutation: three mutations at 50 % on top, as one Undo step
    if (processor.generateWithMutation)
    {
        mutatorPanel.mutateSeveral (0.5f, 3);
        processor.patchName = processor.patchName + " (mutated)";
    }

    canvas.fitToContent();
}

void AquanodeModularAudioProcessorEditor::runGenerator (const juce::String& generatorId, bool openMenu)
{
    const auto* gen = aquanode::presets::findGenerator (generatorId);
    if (gen == nullptr)
        return;

    if (! openMenu || ! gen->hasMenu())
    {
        // a whole new patch per tap; the seed comes from the system RNG so
        // every tap is different
        auto seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
        applyGenerated (gen->random (seed == 0 ? 1u : seed));
        return;
    }

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safeThis (this);

    aquanode::presets::MenuContext ctx;
    ctx.anchor = &sidebarViewport;
    ctx.selectionChanged = [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->sidebar.repaint();
    };
    ctx.apply = [safeThis] (aquanode::presets::BuildFn build)
    {
        if (safeThis != nullptr)
            safeThis->applyGenerated (std::move (build));
    };
    ctx.pickFile = [safeThis] (const juce::String& title, const juce::String& patterns,
                               std::function<void (juce::MemoryBlock, juce::String)> onLoaded)
    {
        if (safeThis == nullptr)
            return;

        safeThis->fileChooser = std::make_unique<juce::FileChooser> (title, juce::File(), patterns);
        safeThis->fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                            | juce::FileBrowserComponent::canSelectFiles,
            [onLoaded] (const juce::FileChooser& fc)
            {
                juce::MemoryBlock data;
                juce::String name;
               #if JUCE_ANDROID
                // a content:// document has no path: read it as a stream
                const auto url = fc.getURLResult();
                if (url.isEmpty())
                    return;
                auto doc = juce::AndroidDocument::fromDocument (url);
                if (! doc.hasValue())
                    return;
                if (auto in = doc.createInputStream())
                    in->readIntoMemoryBlock (data);
                name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
               #else
                const auto file = fc.getResult();
                if (! file.existsAsFile())
                    return;
                file.loadFileAsData (data);
                name = file.getFileNameWithoutExtension();
               #endif
                if (data.getSize() > 0 && onLoaded != nullptr)
                    onLoaded (std::move (data), name);
            });
    };

    juce::PopupMenu menu;
    gen->fillMenu (menu, ctx);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sidebarViewport)
                                                  .withMousePosition());
}

void AquanodeModularAudioProcessorEditor::updateZoomReadout()
{
    const auto text = juce::String (juce::roundToInt (canvas.getZoom() * 100.0f)) + "%";
    if (zoomReadout.text != text)
    {
        zoomReadout.text = text;
        zoomReadout.repaint();
    }
}
//==============================================================================
// Auto-Connect: what was last added gets wired into the patch. Sound sources
// go in parallel into the Polyphony Sum (Audio Out when there is none);
// effects go in series at the end of the chain, between whatever feeds Audio
// Out and Audio Out. The add paths of the plugins wire their own outputs to
// Audio Out (and an effect's input to Audio In) - those cables are taken as
// the module's outputs / inputs and re-routed.
void AquanodeModularAudioProcessorEditor::autoConnectAdded (const std::vector<int>& before, AutoRole role)
{
    using namespace aquanode;
    std::vector<int> added;
    for (const int id : processor.getModuleIds())
        if (std::find (before.begin(), before.end(), id) == before.end())
            added.push_back (id);
    if (added.empty())
        return;

    const auto isNew = [&] (int id) { return std::find (added.begin(), added.end(), id) != added.end(); };
    const auto findType = [this, &isNew] (const juce::String& type, bool fresh)
    {
        for (const int id : processor.getModuleIds())
            if (auto* inst = processor.getInstance (id))
                if (inst->descriptor().typeId == type && isNew (id) == fresh)
                    return id;
        return -1;
    };

    // the role of a plain module: from its sidebar section
    if (role == AutoRole::FromSection)
    {
        auto* inst = processor.getInstance (added.front());
        if (inst == nullptr) return;
        const auto sec = inst->descriptor().section;
        if (sec == ModuleSection::Oscillator)                                  role = AutoRole::Source;
        else if (sec == ModuleSection::Effect || sec == ModuleSection::Filter) role = AutoRole::Effect;
        else return;   // utilities, MIDI, I/O: patched by hand
    }

    // the patch's own Audio Out / Audio In / Polyphony Sum (not ones the add made)
    int out = findType ("io.audioOut", false);
    const int newOut = findType ("io.audioOut", true);
    const int newIn = findType ("io.audioIn", true);
    if (out < 0 && newOut >= 0) out = newOut;   // the patch had none: keep the new one
    if (out < 0)
        return;
    const int psum = findType ("util.polysum", false);

    // the added modules' outputs (what they cabled into an Audio Out) and
    // inputs (what an added Audio In fed), then those cables go
    std::vector<std::pair<int, juce::String>> outs, ins;
    for (int i = (int) processor.getCables().size(); --i >= 0;)
    {
        const auto c = processor.getCables()[(size_t) i];
        const bool intoOut = (c.toModule == out || c.toModule == newOut) && isNew (c.fromModule);
        const bool fromIn = newIn >= 0 && c.fromModule == newIn && isNew (c.toModule);
        if (intoOut) outs.push_back ({ c.fromModule, c.fromSocket });
        if (fromIn)  ins.push_back ({ c.toModule, c.toSocket });
        if (intoOut || fromIn)
            processor.removeCable (i);
    }
    if (newOut >= 0 && newOut != out) processor.removeModule (newOut);
    if (newIn >= 0)                   processor.removeModule (newIn);

    // a plain module: its first audio output / input
    const auto firstSocket = [this] (int id, SocketDirection dir) -> juce::String
    {
        if (auto* inst = processor.getInstance (id))
            for (const auto& sock : inst->descriptor().sockets)
                if (sock.kind == SocketKind::Audio && sock.direction == dir)
                    return sock.id;
        return {};
    };
    if (outs.empty() && added.size() == 1)
        if (auto sock = firstSocket (added.front(), SocketDirection::Output); sock.isNotEmpty())
            outs.push_back ({ added.front(), sock });
    if (ins.empty() && added.size() == 1 && role == AutoRole::Effect)
        if (auto sock = firstSocket (added.front(), SocketDirection::Input); sock.isNotEmpty())
            ins.push_back ({ added.front(), sock });
    if (outs.empty())
        return;

    if (role == AutoRole::Source || ins.empty())
    {
        // in parallel into the Polyphony Sum
        const int target = psum >= 0 ? psum : out;
        for (const auto& [m, sock] : outs)
            processor.addCable (m, sock, target, "audioIn");
        return;
    }

    // in series at the end: find what feeds Audio Out, preferring the chain
    // that comes from the Polyphony Sum
    std::vector<int> feeders;
    for (const auto& c : processor.getCables())
        if (c.toModule == out && c.toSocket == "audioIn" && ! isNew (c.fromModule))
            feeders.push_back (c.fromModule);
    const auto comesFromSum = [this, psum] (int id)
    {
        for (int step = 0; step < 64 && id >= 0; ++step)
        {
            if (id == psum) return true;
            int up = -1;
            for (const auto& c : processor.getCables())
                if (c.toModule == id)
                    if (auto* inst = processor.getInstance (c.fromModule))
                        if (auto* to = processor.getInstance (id))
                            if (to->descriptor().inputIndexOf (c.toSocket) >= 0) { up = c.fromModule; break; }
            id = up;
        }
        return false;
    };
    int tail = -1;
    for (const int f : feeders)
        if (tail < 0 || (psum >= 0 && comesFromSum (f))) tail = f;

    if (tail >= 0)
    {
        juce::String tailSock;
        for (int i = (int) processor.getCables().size(); --i >= 0;)
        {
            const auto c = processor.getCables()[(size_t) i];
            if (c.fromModule == tail && c.toModule == out)
            {
                tailSock = c.fromSocket;
                processor.removeCable (i);
            }
        }
        for (const auto& [m, sock] : ins)
            processor.addCable (tail, tailSock, m, sock);

        // beside the old tail, so the chain reads left to right
        if (auto* t = processor.getInstance (tail))
            if (auto* first = processor.getInstance (ins.front().first))
            {
                const auto shift = t->position + juce::Point<int> (300, 0) - first->position;
                for (const int id : added)
                    if (auto* inst = processor.getInstance (id))
                        processor.setModulePosition (id, inst->position + shift);
                if (auto* o = processor.getInstance (out))
                    processor.setModulePosition (out, o->position + juce::Point<int> (300, 0));
            }
    }
    for (const auto& [m, sock] : outs)
        processor.addCable (m, sock, out, "audioIn");
}
