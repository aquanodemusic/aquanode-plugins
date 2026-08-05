#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Region selection (normalized 0-1)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "regionStart", "Region Start", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "regionEnd", "Region End", 0.0f, 1.0f, 1.0f));

    // Slice sensitivity (threshold for transient detection)
    // Note: Now properly inverted - 1.0 = most sensitive, 0.01 = least sensitive
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "sliceStrength", "Slice Strength",
        juce::NormalisableRange<float>(0.01f, 1.0f, 0.01f, 0.5f), 0.3f));

    // Master volume
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "volume", "Volume", 0.0f, 1.0f, 0.8f));

    // Slice end mode (3 options)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "sliceEndMode", "Slice End Mode",
        juce::StringArray("Full Length", "Trim at -40dB", "Smart Trim (Quietest)"),
        2)); // Default to smart mode

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "fadeoutMs", "Fadeout Length (ms)",
        juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 5.0f));

    // Reverse play mode
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "reverseMode", "Reverse Mode", false));

    // Loop mode — slice loops continuously while held / until stopped
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "loopMode", "Loop Mode", false));

    // Mono mode — only one slice plays at a time; triggering a new one
    // instantly stops whatever is currently playing
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "monoMode", "Mono Mode", false));

    // Playback speed (0.25x to 4x, default 1.0x)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "playbackSpeed", "Speed",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f, 0.5f), 1.0f));

    // Interaction mode for waveform mouse clicks
    // 0 = "Add Slice on Mouse Click"  1 = "Play Slice on Mouse Click"
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "interactionMode", "Interaction Mode",
        juce::StringArray("Add Slice on Mouse Click", "Play Slice on Mouse Click"),
        0));  // Default: add-slice mode

    return layout;
}

//==============================================================================
SlicerAudioProcessor::SlicerAudioProcessor()
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
    voices.resize(128); // One voice per possible MIDI note

    // Initialize random engine with time-based seed
    randomEngine.seed(static_cast<unsigned int>(juce::Time::currentTimeMillis()));
}

SlicerAudioProcessor::~SlicerAudioProcessor()
{
}

//==============================================================================
const juce::String SlicerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SlicerAudioProcessor::acceptsMidi() const
{
    return true;
}

bool SlicerAudioProcessor::producesMidi() const
{
    return false;
}

bool SlicerAudioProcessor::isMidiEffect() const
{
    return false;
}

double SlicerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SlicerAudioProcessor::getNumPrograms()
{
    return 1;
}

int SlicerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SlicerAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String SlicerAudioProcessor::getProgramName(int index)
{
    return {};
}

void SlicerAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void SlicerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& voice : voices)
        voice.reset();
}

void SlicerAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SlicerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

//==============================================================================
int SlicerAudioProcessor::getRegionStart() const
{
    if (!hasSample())
        return 0;

    float regionStartNorm = parameters.getRawParameterValue("regionStart")->load();
    return (int)(regionStartNorm * sampleBuffer.getNumSamples());
}

int SlicerAudioProcessor::getRegionEnd() const
{
    if (!hasSample())
        return 0;

    float regionEndNorm = parameters.getRawParameterValue("regionEnd")->load();
    return (int)(regionEndNorm * sampleBuffer.getNumSamples());
}

//==============================================================================
float SlicerAudioProcessor::calculateEnvelopeFollower(int startSample, int windowSize)
{
    if (!hasSample() || startSample < 0 || startSample >= sampleBuffer.getNumSamples())
        return 0.0f;

    float rmsSum = 0.0f;
    int numChannels = sampleBuffer.getNumChannels();
    int endSample = juce::jmin(startSample + windowSize, sampleBuffer.getNumSamples());
    int count = 0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* channelData = sampleBuffer.getReadPointer(ch);

        for (int i = startSample; i < endSample; ++i)
        {
            float sample = channelData[i];
            rmsSum += sample * sample;
            count++;
        }
    }

    if (count > 0)
        return std::sqrt(rmsSum / count);

    return 0.0f;
}

