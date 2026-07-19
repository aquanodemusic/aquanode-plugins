#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "numGrains", "Number of Grains", 1, 32, 8));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainSize", "Grain Size", // min max stepsize skew default
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.4f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainPosition", "Grain Position", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "spray", "Position Spray",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.4f), 0.05f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "windowSize", "Window Size", 0.01f, 1.0f, 1.0f));

    // Grain ADSR (affects individual grains)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainAttack", "Grain Attack",
        juce::NormalisableRange<float>(0.001f, 0.5f, 0.001f, 0.3f), 0.01f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainDecay", "Grain Decay",
        juce::NormalisableRange<float>(0.001f, 1.0f, 0.001f, 0.3f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainSustain", "Grain Sustain", 0.0f, 1.0f, 0.7f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainRelease", "Grain Release",
        juce::NormalisableRange<float>(0.001f, 1.0f, 0.001f, 0.3f), 0.3f));

    // Note ADSR (affects entire note press/mouse drag)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteAttack", "Note Attack",
        juce::NormalisableRange<float>(0.001f, 1.0f, 0.001f, 0.3f), 0.01f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteDecay", "Note Decay",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteSustain", "Note Sustain", 0.0f, 1.0f, 0.7f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteRelease", "Note Release",
        juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.3f), 0.3f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "amplitudeMod", "Amplitude Modulation", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "amDispersion", "AM Dispersion", 0.0f, 1.0f, 0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "pitchDispersion", "Pitch Dispersion", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "pitch", "Pitch", -24.0f, 24.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "stereoSpread", "Stereo Spread", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "volume", "Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f, 2.0f), 0.7f));

    return layout;
}

//==============================================================================
GranulateAudioProcessor::GranulateAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#endif
    parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    grainVoices.resize(32);
    rng.seed(std::random_device{}());

    for (auto& noteState : noteStates)
        noteState.store(false);
}

GranulateAudioProcessor::~GranulateAudioProcessor()
{
}

//==============================================================================
const juce::String GranulateAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GranulateAudioProcessor::acceptsMidi() const
{
    return true;
}

bool GranulateAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool GranulateAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double GranulateAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int GranulateAudioProcessor::getNumPrograms()
{
    return 1;
}

int GranulateAudioProcessor::getCurrentProgram()
{
    return 0;
}

void GranulateAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String GranulateAudioProcessor::getProgramName(int index)
{
    return {};
}

void GranulateAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void GranulateAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    grainTimer = 0.0f;
    noteEnvelopeTimer = 0.0f;
    noteReleased = false;

    for (auto& voice : grainVoices)
        voice.isActive = false;

    for (auto& noteState : noteStates)
        noteState.store(false);

    anyNoteActive.store(false);
}

void GranulateAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool GranulateAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

float GranulateAudioProcessor::getGrainEnvelope(float phase, const GrainVoice& voice)
{
    // Use grain-specific ADSR values (with randomization applied)
    float attack = voice.grainAttack;
    float decay = voice.grainDecay;
    float sustain = voice.grainSustain;
    float release = voice.grainRelease;

    float totalTime = attack + decay + release;
    float sustainTime = 0.5f;  // Longer sustain time for grains
    totalTime += sustainTime;

    float attackEnd = attack / totalTime;
    float decayEnd = (attack + decay) / totalTime;
    float sustainEnd = (attack + decay + sustainTime) / totalTime;

    if (phase < attackEnd)
        return phase / attackEnd;
    else if (phase < decayEnd)
    {
        float decayPhase = (phase - attackEnd) / (decayEnd - attackEnd);
        return 1.0f - (1.0f - sustain) * decayPhase;
    }
    else if (phase < sustainEnd)
        return sustain;
    else
    {
        float releasePhase = (phase - sustainEnd) / (1.0f - sustainEnd);
        return sustain * (1.0f - releasePhase);
    }
}

