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

    if (auto* inst = processor.getInstance (instanceId))
        setTopLeftPosition (inst->position);

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
                        const int i = choices.indexOf (t.trim());
                        return i >= 0 ? (double) i : t.getDoubleValue();
                    };
                }
                else
                {
                    if (spec.unitSuffix.isNotEmpty())
                        slider->setTextValueSuffix (" " + spec.unitSuffix);
                    const float range = spec.maxValue - spec.minValue;
                    slider->setNumDecimalPlacesToDisplay (spec.interval == 1.0f ? 0
                                                          : range > 1000.0f ? 0
                                                          : range > 20.0f ? 1 : 2);
                }

                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 17);
                slider->setValue (dsp->getParameter (spec.id), juce::dontSendNotification);
                slider->onValueChange = [weakDsp, paramId = spec.id, s = slider.get()]
                {
                    if (auto* m = weakDsp.get())
                        m->setParameter (paramId, (float) s->getValue());
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

        addAndMakeVisible (entry.component.get());
        controls.push_back (std::move (entry));
    }

    extraContent = dsp->createExtraContentComponent();
    if (extraContent != nullptr)
        addAndMakeVisible (extraContent.get());
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
                    if (c.spec->type == ParamType::Rotary || c.spec->type == ParamType::RotarySteppedList)
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

                if (c->spec->type == ParamType::Rotary || c->spec->type == ParamType::RotarySteppedList)
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

void ModuleComponent::refreshLayout()
{
    const int h = layoutEverything (false);
    setSize (moduleWidth, h);
    layoutEverything (true);
    repaint();
    canvas.moduleMoved();
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

    // module title, centred in the header column between the socket columns
    g.setColour (juce::Colours::black);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText (desc.displayName,
                layout::socketColW, layout::pad,
                getWidth() - 2 * layout::socketColW, headerHeight,
                juce::Justification::centred, true);

    // sockets: circular = Audio, square = Modulation, label beside the column
    for (const auto& s : sockets)
    {
        const auto c = s.centre.toFloat();
        const float r = layout::socketSize * 0.5f;

        g.setColour (juce::Colours::black);
        if (s.kind == SocketKind::Audio)
            g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        else
            g.fillRect (c.x - r, c.y - r, 2 * r, 2 * r);

        g.setColour (juce::Colours::white);
        if (s.kind == SocketKind::Audio)
            g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.3f);
        else
            g.drawRect (c.x - r, c.y - r, 2 * r, 2 * r, 1.3f);

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

void ModuleComponent::mouseDown (const juce::MouseEvent& e)
{
    canvas.selectModule (instanceId);

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
    if (draggingCable)
    {
        canvas.updateCableDrag (e.getEventRelativeTo (&canvas).getPosition());
        return;
    }

    if (draggingBody)
    {
        dragger.dragComponent (this, e, nullptr);
        setTopLeftPosition (juce::jmax (0, getX()), juce::jmax (0, getY()));
        processor.setModulePosition (instanceId, getPosition());
        canvas.moduleMoved();
    }
}

void ModuleComponent::mouseUp (const juce::MouseEvent& e)
{
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

void PatchCanvas::resized()
{
    // decorative "+" grid, rebuilt once per resize
    gridPath.clear();
    for (int x = layout::gridSpacing; x < getWidth(); x += layout::gridSpacing)
    {
        for (int y = layout::gridSpacing; y < getHeight(); y += layout::gridSpacing)
        {
            gridPath.startNewSubPath ((float) x - 3.0f, (float) y);
            gridPath.lineTo ((float) x + 3.0f, (float) y);
            gridPath.startNewSubPath ((float) x, (float) y - 3.0f);
            gridPath.lineTo ((float) x, (float) y + 3.0f);
        }
    }
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

void PatchCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2723));            // warm dark brown-grey patch map
    g.setColour (juce::Colour (0xff3c3730));          // subtle warmer "+" grid
    g.strokePath (gridPath, juce::PathStrokeType (1.0f));
}