void SlicerAudioProcessor::detectTransients(float sensitivity, int regionStart, int regionEnd)
{
    slicePoints.clear();

    if (!hasSample() || regionEnd <= regionStart)
        return;

    // IMPROVED TRANSIENT DETECTION ALGORITHM
    // Combines multiple detection methods for better drum/percussion detection

    const int hopSize = 128;        // Smaller hop for better temporal resolution
    const int windowSize = 512;     // Analysis window
    const int numChannels = sampleBuffer.getNumChannels();

    // Calculate multiple detection features
    std::vector<float> energyEnvelope;
    std::vector<float> highFreqContent;
    std::vector<float> spectralFlux;
    std::vector<int> samplePositions;

    // Previous frame for spectral flux calculation
    std::vector<float> prevSpectrum(windowSize / 2, 0.0f);

    for (int i = regionStart; i < regionEnd - windowSize; i += hopSize)
    {
        samplePositions.push_back(i);

        // 1. Energy envelope (RMS with attack weighting)
        float energy = 0.0f;
        float maxPeak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = sampleBuffer.getReadPointer(ch);

            for (int s = 0; s < windowSize; ++s)
            {
                int idx = i + s;
                if (idx >= regionEnd) break;

                float sample = data[idx];
                energy += sample * sample;
                maxPeak = juce::jmax(maxPeak, std::abs(sample));
            }
        }

        energy = std::sqrt(energy / (windowSize * numChannels));
        energyEnvelope.push_back(energy * 0.7f + maxPeak * 0.3f); // Blend RMS and peak

        // 2. High-frequency content (detects attack transients)
        float hfc = 0.0f;
        std::vector<float> currentSpectrum(windowSize / 2, 0.0f);

        // Simple high-frequency energy calculation
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = sampleBuffer.getReadPointer(ch);

            // Focus on the first quarter of the window (attack portion)
            for (int s = 0; s < windowSize / 4; ++s)
            {
                int idx = i + s;
                if (idx >= regionEnd) break;

                float sample = data[idx];
                float prevSample = (idx > 0) ? data[idx - 1] : 0.0f;
                float diff = sample - prevSample;
                hfc += diff * diff; // High frequency = large sample-to-sample changes
            }

            // Also calculate a simple spectrum for flux
            for (int s = 0; s < windowSize / 2; ++s)
            {
                int idx = i + s;
                if (idx >= regionEnd) break;

                currentSpectrum[s] += std::abs(data[idx]);
            }
        }

        hfc = std::sqrt(hfc);
        highFreqContent.push_back(hfc);

        // 3. Spectral flux (detects sudden changes in frequency content)
        float flux = 0.0f;
        for (int s = 0; s < windowSize / 2; ++s)
        {
            float diff = currentSpectrum[s] - prevSpectrum[s];
            if (diff > 0) // Only positive changes (onset detection)
                flux += diff;
        }

        spectralFlux.push_back(flux);
        prevSpectrum = currentSpectrum;
    }

    if (energyEnvelope.empty())
        return;

    // Normalize all features
    auto normalizeVector = [](std::vector<float>& vec) {
        float maxVal = *std::max_element(vec.begin(), vec.end());
        if (maxVal > 0.0f)
        {
            for (auto& val : vec)
                val /= maxVal;
        }
        };

    normalizeVector(energyEnvelope);
    normalizeVector(highFreqContent);
    normalizeVector(spectralFlux);

    // Combine features into a single onset strength function
    std::vector<float> onsetStrength(energyEnvelope.size());
    for (size_t i = 0; i < onsetStrength.size(); ++i)
    {
        // Weighted combination emphasizing attacks
        onsetStrength[i] = energyEnvelope[i] * 0.4f +
            highFreqContent[i] * 0.35f +
            spectralFlux[i] * 0.25f;
    }

    // INVERT the sensitivity: 1.0 = most sensitive (low threshold), 0.01 = least sensitive (high threshold)
    float threshold = 1.0f - sensitivity;
    threshold = threshold * 0.5f + 0.15f; // Map to range 0.15-0.65

    // Peak picking with adaptive threshold
    const int minDistanceBetweenSlices = 2200 / hopSize; // ~50ms minimum distance at 44.1kHz

    for (size_t i = 2; i < onsetStrength.size() - 2; ++i)
    {
        // Local maximum detection with hysteresis
        bool isLocalMax = onsetStrength[i] > onsetStrength[i - 1] &&
            onsetStrength[i] > onsetStrength[i - 2] &&
            onsetStrength[i] >= onsetStrength[i + 1] &&
            onsetStrength[i] >= onsetStrength[i + 2];

        if (!isLocalMax)
            continue;

        // Calculate local adaptive threshold
        float localMax = 0.0f;
        int lookback = juce::jmin(20, (int)i);
        for (int j = 0; j < lookback; ++j)
        {
            localMax = juce::jmax(localMax, onsetStrength[i - j]);
        }

        // Must exceed both global and local thresholds
        bool exceedsThreshold = onsetStrength[i] > threshold &&
            onsetStrength[i] > localMax * 0.6f;

        if (!exceedsThreshold)
            continue;

        // Check minimum distance from previous slices
        bool farEnough = true;
        if (!slicePoints.empty())
        {
            int lastSlicePos = slicePoints.back().samplePosition;
            int currentPos = samplePositions[i];
            int distance = (currentPos - lastSlicePos) / hopSize;

            if (distance < minDistanceBetweenSlices)
                farEnough = false;
        }

        if (farEnough)
        {
            // Find the exact peak within the window for better accuracy
            int basePos = samplePositions[i];
            float maxAmp = 0.0f;
            int peakOffset = 0;

            for (int s = 0; s < hopSize * 2; ++s)
            {
                int checkPos = basePos + s;
                if (checkPos >= regionEnd) break;

                float amp = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    amp += std::abs(sampleBuffer.getReadPointer(ch)[checkPos]);
                }

                if (amp > maxAmp)
                {
                    maxAmp = amp;
                    peakOffset = s;
                }
            }

            int finalPos = basePos + peakOffset;
            slicePoints.emplace_back(finalPos, onsetStrength[i]);
        }
    }

    // Sort slices by position
    std::sort(slicePoints.begin(), slicePoints.end(),
        [](const SlicePoint& a, const SlicePoint& b) {
            return a.samplePosition < b.samplePosition;
        });
}