float GranulateAudioProcessor::getNoteEnvelope(float phase, bool released)
{
    auto* attackParam = parameters.getRawParameterValue("noteAttack");
    auto* decayParam = parameters.getRawParameterValue("noteDecay");
    auto* sustainParam = parameters.getRawParameterValue("noteSustain");
    auto* releaseParam = parameters.getRawParameterValue("noteRelease");

    float attack = attackParam->load();
    float decay = decayParam->load();
    float sustain = sustainParam->load();
    float release = releaseParam->load();

    if (released)
    {
        // In release phase
        return sustain * (1.0f - phase);
    }
    else
    {
        // Attack -> Decay -> Sustain
        float totalTime = attack + decay;

        if (totalTime < 0.001f)
            return sustain;

        float attackEnd = attack / totalTime;

        if (phase < attackEnd)
            return phase / attackEnd;
        else if (phase < 1.0f)
        {
            float decayPhase = (phase - attackEnd) / (1.0f - attackEnd);
            return 1.0f - (1.0f - sustain) * decayPhase;
        }
        else
            return sustain;
    }
}

void GranulateAudioProcessor::processMidiMessages(juce::MidiBuffer& midiMessages)
{
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            int note = message.getNoteNumber();
            noteStates[note].store(true);
            anyNoteActive.store(true);
            mousePressed.store(false); // Exit mouse mode
            noteReleased = false;
            noteEnvelopeTimer = 0.0f;
        }
        else if (message.isNoteOff())
        {
            int note = message.getNoteNumber();
            noteStates[note].store(false);

            // Check if any notes are still active
            bool anyActive = false;
            for (const auto& state : noteStates)
            {
                if (state.load())
                {
                    anyActive = true;
                    break;
                }
            }
            anyNoteActive.store(anyActive);

            if (!anyActive)
            {
                noteReleased = true;
                noteEnvelopeTimer = 0.0f;  // Start release timer
            }
        }
    }
}

