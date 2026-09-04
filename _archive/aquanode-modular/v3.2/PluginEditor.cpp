#include "PluginEditor.h"

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
    setSize (moduleWidth, h);

    // Bounds are WORLD coordinates; pan and zoom live in the transform that
    // the canvas pushes onto every module (see PatchCanvas::applyViewToChildren).
    if (auto* inst = processor.getInstance (instanceId))
        setTopLeftPosition (inst->position);
    setTransform (canvas.worldToScreen());

    layoutEverything (true);
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
                }

                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 17);
                slider->setDoubleClickReturnValue (true, spec.defaultValue);   // double-click resets
                slider->setValue (dsp->getParameter (spec.id), juce::dontSendNotification);

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
                const float hrange = spec.maxValue - spec.minValue;
                slider->setNumDecimalPlacesToDisplay (hrange > 20.0f ? 1 : 2);
                slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 16);
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
                button->onClick = [weakDsp, paramId = spec.id]
                {
                    if (auto* m = weakDsp.get())
                        m->uiButtonClicked (paramId);
                };
                entry.component = std::move (button);
                break;
            }
        }

        // right-clicks on modulatable knobs open the modulation menu
        if ((spec.type == ParamType::Rotary || spec.type == ParamType::HBar) && spec.modulatable)
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
    }
}

int ModuleComponent::layoutEverything (bool apply)
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return 40;

    const auto& desc = inst->descriptor();
    const int innerW = moduleWidth - 2 * layout::pad;
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
        const int rightX = moduleWidth - layout::pad - layout::socketSize / 2 - 2;

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
        bool hasKnob = false;
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
                    if (c.spec->type == ParamType::Rotary || c.spec->type == ParamType::RotarySteppedList
                        || c.spec->type == ParamType::HBar)
                        hasKnob = true;
                }
            }
        }

        if (rowControls.empty())
            continue;

        const int rowH = hasKnob ? layout::knobRowH : layout::wideRowH;
        const int unitW = innerW / layout::unitsPerRow;

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
    return juce::Rectangle<int> (moduleWidth / 2 - 15, 3, 30, 6);
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
    setSize (moduleWidth, h);
    layoutEverything (true);
    repaint();
    canvas.moduleMoved();
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
    const auto body = sectionColour (desc.section);
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
    g.drawText (desc.displayName + inst->dsp->titleSuffix(),
                layout::socketColW, layout::pad,
                getWidth() - 2 * layout::socketColW, headerHeight,
                juce::Justification::centred, true);

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

    return getPosition() + juce::Point<int> (moduleWidth / 2, headerHeight / 2);
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
    // slow fine-adjust is a completely normal gesture and must never pop a
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
// afford it. Visually the grid just coarsens as you pull back, which is what a
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
            // the cable still exists and still modulates, it just has nowhere
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

        // cable colour always follows the source module's section colour
        auto colour = sectionColour (src->descriptor().section);
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

void PatchCanvas::mouseDown (const juce::MouseEvent& e)
{
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
    if (! panning)
        return;

    panOffset = panStartOffset + (e.getPosition() - panMouseStart);
    applyViewToChildren();
    repaint();
}