void SlicerAudioProcessor::analyzeAndSlice()
{
    if (!hasSample())
        return;

    float sensitivity = parameters.getRawParameterValue("sliceStrength")->load();
    int regionStart = getRegionStart();
    int regionEnd = getRegionEnd();

    detectTransients(sensitivity, regionStart, regionEnd);

    // Apply slice end mode adjustments (modes 2 and 3 will adjust slice positions)
    applySliceEndMode();
}

//==============================================================================
int SlicerAudioProcessor::getNoteForSlice(int sliceIndex) const
{
    // Map to C1 (MIDI 36) to C9 (MIDI 108)
    // That's 73 possible notes (9 octaves * 12 semitones + 1)
    const int baseNote = 36; // C1
    const int maxNote = 108; // C9

    int midiNote = baseNote + sliceIndex;
    return juce::jlimit(baseNote, maxNote, midiNote);
}

int SlicerAudioProcessor::getSliceForNote(int midiNote) const
{
    const int baseNote = 36; // C1

    if (midiNote < baseNote)
        return -1;

    int sliceIndex = midiNote - baseNote;

    if (sliceIndex >= (int)slicePoints.size())
        return -1;

    return sliceIndex;
}

//==============================================================================
void SlicerAudioProcessor::triggerSlice(int sliceIndex, float velocity)
{
    if (sliceIndex < 0 || sliceIndex >= (int)slicePoints.size())
        return;

    if (!slicePoints[sliceIndex].active)
        return;

    // Mono mode: silence every active voice before starting the new one
    // so only one slice ever plays at a time.
    bool monoMode = parameters.getRawParameterValue("monoMode")->load() > 0.5f;
    if (monoMode)
    {
        for (auto& v : voices)
            v.reset();
        randomStreamActive = false;
    }

    // Find or create a voice for this slice
    int voiceIndex = getNoteForSlice(sliceIndex);

    if (voiceIndex < 0 || voiceIndex >= (int)voices.size())
        return;

    auto& voice = voices[voiceIndex];

    voice.isActive = true;
    voice.sliceIndex = sliceIndex;
    voice.velocity = velocity;
    voice.sliceStart = slicePoints[sliceIndex].samplePosition;

    // Slice end is simply the next slice start (already adjusted by applySliceEndMode)
    if (sliceIndex + 1 < (int)slicePoints.size())
        voice.sliceEnd = slicePoints[sliceIndex + 1].samplePosition;
    else
        voice.sliceEnd = getRegionEnd();

    // Check if reverse mode is enabled
    bool reverseMode = parameters.getRawParameterValue("reverseMode")->load() > 0.5f;
    voice.playingReverse = reverseMode;

    // Always start currentSample at 0, regardless of direction
    voice.currentSample = 0;
}

void SlicerAudioProcessor::stopSlice(int sliceIndex)
{
    int voiceIndex = getNoteForSlice(sliceIndex);

    if (voiceIndex >= 0 && voiceIndex < (int)voices.size())
        voices[voiceIndex].reset();
}