void PatchCanvas::paintOverChildren (juce::Graphics& g)
{
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

    const int hit = cableIndexNear (e.position);
    selectedCableIndex = hit;
    if (hit >= 0)
        selectedModuleId = -1;   // cable selection replaces module selection
    else
        selectedModuleId = -1;   // click on empty canvas deselects

    repaint();
}

void PatchCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-clicking a cable removes it
    const int hit = cableIndexNear (e.position);
    if (hit >= 0)
    {
        processor.removeCable (hit);
        selectedCableIndex = -1;
        repaint();
    }
}

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && selectedCableIndex >= 0)
    {
        processor.removeCable (selectedCableIndex);
        selectedCableIndex = -1;
        repaint();
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
    dragCurrentPos = canvasPos;
    repaint();
}

void PatchCanvas::endCableDrag (juce::Point<int> canvasPos)
{
    dragActive = false;

    for (const auto& m : moduleComponents)
    {
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
    }

    repaint();
}

//==============================================================================
// SidebarComponent
//==============================================================================
SidebarComponent::SidebarComponent (std::function<void (const juce::String&)> onModuleClicked)
    : moduleClicked (std::move (onModuleClicked))
{
    buildRows();
}

void SidebarComponent::buildRows()
{
    rows.clear();
    int y = 10;

    for (const auto section : allSections())
    {
        // section title
        rows.push_back ({ { 10, y, sidebarWidth - 20, 20 },
                          sectionName (section), {}, sectionColour (section) });
        y += 24;

        // module names in explicit sidebarOrder (never map/alphabetical order)
        std::vector<const RegisteredModule*> mods;
        for (const auto& r : ModuleFactory::instance().all())
            if (r.descriptor.section == section)
                mods.push_back (&r);

        std::sort (mods.begin(), mods.end(), [] (const RegisteredModule* a, const RegisteredModule* b)
        {
            return a->descriptor.sidebarOrder < b->descriptor.sidebarOrder;
        });

        for (const auto* r : mods)
        {
            rows.push_back ({ { 18, y, sidebarWidth - 28, 18 },
                              r->descriptor.displayName, r->descriptor.typeId, juce::Colours::white });
            y += 20;
        }

        y += 10;
    }
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
            if (moduleClicked != nullptr)
                moduleClicked (row.typeId);
            return;
        }
    }
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
          const auto pos = juce::Point<int> (40 + (placementCounter % 4) * 36,
                                             40 + (placementCounter % 6) * 30);
          ++placementCounter;
          const int newId = processor.addModule (typeId, pos);
          canvas.rebuild();
          canvas.selectModule (newId);
      }),
      canvas (p)
{
    addAndMakeVisible (sidebar);
    addAndMakeVisible (canvas);

    for (auto* b : { &cloneButton, &deleteButton, &exportButton, &importButton })
        addAndMakeVisible (b);

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
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file != juce::File())
                    processor.exportPatchToZip (file.withFileExtension ("zip"));
            });
    };

    importButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Import Patch", juce::File(), "*.zip");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    processor.importPatchFromZip (file);
                    canvas.rebuild();
                }
            });
    };

    processor.addChangeListener (this);

    setSize (1000, 700);
    setResizable (false, false);

    canvas.rebuild();
}

AquanodeModularAudioProcessorEditor::~AquanodeModularAudioProcessorEditor()
{
    processor.removeChangeListener (this);
}

void AquanodeModularAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    canvas.rebuild();
}

void AquanodeModularAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void AquanodeModularAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    sidebar.setBounds (area.removeFromLeft (SidebarComponent::sidebarWidth));
    canvas.setBounds (area);

    // top-right corner, always visible, plain rectangular buttons
    const int bw = 110, bh = 22, gap = 6;
    int x = getWidth() - gap - bw;
    for (auto* b : { &importButton, &exportButton, &deleteButton, &cloneButton })
    {
        b->setBounds (x, 8, bw, bh);
        x -= bw + gap;
    }
}
