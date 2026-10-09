#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace aquavibrio
{

AquaVibrioProcessor::AquaVibrioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",   juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)
          // The Virus has a second pair of outputs, and Second Output Balance
          // decides how much of the patch goes there. The host may leave this
          // bus disabled, which is fine - the engine checks.
          .withOutput ("Output 2", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "AquaVibrio", createParameterLayout())
{
}

void AquaVibrioProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    engine.bindTo (apvts);
    engine.prepare (sampleRate, blockSize);
}

bool AquaVibrioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto second = layouts.getChannelSet (false, 1);
    return second.isDisabled() || second == juce::AudioChannelSet::stereo();
}

void AquaVibrioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Merges notes played on the on-screen keyboard into the same buffer the
    // host's MIDI arrives in, and - just by scanning that buffer - lets the
    // keyboard light up for notes played from the host or a real controller
    // too, so the widget is a two-way window onto whatever the engine hears.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                engine.setTempo (*bpm);

    // Deliberately NOT cleared: the engine reads each input sample for the
    // vocoder and the input follower before writing its own output over it.
    engine.setSecondOutput (getBusCount (false) > 1 && getBus (false, 1) != nullptr
                              && getBus (false, 1)->isEnabled()
                              ? getBusBuffer (buffer, false, 1).getArrayOfWritePointers()
                              : nullptr);

    engine.renderBlock (buffer, midi);
}

juce::AudioProcessorEditor* AquaVibrioProcessor::createEditor()
{
    return new AquaVibrioEditor (*this);
}

//==============================================================================
void AquaVibrioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AquaVibrioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
int AquaVibrioProcessor::loadBank (const juce::File& file)
{
    presetFiles.clear();
    presetNames.clear();
    currentPatch = -1;

    const auto dir = file.isDirectory() ? file : file.getParentDirectory();
    if (! dir.isDirectory())
        return 0;

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();

    int selectIndex = -1;

    for (auto& f : files)
    {
        const auto json = f.loadFileAsString();
        const auto parsed = juce::JSON::parse (json);

        // Only files that actually look like our presets go in the list -
        // an unrelated .json file living in the same folder is silently
        // skipped rather than corrupting the bank.
        if (! parsed.isObject() || parsed.getProperty ("format", {}).toString() != "AquaVibrioPreset")
            continue;

        if (! file.isDirectory() && f == file)
            selectIndex = (int) presetFiles.size();

        presetNames.add (presetNameFromJson (json, f));
        presetFiles.push_back (f);
    }

    if (! presetFiles.empty())
        selectPatch (selectIndex >= 0 ? selectIndex : 0);

    return (int) presetFiles.size();
}

void AquaVibrioProcessor::selectPatch (int index)
{
    if (index < 0 || index >= (int) presetFiles.size())
        return;

    const auto json = presetFiles[(size_t) index].loadFileAsString();
    if (applyJsonPreset (json, apvts))
    {
        currentPatch = index;
        engine.allNotesOff();
    }
}

void AquaVibrioProcessor::saveCurrentPatch (const juce::File& file, const juce::String& name)
{
    const auto json = presetToJson (apvts, name);
    file.replaceWithText (json);
}

juce::String AquaVibrioProcessor::getPatchName (int index) const
{
    return index >= 0 && index < presetNames.size() ? presetNames[index] : juce::String();
}

juce::String AquaVibrioProcessor::getCurrentPatchName() const
{
    return getPatchName (currentPatch);
}

} // namespace aquavibrio

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aquavibrio::AquaVibrioProcessor();
}