//==============================================================================
void SlicerAudioProcessor::triggerRandomSlice()
{
    if (slicePoints.empty())
        return;

    // Update distribution range if needed
    int maxSliceIndex = (int)slicePoints.size() - 1;
    randomSliceDistribution = std::uniform_int_distribution<int>(0, maxSliceIndex);

    // Pick a random slice
    int randomSliceIndex = randomSliceDistribution(randomEngine);

    // Use voice index 35 (MIDI note 35 = B0) for random stream
    randomStreamVoiceIndex = 35;

    // Trigger the random slice with full velocity
    auto& voice = voices[randomStreamVoiceIndex];

    if (randomSliceIndex < 0 || randomSliceIndex >= (int)slicePoints.size())
        return;

    if (!slicePoints[randomSliceIndex].active)
    {
        // Try another random slice
        triggerRandomSlice();
        return;
    }

    voice.isActive = true;
    voice.sliceIndex = randomSliceIndex;
    voice.velocity = 1.0f;
    voice.sliceStart = slicePoints[randomSliceIndex].samplePosition;

    if (randomSliceIndex + 1 < (int)slicePoints.size())
        voice.sliceEnd = slicePoints[randomSliceIndex + 1].samplePosition;
    else
        voice.sliceEnd = getRegionEnd();

    // Check if reverse mode is enabled
    bool reverseMode = parameters.getRawParameterValue("reverseMode")->load() > 0.5f;
    voice.playingReverse = reverseMode;

    // Always start currentSample at 0, regardless of direction
    voice.currentSample = 0;
}

void SlicerAudioProcessor::startRandomStream()
{
    if (!randomStreamActive)
    {
        randomStreamActive = true;
        triggerRandomSlice();
    }
}

void SlicerAudioProcessor::stopRandomStream()
{
    randomStreamActive = false;
    if (randomStreamVoiceIndex >= 0 && randomStreamVoiceIndex < (int)voices.size())
        voices[randomStreamVoiceIndex].reset();
}