void GranulateAudioProcessor::triggerGrain(int voiceIndex, float basePosition)
{
    auto& voice = grainVoices[voiceIndex];

    auto* sprayParam = parameters.getRawParameterValue("spray");
    auto* windowSizeParam = parameters.getRawParameterValue("windowSize");
    auto* pitchDispParam = parameters.getRawParameterValue("pitchDispersion");
    auto* amDispParam = parameters.getRawParameterValue("amDispersion");
    auto* stereoSpreadParam = parameters.getRawParameterValue("stereoSpread");
    auto* pitchParam = parameters.getRawParameterValue("pitch");

    // Grain ADSR parameters (base values)
    auto* grainAttackParam = parameters.getRawParameterValue("grainAttack");
    auto* grainDecayParam = parameters.getRawParameterValue("grainDecay");
    auto* grainSustainParam = parameters.getRawParameterValue("grainSustain");
    auto* grainReleaseParam = parameters.getRawParameterValue("grainRelease");

    float spray = sprayParam->load();
    float windowSize = windowSizeParam->load();
    float pitchDisp = pitchDispParam->load();
    float amDisp = amDispParam->load();
    float stereoSpread = stereoSpreadParam->load();
    float pitchSemitones = pitchParam->load();

    // Randomize grain ADSR values (only grain envelope gets randomization)
    float adsrRandomization = 0.3f;  // 30% variation
    voice.grainAttack = grainAttackParam->load() * (1.0f + (dist01(rng) - 0.5f) * adsrRandomization);
    voice.grainDecay = grainDecayParam->load() * (1.0f + (dist01(rng) - 0.5f) * adsrRandomization);
    voice.grainSustain = juce::jlimit(0.0f, 1.0f, grainSustainParam->load() + (dist01(rng) - 0.5f) * adsrRandomization);
    voice.grainRelease = grainReleaseParam->load() * (1.0f + (dist01(rng) - 0.5f) * adsrRandomization);

    // Position with spray
    float randomOffset = (dist01(rng) - 0.5f) * 2.0f * spray * windowSize;
    float targetPosition = juce::jlimit(0.0f, 1.0f, basePosition + randomOffset);

    int numSamples = sampleBuffer.getNumSamples();
    // Use double precision to avoid precision loss with large sample counts
    voice.samplePosition = (double)targetPosition * (double)numSamples;
    voice.grainEnvelopePhase = 0.0f;
    voice.noteEnvelopePhase = 0.0f;

    // Pitch calculation
    float totalPitch = 0.0f;

    // MIDI mode: Use resampling based on note
    if (anyNoteActive.load())
    {
        // Find the highest active note
        int highestNote = -1;
        for (int n = 0; n < 128; ++n)
        {
            if (noteStates[n].load())
                highestNote = n;
        }

        if (highestNote >= 0)
        {
            // C4 (MIDI note 60) is the root
            int semitoneOffset = highestNote - 60;
            totalPitch = semitoneOffset;
            voice.midiNote = highestNote;
        }
    }
    else if (mousePressed.load())
    {
        // Mouse mode: Use pitch knob
        totalPitch = pitchSemitones;
    }

    // Add pitch dispersion
    float pitchOffset = (dist01(rng) - 0.5f) * 2.0f * pitchDisp * 12.0f;
    totalPitch += pitchOffset;

    voice.pitch = std::pow(2.0f, totalPitch / 12.0f);

    // Amplitude modulation
    float ampOffset = (dist01(rng) - 0.5f) * 2.0f * amDisp;
    voice.amplitude = juce::jlimit(0.0f, 1.0f, 1.0f + ampOffset);

    // Stereo panning
    float pan = (dist01(rng) - 0.5f) * 2.0f * stereoSpread;
    float panAngle = (pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
    voice.panLeft = std::cos(panAngle);
    voice.panRight = std::sin(panAngle);

    voice.isActive = true;
}

void GranulateAudioProcessor::stopAllGrains()
{
    for (auto& voice : grainVoices)
        voice.isActive = false;
}

void GranulateAudioProcessor::setMousePosition(float position)
{
    mousePosition.store(position);
    mousePressed.store(true);
    anyNoteActive.store(false);
    noteReleased = false;
    noteEnvelopeTimer = 0.0f;

    for (auto& state : noteStates)
        state.store(false);
}

void GranulateAudioProcessor::releaseMousePosition()
{
    mousePressed.store(false);
    noteReleased = true;
    noteEnvelopeTimer = 0.0f;
}

bool GranulateAudioProcessor::isInMouseMode() const
{
    return mousePressed.load();
}

void GranulateAudioProcessor::processGrains(juce::AudioBuffer<float>& buffer,
    int startSample,
    int numSamples)
{
    if (!hasSample())
        return;

    // Try to lock buffer - if we can't (because it's being loaded), skip this block
    const juce::ScopedTryLock sl(bufferLock);
    if (!sl.isLocked())
        return;

    auto* numGrainsParam = parameters.getRawParameterValue("numGrains");
    auto* grainSizeParam = parameters.getRawParameterValue("grainSize");
    auto* grainPosParam = parameters.getRawParameterValue("grainPosition");
    auto* volumeParam = parameters.getRawParameterValue("volume");
    auto* noteAttackParam = parameters.getRawParameterValue("noteAttack");
    auto* noteDecayParam = parameters.getRawParameterValue("noteDecay");
    auto* noteReleaseParam = parameters.getRawParameterValue("noteRelease");

    const int   maxGrains = (int)numGrainsParam->load();
    const float grainSize = std::max(0.001f, grainSizeParam->load());
    const float basePosKnob = grainPosParam->load();
    const float volume = volumeParam->load();

    const float noteAttack = noteAttackParam->load();
    const float noteDecay = noteDecayParam->load();
    const float noteRelease = noteReleaseParam->load();

    const float sampleRate = (float)getSampleRate();

    const bool activeInput = anyNoteActive.load() || mousePressed.load();
    bool shouldProcess = activeInput || noteReleased;

    // How often grains are fired (Ableton-style density)
    const float grainIntervalSeconds =
        grainSize / (float)std::max(1, maxGrains);

    float basePosition =
        mousePressed.load() ? mousePosition.load() : basePosKnob;

    const int numChannels = sampleBuffer.getNumChannels();
    const int maxSampleIndex = sampleBuffer.getNumSamples() - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const int bufferIndex = startSample + i;

        // ============================
        // NOTE ENVELOPE
        // ============================
        float noteEnv = 1.0f;

        if (noteReleased)
        {
            noteEnvelopeTimer += 1.0f / sampleRate;

            const float phase = noteEnvelopeTimer / noteRelease;
            noteEnv = getNoteEnvelope(phase, true);

            if (noteEnvelopeTimer >= noteRelease)
            {
                stopAllGrains();
                break;
            }
        }
        else if (shouldProcess)
        {
            noteEnvelopeTimer += 1.0f / sampleRate;

            const float attackDecay = noteAttack + noteDecay;
            const float phase =
                attackDecay > 0.001f ? noteEnvelopeTimer / attackDecay : 1.0f;

            noteEnv = getNoteEnvelope(phase, false);
        }

        // ============================
        // GRAIN SCHEDULER (THE FIX)
        // ============================
        if (shouldProcess && !noteReleased)
        {
            grainTimer += 1.0f / sampleRate;

            if (grainTimer >= grainIntervalSeconds)
            {
                grainTimer -= grainIntervalSeconds;

                for (int v = 0; v < maxGrains; ++v)
                {
                    if (!grainVoices[v].isActive)
                    {
                        triggerGrain(v, basePosition);
                        break;
                    }
                }
            }
        }

        // ============================
        // AUDIO ACCUMULATION
        // ============================
        float leftOut = 0.0f;
        float rightOut = 0.0f;

        for (int v = 0; v < maxGrains; ++v)
        {
            auto& voice = grainVoices[v];
            if (!voice.isActive)
                continue;

            const int index = (int)voice.samplePosition;

            if (index >= 0 && index < maxSampleIndex)
            {
                const double frac = voice.samplePosition - (double)index;
                float sample = 0.0f;

                if (numChannels == 1)
                {
                    const float* d = sampleBuffer.getReadPointer(0);
                    sample = d[index] + (float)frac * (d[index + 1] - d[index]);
                }
                else
                {
                    const float* l = sampleBuffer.getReadPointer(0);
                    const float* r = sampleBuffer.getReadPointer(1);

                    const float ls = l[index] + (float)frac * (l[index + 1] - l[index]);
                    const float rs = r[index] + (float)frac * (r[index + 1] - r[index]);
                    sample = 0.5f * (ls + rs);
                }

                const float grainEnv =
                    getGrainEnvelope(voice.grainEnvelopePhase, voice);

                const float amp =
                    grainEnv * noteEnv * voice.amplitude * volume;

                sample *= amp;

                leftOut += sample * voice.panLeft;
                rightOut += sample * voice.panRight;
            }

            voice.samplePosition += voice.pitch;
            voice.grainEnvelopePhase += 1.0f / (grainSize * sampleRate);

            if (voice.grainEnvelopePhase >= 1.0f ||
                voice.samplePosition >= maxSampleIndex)
            {
                voice.isActive = false;
            }
        }

        leftOut = juce::jlimit(-0.95f, 0.95f, leftOut);
        rightOut = juce::jlimit(-0.95f, 0.95f, rightOut);

        if (buffer.getNumChannels() >= 2)
        {
            buffer.getWritePointer(0)[bufferIndex] += leftOut;
            buffer.getWritePointer(1)[bufferIndex] += rightOut;
        }
        else
        {
            buffer.getWritePointer(0)[bufferIndex] +=
                0.5f * (leftOut + rightOut);
        }
    }
}

void GranulateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    processMidiMessages(midiMessages);
    processGrains(buffer, 0, buffer.getNumSamples());
}

//==============================================================================
bool GranulateAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* GranulateAudioProcessor::createEditor()
{
    return new GranulateAudioProcessorEditor(*this);
}

//==============================================================================
void GranulateAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GranulateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

void GranulateAudioProcessor::loadSample(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        // Calculate file duration
        const double fileDurationSeconds = reader->lengthInSamples / reader->sampleRate;

        // Warn if file is longer than 1 hour
        if (fileDurationSeconds > 60 * 60)
        {
            int minutes = (int)(fileDurationSeconds / 60);
            int seconds = (int)fileDurationSeconds % 60;

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Long File Warning",
                "This file is " + juce::String(minutes) + " minutes and " +
                juce::String(seconds) + " seconds long.\n\n"
                "Loading files of this size (being longer than 1 hour) require a significant amount of memory. Up to 3 hours are supported.",
                "OK");
        }

        // Safety check: limit to 3 hours at 48kHz
        const juce::int64 maxSamples = 3 * 60 * 60 * 48000;  // 3 hours at 48kHz
        juce::int64 samplesToRead = juce::jmin(reader->lengthInSamples, maxSamples);

        if (reader->lengthInSamples > maxSamples)
        {
            DBG("Warning: File is longer than 3 hours, truncating to 3 hours");

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "File Truncated",
                "This file exceeds the 3-hour length limit. Only the first 3 hours will be loaded.",
                "OK");
        }

        // Allocate buffer with size check
        try
        {
            juce::AudioBuffer<float> tempBuffer((int)reader->numChannels, (int)samplesToRead);

            // Read in chunks to avoid blocking too long
            const int chunkSize = 65536;  // Read 64k samples at a time
            juce::int64 samplesRead = 0;

            while (samplesRead < samplesToRead)
            {
                juce::int64 samplesToReadThisTime = juce::jmin((juce::int64)chunkSize, samplesToRead - samplesRead);
                reader->read(&tempBuffer, (int)samplesRead, (int)samplesToReadThisTime,
                    samplesRead, true, true);
                samplesRead += samplesToReadThisTime;
            }

            // Atomically swap buffers to avoid audio thread access issues
            {
                const juce::ScopedLock sl(bufferLock);
                sampleBuffer = std::move(tempBuffer);
            }

            stopAllGrains();
            mousePressed.store(false);
        }
        catch (const std::exception& e)
        {
            DBG("Error loading sample: " + juce::String(e.what()));

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Error Loading File",
                "Failed to load audio file:\n" + juce::String(e.what()),
                "OK");
        }
    }
}

