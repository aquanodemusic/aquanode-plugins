#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace aquanova {

//==============================================================================
AquaNovaProcessor::AquaNovaProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AQUANOVA", createParameterLayout())
{
    engine.params.attach (apvts);
}

//==============================================================================
void AquaNovaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

bool AquaNovaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo()
                           || in == juce::AudioChannelSet::mono();
}

void AquaNovaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (auto* ph = getPlayHead())
    {
        if (auto info = ph->getPosition())
        {
            if (auto bpm = info->getBpm())
                engine.setTempo (*bpm);

            // When the host stops the transport, stop making noise immediately
            // rather than letting releases and reverb tails ring on.
            const bool playing = info->getIsPlaying();

            if (wasPlaying && ! playing)
            {
                engine.panic();
                buffer.clear();
            }

            wasPlaying = playing;
        }
    }

    // Injects any notes queued by mouse clicks on the on-screen keyboard into
    // the buffer, and updates its own note-on state from whatever ends up in
    // it - host input included - so the keyboard lights up for both sources.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    // Same idea for the on-screen mod wheel: turn a UI move into an ordinary
    // CC1 event so it reaches the engine on the same path a hardware wheel's
    // MIDI would, rather than needing its own separate hook into the voices.
    const float modWheelNow = uiModWheel.load();

    if (std::abs (modWheelNow - lastSentModWheel) > 0.0001f)
    {
        lastSentModWheel = modWheelNow;
        const auto ccValue = (juce::uint8) juce::jlimit (0, 127, juce::roundToInt (modWheelNow * 127.0f));
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, ccValue), 0);
    }

    engine.process (buffer, midi);
    midi.clear();
}

juce::AudioProcessorEditor* AquaNovaProcessor::createEditor()
{
    return new AquaNovaEditor (*this);
}

//==============================================================================
void AquaNovaProcessor::changeProgramName (int, const juce::String& newName)
{
    setPatchName (newName);
}

void AquaNovaProcessor::setPatchName (const juce::String& n)
{
    patchName = n.isEmpty() ? "Init" : n;

    if (onPatchChanged)
        onPatchChanged();
}

//==============================================================================
void AquaNovaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("patchName", patchName, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AquaNovaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.isValid() && tree.hasType (apvts.state.getType()))
        {
            patchName = tree.getProperty ("patchName", "Init").toString();
            apvts.replaceState (tree);

            if (onPatchChanged)
                onPatchChanged();
        }
    }
}

//==============================================================================
bool AquaNovaProcessor::savePatch (const juce::File& file)
{
    // The file name wins: a patch saved as "Deep Bass.aquanova" is called
    // "Deep Bass", whatever it happened to be called before.
    setPatchName (file.getFileNameWithoutExtension());

    auto state = apvts.copyState();
    state.setProperty ("patchName", patchName, nullptr);

    if (auto xml = state.createXml())
        return xml->writeTo (file);

    return false;
}

bool AquaNovaProcessor::loadPatch (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return false;

    apvts.replaceState (tree);

    // Likewise on the way back in - the file is the patch.
    patchName = file.getFileNameWithoutExtension();

    if (onPatchChanged)
        onPatchChanged();

    return true;
}

} // namespace aquanova

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aquanova::AquaNovaProcessor();
}