//==============================================================================
void SlicerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (!hasSample())
        return;

    float volume = parameters.getRawParameterValue("volume")->load();
    float fadeoutMs = parameters.getRawParameterValue("fadeoutMs")->load();
    float playbackSpeed = parameters.getRawParameterValue("playbackSpeed")->load();
    bool loopMode = parameters.getRawParameterValue("loopMode")->load() > 0.5f;

    // Convert fadeout from ms to samples at current sample rate
    double currentSampleRate = getSampleRate();
    if (currentSampleRate <= 0)
        currentSampleRate = 44100.0;
    int fadeoutSamples = (int)((fadeoutMs / 1000.0) * currentSampleRate);

    // MIDI note 35 (B0) is reserved for random stream mode
    const int randomStreamNote = 35;

    // Process MIDI
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            if (message.getNoteNumber() == randomStreamNote)
            {
                // MIDI note 35 triggers random stream mode
                if (!randomStreamActive)
                {
                    randomStreamActive = true;
                    triggerRandomSlice();
                }
            }
            else
            {
                // Normal mode: map note to specific slice
                int sliceIndex = getSliceForNote(message.getNoteNumber());
                if (sliceIndex >= 0)
                {
                    float velocity = message.getVelocity() / 127.0f;
                    triggerSlice(sliceIndex, velocity);
                }
            }
        }
        else if (message.isNoteOff())
        {
            if (message.getNoteNumber() == randomStreamNote)
            {
                // Note off on MIDI 35 stops random stream
                stopRandomStream();
            }
            else
            {
                int sliceIndex = getSliceForNote(message.getNoteNumber());
                if (sliceIndex >= 0)
                    stopSlice(sliceIndex);
            }
        }
    }

    // Render audio
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int sampleNumChannels = sampleBuffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        float outputLeft = 0.0f;
        float outputRight = 0.0f;

        // Mix all active voices
        for (auto& voice : voices)
        {
            if (!voice.isActive)
                continue;

            int sliceLength = voice.sliceEnd - voice.sliceStart;

            // Check if we've reached or passed the end of the slice
            if (voice.currentSample >= (float)sliceLength)
            {
                if (randomStreamActive && &voice == &voices[randomStreamVoiceIndex])
                {
                    // Trigger next slice immediately so there's no gap
                    triggerRandomSlice();

                    // Re-fetch slice length for the NEW slice
                    sliceLength = voice.sliceEnd - voice.sliceStart;

                    // Safety check: if new slice is invalid, stop random stream
                    if (sliceLength <= 0)
                    {
                        voice.reset();
                        randomStreamActive = false;
                        continue;
                    }
                }
                else
                {
                    // Loop mode: wrap currentSample back to the start of the
                    // slice so playback continues seamlessly.
                    if (loopMode)
                    {
                        voice.currentSample = std::fmod(voice.currentSample, (float)sliceLength);
                    }
                    else
                    {
                        voice.reset();
                        continue;
                    }
                }
            }

            // Clamp currentSample to valid range to prevent overflow
            if (voice.currentSample >= (float)sliceLength)
                voice.currentSample = (float)(sliceLength - 1);
            if (voice.currentSample < 0.0f)
                voice.currentSample = 0.0f;

            // Calculate sample position based on playback direction
            float floatSamplePos;
            if (voice.playingReverse)
                floatSamplePos = (float)voice.sliceEnd - 1.0f - voice.currentSample;
            else
                floatSamplePos = (float)voice.sliceStart + voice.currentSample;

            // Get integer and fractional parts for interpolation
            int samplePos = (int)floatSamplePos;
            float frac = floatSamplePos - (float)samplePos;

            // Clamp sample position to valid buffer range (more lenient than before)
            samplePos = juce::jlimit(0, sampleBuffer.getNumSamples() - 2, samplePos);

            // Read sample with linear interpolation for smooth pitch shifting
            float sample = 0.0f;

            if (sampleNumChannels == 1)
            {
                float sample1 = sampleBuffer.getSample(0, samplePos);
                float sample2 = sampleBuffer.getSample(0, samplePos + 1);
                sample = sample1 + frac * (sample2 - sample1);
            }
            else
            {
                float left1 = sampleBuffer.getSample(0, samplePos);
                float left2 = sampleBuffer.getSample(0, samplePos + 1);
                float right1 = sampleBuffer.getSample(1, samplePos);
                float right2 = sampleBuffer.getSample(1, samplePos + 1);

                float left = left1 + frac * (left2 - left1);
                float right = right1 + frac * (right2 - right1);
                sample = (left + right) * 0.5f;
            }

            // Apply fadeout if we're near the end of the slice
            int samplesFromEnd = sliceLength - (int)voice.currentSample;

            if (samplesFromEnd <= fadeoutSamples && fadeoutSamples > 0)
            {
                float fadeGain = (float)samplesFromEnd / (float)fadeoutSamples;
                sample *= fadeGain;
            }

            sample *= voice.velocity;

            outputLeft += sample;
            outputRight += sample;

            // Advance by playback speed
            voice.currentSample += playbackSpeed;
        }

        // Apply volume and write to output
        outputLeft *= volume;
        outputRight *= volume;

        if (numChannels >= 1)
            buffer.setSample(0, i, outputLeft);
        if (numChannels >= 2)
            buffer.setSample(1, i, outputRight);
    }
}

//==============================================================================
void SlicerAudioProcessor::setSlicePosition(int index, int newPosition)
{
    if (index >= 0 && index < (int)slicePoints.size())
    {
        slicePoints[index].samplePosition = newPosition;

        // Re-sort slices
        std::sort(slicePoints.begin(), slicePoints.end(),
            [](const SlicePoint& a, const SlicePoint& b) {
                return a.samplePosition < b.samplePosition;
            });
    }
}

void SlicerAudioProcessor::removeSlice(int index)
{
    if (index >= 0 && index < (int)slicePoints.size())
        slicePoints.erase(slicePoints.begin() + index);
}

void SlicerAudioProcessor::addSliceAtPosition(int samplePosition)
{
    slicePoints.emplace_back(samplePosition, 1.0f);

    // Re-sort slices
    std::sort(slicePoints.begin(), slicePoints.end(),
        [](const SlicePoint& a, const SlicePoint& b) {
            return a.samplePosition < b.samplePosition;
        });
}

//==============================================================================
bool SlicerAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SlicerAudioProcessor::createEditor()
{
    return new SlicerAudioProcessorEditor(*this);
}

//==============================================================================
void SlicerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    // Persist the last loaded sample path so the DAW can restore it on reload
    if (lastLoadedSamplePath.isNotEmpty())
        state.setProperty("samplePath", lastLoadedSamplePath, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SlicerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        auto newState = juce::ValueTree::fromXml(*xmlState);
        parameters.replaceState(newState);

        // Restore the last loaded sample. Silently skip if the file is missing
        // (moved, deleted, different machine).
        const juce::String path = newState.getProperty("samplePath", "").toString();
        if (path.isNotEmpty())
        {
            const juce::File f(path);
            if (f.existsAsFile())
                loadSample(f);
        }
    }
}

