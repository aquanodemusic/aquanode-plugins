#include "PresetCommon.h"
#include "../PluginProcessor.h"

namespace aquanode::presets
{

//==============================================================================
// card size estimate - mirrors ModuleComponent::layoutEverything()
//==============================================================================
namespace
{
    constexpr int kPad = 10, kSocketSlotH = 30, kHeaderMinH = 52, kKnobRowH = 82, kWideRowH = 32, kBarRowH = 40;
    constexpr int kDefaultWidth = 250;

    bool paramVisible (const ModuleInstance& inst, const ParamSpec& spec)
    {
        if (spec.visibleWhenParamId.isEmpty())
            return true;
        return std::abs (inst.dsp->getParameter (spec.visibleWhenParamId) - spec.visibleWhenEquals) < 0.5f;
    }
}

int estimateCardWidth (const ModuleInstance& inst)
{
    const int w = inst.dsp != nullptr ? inst.dsp->preferredModuleWidth() : 0;
    return w > 0 ? w : kDefaultWidth;
}

int estimateCardHeight (const ModuleInstance& inst)
{
    const auto& desc = inst.descriptor();
    int y = kPad;
    const int headerRows = juce::jmax (desc.numInputs(), desc.numOutputs());
    y += juce::jmax (kHeaderMinH, headerRows * kSocketSlotH + kPad) + 2;

    if (inst.collapsed)
        return y + kPad - 4;

    std::map<int, int> rows;   // row index -> 0 combos/buttons only, 1 bars, 2 knobs
    for (const auto& spec : desc.params)
    {
        if (spec.hidden || ! paramVisible (inst, spec))
            continue;
        const int kind = spec.type == ParamType::Rotary || spec.type == ParamType::RotarySteppedList ? 2
                       : spec.type == ParamType::HBar ? 1 : 0;
        rows[spec.row] = juce::jmax (rows[spec.row], kind);
    }
    for (const auto& r : rows)
        y += (r.second == 2 ? kKnobRowH : r.second == 1 ? kBarRowH : kWideRowH) + 2;

    if (inst.dsp != nullptr)
        if (auto extra = inst.dsp->extraContentHeight(); extra > 0)
            y += extra + 4;

    return y + kPad - 2;
}

//==============================================================================
juce::int64 scrambleSeed (juce::uint32 x)
{
    x += 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
    x = (x ^ (x >> 13)) * 0xC2B2AE35u;
    x ^= x >> 16;
    return (juce::int64) x;
}

//==============================================================================
// PatchSketch
//==============================================================================
int PatchSketch::add (const juce::String& typeId, int column)
{
    const int id = p.addModule (typeId, { 0, 0 });
    jassert (id >= 0);   // an unknown typeId is a bug in the generator
    if (id >= 0)
        entries.push_back ({ id, column, false });
    return id;
}

void PatchSketch::set (int id, const juce::String& paramId, float value)
{
    if (auto* inst = p.getInstance (id))
        if (const auto* spec = inst->descriptor().findParam (paramId))
            inst->dsp->setParameter (paramId, juce::jlimit (spec->minValue, spec->maxValue, value));
}

bool PatchSketch::setChoice (int id, const juce::String& paramId, const juce::String& choice)
{
    if (auto* inst = p.getInstance (id))
        if (const auto* spec = inst->descriptor().findParam (paramId))
        {
            const int idx = spec->choices.indexOf (choice, true);
            if (idx >= 0)
            {
                inst->dsp->setParameter (paramId, (float) idx);
                return true;
            }
        }
    jassertfalse;   // no such choice: a typo in a generator
    return false;
}

float PatchSketch::get (int id, const juce::String& paramId) const
{
    if (auto* inst = p.getInstance (id))
        return inst->dsp->getParameter (paramId);
    return 0.0f;
}

bool PatchSketch::wire (int from, const juce::String& out, int to, const juce::String& in)
{
    if (from < 0 || to < 0)
        return false;
    const bool ok = p.addCable (from, out, to, in);
    jassert (ok);
    return ok;
}

bool PatchSketch::mod (int from, const juce::String& out, int to, const juce::String& paramId, float depth)
{
    if (from < 0 || to < 0)
        return false;
    const bool ok = p.addParamCable (from, out, to, paramId, depth);
    jassert (ok);
    return ok;
}

void PatchSketch::collapse (int id, bool shouldCollapse)
{
    if (auto* inst = p.getInstance (id))
        inst->collapsed = shouldCollapse;
}

SynthModule* PatchSketch::dsp (int id) const
{
    if (auto* inst = p.getInstance (id))
        return inst->dsp.get();
    return nullptr;
}

void PatchSketch::place (int id, juce::Point<int> pos)
{
    p.setModulePosition (id, pos);
    for (auto& e : entries)
        if (e.id == id)
            e.placed = true;
}

void PatchSketch::layout (int maxColumnHeight, juce::Point<int> origin)
{
    constexpr int gapX = 40, gapY = 30;

    int maxColumn = -1;
    for (const auto& e : entries)
        if (! e.placed)
            maxColumn = juce::jmax (maxColumn, e.column);

    // hand-placed modules (an algorithm chart) push the columns to their right
    int x = origin.x;
    for (const auto& e : entries)
        if (e.placed)
            if (auto* inst = p.getInstance (e.id))
                x = juce::jmax (x, inst->position.x + estimateCardWidth (*inst) + gapX);

    for (int c = 0; c <= maxColumn; ++c)
    {
        int y = origin.y, colW = 0;
        bool any = false;
        for (const auto& e : entries)
        {
            if (e.placed || e.column != c)
                continue;
            auto* inst = p.getInstance (e.id);
            if (inst == nullptr)
                continue;

            const int h = estimateCardHeight (*inst);
            if (y > origin.y && y + h > origin.y + maxColumnHeight)
            {
                // the chain carries on in a fresh column
                x += colW + gapX;
                y = origin.y;
                colW = 0;
            }
            p.setModulePosition (e.id, { x, y });
            y += h + gapY;
            colW = juce::jmax (colW, estimateCardWidth (*inst));
            any = true;
        }
        if (any)
            x += colW + gapX;
    }
}

} // namespace aquanode::presets