void PatchCanvas::mouseUp (const juce::MouseEvent&)
{
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
    // (and more recent) click, so Delete removes just the cable
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
        if (! dragFromInput)
        {
            if (const auto* spec = m->findModulatableKnobNear (localPos))
            {
                processor.addParamCable (dragModuleId, dragSocketId,
                                         m->getInstanceId(), spec->id,
                                         spec->hiddenCableTarget ? 1.0f : 0.3f);
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
SidebarComponent::SidebarComponent (std::function<void (const juce::String&)> onModuleClicked)
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
        if (row.typeId == hoveredTypeId)
        {
            showTooltipFor (row);
            return;
        }
}

void SidebarComponent::showTooltipFor (const Row& row)
{
    auto* reg = ModuleFactory::instance().find (row.typeId);
    if (reg == nullptr || reg->descriptor.description.isEmpty())
        return;

    auto* top = getTopLevelComponent();
    if (top == nullptr)
        return;

    tooltip = std::make_unique<SidebarTooltip>();
    tooltip->setText (reg->descriptor.displayName, reg->descriptor.description,
                      juce::jmin (340, juce::jmax (200, top->getWidth() - sidebarWidth - 24)));

    // sit just right of the sidebar, vertically centred on the row, and keep
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

    for (const auto section : allSections())
    {
        // section title
        rows.push_back ({ { 10, y, contentWidth - 20, 20 },
                          sectionName (section), {}, sectionColour (section) });
        y += 24;

        // the categories keep their fixed running order (I/O first, then sound
        // generation, ...) but the modules inside each are listed A-Z
        std::vector<const RegisteredModule*> plain, spectral;
        for (const auto& r : ModuleFactory::instance().all())
        {
            if (r.descriptor.section != section)
                continue;
            // the spectral effects keep their own sub-heading within Effect
            (r.descriptor.typeId.startsWith ("fx.spec") ? spectral : plain).push_back (&r);
        }

        auto byName = [] (const RegisteredModule* a, const RegisteredModule* b)
        {
            return a->descriptor.displayName.compareIgnoreCase (b->descriptor.displayName) < 0;
        };
        std::sort (plain.begin(), plain.end(), byName);
        std::sort (spectral.begin(), spectral.end(), byName);

        auto emitRow = [&] (const RegisteredModule* r)
        {
            rows.push_back ({ { 18, y, contentWidth - 28, 18 },
                              r->descriptor.displayName, r->descriptor.typeId, juce::Colours::white });
            y += 20;
        };

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

        y += 10;
    }

    // size ourselves to fit every row; the parent Viewport takes care of clipping + scrolling
    setSize (contentWidth, y);
}

void SidebarComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    for (const auto& row : rows)
    {
        const bool isTitle = row.typeId.isEmpty();
        g.setColour (row.colour);
        g.setFont (juce::Font (juce::FontOptions (isTitle ? 14.0f : 12.5f,
                                                  isTitle ? juce::Font::bold : juce::Font::plain)));
        g.drawText (row.text, row.area, juce::Justification::centredLeft);
    }
}

void SidebarComponent::mouseUp (const juce::MouseEvent& e)
{
    for (const auto& row : rows)
    {
        if (row.typeId.isNotEmpty() && row.area.contains (e.getPosition()))
        {
            // the module is being added; the description has served its purpose
            hideTooltip();
            hoverStartMs = juce::Time::getMillisecondCounter();

            if (moduleClicked != nullptr)
                moduleClicked (row.typeId);
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
    const float amount = (float) amountSlider.getValue() * 0.01f;

    for (const int id : processor.getModuleIds())
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr || inst->descriptor().section == ModuleSection::InputOutput)
            continue;   // never mutate Audio In/Out levels

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

    canvas.refreshAllModuleValues();
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
      sidebar ([this] (const juce::String& typeId)
      {
          // clicking a sidebar name opens one instance in the patch map
          // spawn into the currently visible area, regardless of pan
          const auto viewSpawn = juce::Point<int> (40 + (placementCounter % 4) * 36,
                                                   40 + (placementCounter % 6) * 30);
          const auto pos = viewSpawn - canvas.getPanOffset();
          ++placementCounter;
          const int newId = processor.addModule (typeId, pos);
          canvas.rebuild();
          canvas.selectModule (newId);
      }),
      canvas (p),
      mutatorPanel (p, canvas)
{
    addAndMakeVisible (sidebarViewport);
    sidebarViewport.setViewedComponent (&sidebar, false);   // false: sidebar is a member, viewport must not delete it
    sidebarViewport.setScrollBarsShown (true, false);        // vertical only
    addAndMakeVisible (canvas);
    addChildComponent (mutatorPanel);   // hidden until the Mutator button toggles it
    addAndMakeVisible (toolbarStrip);   // added before the buttons: they draw on top of it

    // On-screen keyboard: driven by (and mirrored onto) the processor's shared
    // MidiKeyboardState. Only visible / laid out on Android.
    midiKeyboard = std::make_unique<juce::MidiKeyboardComponent> (
                       processor.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
    midiKeyboard->setOctaveForMiddleC (4);
   #if JUCE_ANDROID
    addAndMakeVisible (*midiKeyboard);
   #endif

    for (auto* b : { &initButton, &cloneButton, &deleteButton, &exportButton, &importButton,
                     &mutatorButton, &sidebarButton })
        addAndMakeVisible (b);

   #if JUCE_ANDROID
    addAndMakeVisible (keysButton);
   #endif

    for (auto* b : { &zoomOutButton, &zoomResetButton, &zoomInButton })
        addAndMakeVisible (b);

    sidebarButton.onClick = [this]
    {
        sidebarVisible = ! sidebarVisible;
        sidebarViewport.setVisible (sidebarVisible);
        resized();
    };

    keysButton.onClick = [this]
    {
        keyboardVisible = ! keyboardVisible;
        if (midiKeyboard != nullptr)
            midiKeyboard->setVisible (keyboardVisible);
        resized();
    };

    zoomInButton .onClick = [this] { canvas.zoomBy (1.25f); };
    zoomOutButton.onClick = [this] { canvas.zoomBy (1.0f / 1.25f); };
    zoomResetButton.onClick = [this] { canvas.resetView(); };

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
                        processor.importPatchFromStream (*in);
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

   #if JUCE_ANDROID
    // Bottom on-screen keyboard, height scaled to the screen (clamped sane).
    const int kbH = juce::jlimit (110, 220, getHeight() / 4);
    if (midiKeyboard != nullptr && keyboardVisible)
    {
        auto kbArea = area.removeFromBottom (kbH);
        midiKeyboard->setKeyWidth (juce::jmax (26.0f, kbArea.getWidth() / 30.0f));
        midiKeyboard->setBounds (kbArea);
    }

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

    // mutator strip along the bottom of the patch area (above the keyboard on Android)
    mutatorPanel.setBounds (area.getX(), area.getBottom() - 34, area.getWidth(), 34);

    // Toolbar strip along the top of the patch area, laid out the same way as
    // the mutator strip: it spans exactly the patch region, so nothing can
    // reach across the sidebar, and the buttons split the width evenly so all
    // six stay visible however narrow the screen gets.
    const int stripH = topY + bh + topY;
    const auto strip = juce::Rectangle<int> (area.getX(), area.getY(), area.getWidth(), stripH);
    toolbarStrip.setBounds (strip);

    {
        const auto bar = strip.reduced (gap, topY);   // editor coordinates

        std::vector<juce::TextButton*> buttons {
            &mutatorButton, &importButton, &exportButton,
            &deleteButton,  &cloneButton,  &initButton
        };

        // Panel toggles sit at the far left of the strip, past the six patch
        // buttons, so the order everyone already knows is untouched.
        buttons.push_back (&sidebarButton);
       #if JUCE_ANDROID
        buttons.push_back (&keysButton);
       #endif

        const int n = (int) buttons.size();
        const int bw = (bar.getWidth() - (n - 1) * gap) / n;

        // Right to left, so the button order on screen is unchanged.
        for (int i = 0; i < n; ++i)
            buttons[(size_t) i]->setBounds (bar.getRight() - bw - i * (bw + gap),
                                            bar.getY(), bw, bar.getHeight());
    }

    // Floating zoom cluster, bottom-right of the patch area and lifted clear of
    // the mutator strip so the two can never collide when the Mutator is open.
    {
       #if JUCE_ANDROID
        const int zw = 54, zh = 38, zgap = 6;
       #else
        const int zw = 38, zh = 24, zgap = 4;
       #endif
        const int right  = area.getRight() - zgap;
        const int bottom = area.getBottom() - 34 - zgap;

        zoomInButton   .setBounds (right - zw,                    bottom - zh, zw, zh);
        zoomResetButton.setBounds (right - zw * 2 - zgap,         bottom - zh, zw, zh);
        zoomOutButton  .setBounds (right - zw * 3 - zgap * 2,     bottom - zh, zw, zh);
    }

    updateZoomReadout();
}

void AquanodeModularAudioProcessorEditor::updateZoomReadout()
{
    const auto text = juce::String (juce::roundToInt (canvas.getZoom() * 100.0f)) + "%";
    if (zoomResetButton.getButtonText() != text)
        zoomResetButton.setButtonText (text);
}