void SlicerAudioProcessor::loadSample(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        const double maxDurationSeconds = 10.0 * 60.0;
        const juce::int64 maxSamples = static_cast<juce::int64>(maxDurationSeconds * reader->sampleRate);

        bool wasTruncated = reader->lengthInSamples > maxSamples;
        int numSamplesToLoad = static_cast<int>(wasTruncated ? maxSamples : reader->lengthInSamples);

        sampleBuffer.setSize(static_cast<int>(reader->numChannels), numSamplesToLoad);
        reader->read(&sampleBuffer, 0, numSamplesToLoad, 0, true, true);

        loadedSampleRate = reader->sampleRate;
        lastLoadedSamplePath = file.getFullPathName();

        if (wasTruncated)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "File Truncated",
                "The file you want to load is very long for the purpose of Slicer, for safety we only load the first 10 minutes!",
                "OK");
        }

        analyzeAndSlice();

        for (auto& voice : voices)
            voice.reset();
    }
}

void SlicerAudioProcessor::loadSample(const void* data, size_t dataSize)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto inputStream = std::make_unique<juce::MemoryInputStream>(data, dataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(inputStream)));

    if (reader != nullptr)
    {
        const double maxDurationSeconds = 10.0 * 60.0;
        const juce::int64 maxSamples = static_cast<juce::int64>(maxDurationSeconds * reader->sampleRate);

        bool wasTruncated = reader->lengthInSamples > maxSamples;
        int numSamplesToLoad = static_cast<int>(wasTruncated ? maxSamples : reader->lengthInSamples);

        sampleBuffer.setSize(static_cast<int>(reader->numChannels), numSamplesToLoad);
        reader->read(&sampleBuffer, 0, numSamplesToLoad, 0, true, true);

        loadedSampleRate = reader->sampleRate;

        if (wasTruncated)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "File Truncated",
                "File is too long, we only load the first 10 minutes.",
                "OK");
        }

        analyzeAndSlice();

        for (auto& voice : voices)
            voice.reset();
    }
}

//==============================================================================
// Calculate slice end position based on mode - MODE 1 only (no adjustment)
//==============================================================================

int SlicerAudioProcessor::findQuietestPointInTail(int sliceStart, int sliceEnd) const
{
    if (!hasSample())
        return sliceEnd;

    int sliceLength = sliceEnd - sliceStart;

    // Search in the last 10% of the slice
    int searchStart = sliceStart + (sliceLength * 9 / 10);

    if (searchStart >= sliceEnd - 1)
        return sliceEnd;

    float minRMS = std::numeric_limits<float>::max();
    int quietestPoint = sliceEnd;

    const int windowSize = 128;
    const int numChannels = sampleBuffer.getNumChannels();

    for (int s = searchStart; s < sliceEnd - windowSize; ++s)
    {
        float rmsSum = 0.0f;
        int count = 0;

        for (int w = 0; w < windowSize && (s + w) < sliceEnd; ++w)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float sample = sampleBuffer.getSample(ch, s + w);
                rmsSum += sample * sample;
                count++;
            }
        }

        if (count > 0)
        {
            float rms = std::sqrt(rmsSum / count);

            if (rms < minRMS)
            {
                minRMS = rms;
                quietestPoint = s;
            }
        }
    }

    // Ensure we keep at least 50% of the slice
    return juce::jmax(quietestPoint, sliceStart + sliceLength / 2);
}

int SlicerAudioProcessor::findDb40EndPoint(int sliceStart, int sliceEnd) const
{
    if (!hasSample())
        return sliceEnd;

    const float threshold = 0.01f; // -40dB
    int sliceLength = sliceEnd - sliceStart;
    const int numChannels = sampleBuffer.getNumChannels();

    // Scan backwards to find where audio drops below -40dB threshold
    for (int s = sliceEnd - 1; s > sliceStart + sliceLength / 2; --s)
    {
        float maxSample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            maxSample = juce::jmax(maxSample, std::abs(sampleBuffer.getSample(ch, s)));
        }

        if (maxSample > threshold)
        {
            return s + 1;
        }
    }

    return sliceEnd;
}