void GranulateAudioProcessor::loadSample(const void* data, size_t dataSize)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto inputStream = std::make_unique<juce::MemoryInputStream>(data, dataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(inputStream)));

    if (reader != nullptr)
    {
        // Calculate file duration
        const double fileDurationSeconds = reader->lengthInSamples / reader->sampleRate;

        // Warn if file is longer than 1 hour
        if (fileDurationSeconds > 60 * 60)
        {
            int minutes = (int)(fileDurationSeconds / 60);
            int seconds = (int)fileDurationSeconds % 60;

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Long File Warning",
                "This file is " + juce::String(minutes) + " minutes and " +
                juce::String(seconds) + " seconds long.\n\n"
                "Loading files of this size (being longer than 1 hour) require a significant amount of memory. Up to 3 hours are supported.",
                "OK");
        }

        // Safety check: limit to 3 hours at 48kHz
        const juce::int64 maxSamples = 3 * 60 * 60 * 48000;
        juce::int64 samplesToRead = juce::jmin(reader->lengthInSamples, maxSamples);

        if (reader->lengthInSamples > maxSamples)
        {
            DBG("Warning: File is longer than 3 hours, truncating to 3 hours");

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "File Truncated",
                "This file exceeds the 3-hour length limit. Only the first 3 hours will be loaded.",
                "OK");
        }

        try
        {
            juce::AudioBuffer<float> tempBuffer((int)reader->numChannels, (int)samplesToRead);

            // Read in chunks
            const int chunkSize = 65536;
            juce::int64 samplesRead = 0;

            while (samplesRead < samplesToRead)
            {
                juce::int64 samplesToReadThisTime = juce::jmin((juce::int64)chunkSize, samplesToRead - samplesRead);
                reader->read(&tempBuffer, (int)samplesRead, (int)samplesToReadThisTime,
                    samplesRead, true, true);
                samplesRead += samplesToReadThisTime;
            }

            // Atomically swap buffers
            {
                const juce::ScopedLock sl(bufferLock);
                sampleBuffer = std::move(tempBuffer);
            }

            stopAllGrains();
            mousePressed.store(false);
        }
        catch (const std::exception& e)
        {
            DBG("Error loading sample: " + juce::String(e.what()));

            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Error Loading File",
                "Failed to load audio file:\n" + juce::String(e.what()),
                "OK");
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GranulateAudioProcessor();
}