int SlicerAudioProcessor::calculateSliceEnd(int sliceIndex) const
{
    if (sliceIndex < 0 || sliceIndex >= (int)slicePoints.size())
        return getRegionEnd();

    int sliceStart = slicePoints[sliceIndex].samplePosition;
    int rawSliceEnd;

    // Determine the raw slice end (next slice or region end)
    if (sliceIndex + 1 < (int)slicePoints.size())
        rawSliceEnd = slicePoints[sliceIndex + 1].samplePosition;
    else
        rawSliceEnd = getRegionEnd();

    // Mode 1: Full length - no adjustment, return raw end
    // This is used only for real-time playback in mode 1
    // Modes 2 and 3 adjust slice positions directly in analyzeAndSlice()
    return rawSliceEnd;
}

void SlicerAudioProcessor::applySliceEndMode()
{
    if (!hasSample() || slicePoints.empty())
        return;

    int mode = (int)parameters.getRawParameterValue("sliceEndMode")->load();

    if (mode == 0)
    {
        // Mode 1: Full Length - do nothing, slices stay as detected
        return;
    }

    int regionEnd = getRegionEnd();

    // Process all slices except the last one (last one goes to region end)
    for (size_t i = 0; i < slicePoints.size() - 1; ++i)
    {
        int sliceStart = slicePoints[i].samplePosition;
        int nextSliceStart = slicePoints[i + 1].samplePosition;

        int adjustedEnd;

        if (mode == 1)
        {
            // Mode 2: Trim at -40dB
            adjustedEnd = findDb40EndPoint(sliceStart, nextSliceStart);
        }
        else // mode == 2
        {
            // Mode 3: Smart Trim (quietest point)
            adjustedEnd = findQuietestPointInTail(sliceStart, nextSliceStart);
        }

        // Move the next slice's start position to this adjusted end
        // This effectively shortens the current slice
        if (adjustedEnd < nextSliceStart && adjustedEnd > sliceStart)
        {
            slicePoints[i + 1].samplePosition = adjustedEnd;
        }
    }
}

//==============================================================================
bool SlicerAudioProcessor::isSlicePlaying(int sliceIndex) const
{
    int voiceIndex = getNoteForSlice(sliceIndex);

    if (voiceIndex >= 0 && voiceIndex < (int)voices.size())
        return voices[voiceIndex].isActive && voices[voiceIndex].sliceIndex == sliceIndex;

    return false;
}

//==============================================================================
bool SlicerAudioProcessor::exportSlicesToDisc(const juce::File& outputFolder)
{
    if (!hasSample() || slicePoints.empty())
        return false;

    if (!outputFolder.exists())
        outputFolder.createDirectory();

    // Setup FLAC format writer
    juce::FlacAudioFormat flacFormat;

    // Target sample rate
    const double targetSampleRate = 48000.0;

    // Get parameters
    float fadeoutMs = parameters.getRawParameterValue("fadeoutMs")->load();
    bool reverseMode = parameters.getRawParameterValue("reverseMode")->load() > 0.5f;
    float playbackSpeed = parameters.getRawParameterValue("playbackSpeed")->load(); // <--- NEW: Get speed

    int regionEnd = getRegionEnd();

    // Export each slice
    for (size_t i = 0; i < slicePoints.size(); ++i)
    {
        if (!slicePoints[i].active)
            continue;

        int sliceStart = slicePoints[i].samplePosition;
        int sliceEnd;

        // Slice positions are already adjusted by applySliceEndMode
        if (i + 1 < slicePoints.size())
            sliceEnd = slicePoints[i + 1].samplePosition;
        else
            sliceEnd = regionEnd;

        int sourceLength = sliceEnd - sliceStart;

        if (sourceLength <= 0)
            continue;

        // <--- NEW: Calculate output length based on speed
        // If speed is 2.0x, length is halved. If speed is 0.5x, length is doubled.
        int outputLength = (int)(sourceLength / playbackSpeed);

        if (outputLength <= 0)
            continue;

        // Create a buffer for this slice with the NEW length
        juce::AudioBuffer<float> sliceBuffer(sampleBuffer.getNumChannels(), outputLength);

        // <--- NEW: Resample data from source to slice buffer
        for (int ch = 0; ch < sampleBuffer.getNumChannels(); ++ch)
        {
            const float* srcData = sampleBuffer.getReadPointer(ch);
            float* destData = sliceBuffer.getWritePointer(ch);

            for (int s = 0; s < outputLength; ++s)
            {
                // Calculate position in source buffer
                double srcPos = sliceStart + (s * playbackSpeed);

                // Linear Interpolation (consistent with processBlock)
                int p1 = (int)srcPos;
                int p2 = p1 + 1;
                float frac = (float)(srcPos - p1);

                // Boundary checks
                if (p1 >= sampleBuffer.getNumSamples()) p1 = sampleBuffer.getNumSamples() - 1;
                if (p2 >= sampleBuffer.getNumSamples()) p2 = sampleBuffer.getNumSamples() - 1;

                destData[s] = srcData[p1] + frac * (srcData[p2] - srcData[p1]);
            }
        }

        // Reverse the buffer if reverse mode is enabled
        // Note: We now use outputLength instead of sliceLength
        if (reverseMode)
        {
            for (int ch = 0; ch < sliceBuffer.getNumChannels(); ++ch)
            {
                float* channelData = sliceBuffer.getWritePointer(ch);
                std::reverse(channelData, channelData + outputLength);
            }
        }

        // Calculate fadeout length in samples (convert ms to samples at target rate)
        // Use outputLength for bounds check
        int fadeoutSamples = (int)((fadeoutMs / 1000.0f) * targetSampleRate);
        fadeoutSamples = juce::jmin(fadeoutSamples, outputLength / 2);

        // Apply fadeout to prevent clicks
        for (int s = outputLength - fadeoutSamples; s < outputLength; ++s)
        {
            if (s < 0) continue;
            float gain = (float)(outputLength - s) / fadeoutSamples;
            for (int ch = 0; ch < sliceBuffer.getNumChannels(); ++ch)
            {
                sliceBuffer.setSample(ch, s, sliceBuffer.getSample(ch, s) * gain);
            }
        }

        // Calculate MIDI note for filename
        int midiNote = 36 + i; // C2 = 36
        static const char* noteNames[] = { "C", "Cs", "D", "Ds", "E", "F",
                                           "Fs", "G", "Gs", "A", "As", "B" };

        int octave = (midiNote / 12) - 1;
        int noteIndex = midiNote % 12;

        // Create filename
        juce::String filename = juce::String("slice_") +
            juce::String(i + 1).paddedLeft('0', 3) +
            juce::String("_") +
            juce::String(noteNames[noteIndex]) +
            juce::String(octave) +
            juce::String(".flac");

        juce::File outputFile = outputFolder.getChildFile(filename);

        // Delete if exists
        if (outputFile.exists())
            outputFile.deleteFile();

        // Create output stream
        std::unique_ptr<juce::FileOutputStream> outputStream(outputFile.createOutputStream());

        if (outputStream == nullptr)
            continue;

        // Create FLAC writer
        juce::StringPairArray metadataValues;
        std::unique_ptr<juce::AudioFormatWriter> writer;

        writer.reset(flacFormat.createWriterFor(outputStream.get(),
            targetSampleRate,
            sliceBuffer.getNumChannels(),
            24,
            metadataValues,
            8));

        if (writer != nullptr)
        {
            outputStream.release(); // Writer takes ownership

            // Resample if necessary (SRC to 48kHz)
            if (targetSampleRate != getSampleRate())
            {
                // Calculate resampling ratio
                double ratio = targetSampleRate / getSampleRate();
                int newResampledLength = (int)(outputLength * ratio);

                // Create resampled buffer
                juce::AudioBuffer<float> resampledBuffer(sliceBuffer.getNumChannels(), newResampledLength);

                // Linear interpolation for sample rate conversion
                for (int ch = 0; ch < sliceBuffer.getNumChannels(); ++ch)
                {
                    const float* source = sliceBuffer.getReadPointer(ch);
                    float* dest = resampledBuffer.getWritePointer(ch);

                    for (int j = 0; j < newResampledLength; ++j)
                    {
                        double sourcePos = j / ratio;
                        int pos1 = (int)sourcePos;
                        int pos2 = juce::jmin(pos1 + 1, outputLength - 1);
                        float frac = (float)(sourcePos - pos1);

                        dest[j] = source[pos1] * (1.0f - frac) + source[pos2] * frac;
                    }
                }

                // Write resampled audio
                writer->writeFromAudioSampleBuffer(resampledBuffer, 0, resampledBuffer.getNumSamples());
            }
            else
            {
                // Write audio directly
                writer->writeFromAudioSampleBuffer(sliceBuffer, 0, sliceBuffer.getNumSamples());
            }

            writer.reset();
        }
    }

    return true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SlicerAudioProcessor();
}