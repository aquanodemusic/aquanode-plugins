#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Constructor
//==============================================================================

GrainfreezeAudioProcessor::GrainfreezeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Normalized playback position, host-automatable
    addParameter(playheadParam = new juce::AudioParameterFloat(
        "playhead", "Playhead",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.0f));

    // Time stretching factor (0.1 = 10x faster, 1.0 = normal, 4.0 = 4x slower)
    addParameter(timeStretch = new juce::AudioParameterFloat(
        "timeStretch", "Time Stretch",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f, 0.5f),
        1.0f));

    // Grain size (reserved for future enhancements)
    addParameter(grainSizeParam = new juce::AudioParameterFloat(
        "grainSize", "Grain Size",
        juce::NormalisableRange<float>(512.0f, 8192.0f, 1.0f, 0.3f),
        2048.0f));

    // Hop size as divisor of FFT size (lower = more overlap = smoother)
    addParameter(hopSizeParam = new juce::AudioParameterFloat(
        "hopSize", "Hop Size",
        juce::NormalisableRange<float>(2.0f, 16.0f, 0.5f),
        4.0f));

    // FFT Size (larger = better frequency resolution, more latency)
    juce::StringArray fftSizeChoices;
    fftSizeChoices.add("512");
    fftSizeChoices.add("1024");
    fftSizeChoices.add("2048");
    fftSizeChoices.add("4096");
    fftSizeChoices.add("8192");
    fftSizeChoices.add("16384");
    fftSizeChoices.add("32768");
    fftSizeChoices.add("65536");

    addParameter(fftSizeParam = new juce::AudioParameterChoice(
        "fftSize", "FFT Size",
        fftSizeChoices,
        3));

    // Freeze mode toggle
    addParameter(freezeModeParam = new juce::AudioParameterBool(
        "freezeMode", "Freeze Mode",
        false));

    // Glide time for freeze mode position changes (0-1000ms)
    addParameter(glideParam = new juce::AudioParameterFloat(
        "glide", "Glide",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 1.0f, 0.5f),
        100.0f));

    // Freeze algorithm choice: Spectral (original) or Cepstral (smoother)
    juce::StringArray freezeTypeChoices;
    freezeTypeChoices.add("Spectral");
    freezeTypeChoices.add("Cepstral");

    addParameter(freezeTypeParam = new juce::AudioParameterChoice(
        "freezeType", "Freeze Type",
        freezeTypeChoices,
        1)); // default: Cepstral (smoother, less phasey)

    // Cepstral lifter cutoff (0-100%), used only in Cepstral freeze type
    addParameter(characterParam = new juce::AudioParameterFloat(
        "character", "Character",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        50.0f));

    // Stereo analysis toggle (default on: independent per-channel FFT chain
    // when a stereo source is loaded; still mixes to mono if the source is mono)
    addParameter(stereoModeParam = new juce::AudioParameterBool(
        "stereoMode", "Stereo",
        true));

    // Phase lock toggle (default on: locks each spectral neighbourhood to its
    // peak bin's phase, keeping transients sharper)
    addParameter(phaseLockParam = new juce::AudioParameterBool(
        "phaseLock", "Phase Lock",
        true));

    // High-frequency boost to compensate for phase vocoder roll-off (0-100%)
    addParameter(hfBoostParam = new juce::AudioParameterFloat(
        "hfBoost", "HF Boost",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        10.0f));

    // Micro-movement amount in freeze mode to reduce artifacts (0-100%)
    addParameter(microMovementParam = new juce::AudioParameterFloat(
        "microMovement", "Micro Movement",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        20.0f));

    // Window function type selection
    juce::StringArray windowChoices;
    windowChoices.add("Hann");
    windowChoices.add("Blackman-Harris");

    addParameter(windowTypeParam = new juce::AudioParameterChoice(
        "windowType", "Window Type",
        windowChoices,
        1));

    // Crossfade length for smooth playhead jumps (1-8 hops)
    addParameter(crossfadeLengthParam = new juce::AudioParameterFloat(
        "crossfadeLength", "Crossfade Length",
        juce::NormalisableRange<float>(1.0f, 8.0f, 0.5f),
        2.0f));

    updateFftSize();
}

GrainfreezeAudioProcessor::~GrainfreezeAudioProcessor()
{
}

//==============================================================================
// Plugin Info
//==============================================================================

const juce::String GrainfreezeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GrainfreezeAudioProcessor::acceptsMidi() const { return true; }
bool GrainfreezeAudioProcessor::producesMidi() const { return false; }
bool GrainfreezeAudioProcessor::isMidiEffect() const { return false; }
double GrainfreezeAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int GrainfreezeAudioProcessor::getNumPrograms() { return 1; }
int GrainfreezeAudioProcessor::getCurrentProgram() { return 0; }
void GrainfreezeAudioProcessor::setCurrentProgram(int index) {}
const juce::String GrainfreezeAudioProcessor::getProgramName(int index) { return {}; }
void GrainfreezeAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

//==============================================================================
// Audio Processing Setup
//==============================================================================

void GrainfreezeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    playbackPosition = 0.0;
    outputWritePos = 0;

    // Initialize freeze mode smoothing
    smoothedFreezePosition.reset(sampleRate, 0.1);
    smoothedFreezePosition.setCurrentAndTargetValue(0.0);
    freezeCurrentPosition = 0.0;
    freezeTargetPosition = 0.0;
    freezeMicroMovement = 0.0f;
    freezeMicroCounter = 0;

    // Clear all processing buffers (both channel slots)
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(previousPhase[ch].begin(), previousPhase[ch].end(), 0.0f);
        std::fill(synthesisPhase[ch].begin(), synthesisPhase[ch].end(), 0.0f);
    }
}

void GrainfreezeAudioProcessor::releaseResources()
{
}

bool GrainfreezeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Only support stereo output
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
// Main Audio Processing
//==============================================================================

void GrainfreezeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    //==========================================================================
    // MIDI Note Handling
    //
    // A held note starts/pitches playback exactly as if the Play button were
    // held down -- it works in both normal and freeze mode -- pitched
    // relative to C4 (MIDI note 60) as the root. Last-note-priority: letting
    // go of the most recently pressed note falls back to any other still-held
    // note, or stops playback if none remain.
    //==========================================================================
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            auto note = message.getNoteNumber();
            heldMidiNotes.erase(std::remove(heldMidiNotes.begin(), heldMidiNotes.end(), note), heldMidiNotes.end());
            heldMidiNotes.push_back(note);

            currentMidiNote = note;
            midiPitchRatio = std::pow(2.0f, static_cast<float>(currentMidiNote - 60) / 12.0f);
            midiNoteActive = true;
            pitchInterpolatorL.reset();
            pitchInterpolatorR.reset();
            setPlaying(true);
        }
        else if (message.isNoteOff())
        {
            auto note = message.getNoteNumber();
            heldMidiNotes.erase(std::remove(heldMidiNotes.begin(), heldMidiNotes.end(), note), heldMidiNotes.end());

            if (note == currentMidiNote)
            {
                if (!heldMidiNotes.empty())
                {
                    currentMidiNote = heldMidiNotes.back();
                    midiPitchRatio = std::pow(2.0f, static_cast<float>(currentMidiNote - 60) / 12.0f);
                    pitchInterpolatorL.reset();
                    pitchInterpolatorR.reset();
                }
                else
                {
                    midiNoteActive = false;
                    midiPitchRatio = 1.0f;
                    setPlaying(false);
                }
            }
        }
    }

    if (!audioLoaded)
        return;

    // Even while stopped, keep the playhead parameter responsive to host
    // automation/session restore so scrubbing/automation works before the
    // user hits play, not just during playback.
    if (!playing)
    {
        float paramValue = playheadParam->get();
        if (!userScrubbing && std::abs(paramValue - lastSyncedPlayheadValue) > 1.0e-6f)
        {
            setPlayheadPosition(paramValue, true);
        }
        lastSyncedPlayheadValue = paramValue;
        return;
    }

    processTimeStretch(buffer, buffer.getNumSamples());
}

//==============================================================================
// Time Stretching and Freeze Mode
//==============================================================================

void GrainfreezeAudioProcessor::processTimeStretch(juce::AudioBuffer<float>& outputBuffer, int numSamples)
{
    // Check if FFT size changed (requires buffer reallocation)
    static int lastFftSizeIndex = -1;
    bool fftSizeChanged = (fftSizeParam->getIndex() != lastFftSizeIndex);

    if (fftSizeChanged)
    {
        lastFftSizeIndex = fftSizeParam->getIndex();
        updateFftSize();
    }

    // Check if hop size changed (updates immediately for responsive control)
    static float lastHopSize = -1.0f;
    float currentHopParam = hopSizeParam->get();

    if (currentHopParam != lastHopSize)
    {
        lastHopSize = currentHopParam;
        updateHopSize();

        // Clear accumulator to prevent artifacts
        for (int ch = 0; ch < 2; ++ch)
            std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        outputWritePos = 0;
        grainCounter = 0;
    }

    // Sync engine <-> playheadParam. If the parameter's value changed since
    // we last looked at it (host automation, or a saved session restoring
    // it) and the user isn't actively scrubbing the UI right now, seek the
    // engine to match. While the user IS scrubbing, the UI is already the
    // source of truth (it calls setPlayheadPosition() directly), so skip
    // this to avoid fighting the drag.
    float playheadParamValue = playheadParam->get();
    if (!userScrubbing && std::abs(playheadParamValue - lastSyncedPlayheadValue) > 1.0e-6f)
    {
        setPlayheadPosition(playheadParamValue, true);
    }
    lastSyncedPlayheadValue = playheadParamValue;

    // Update active channel count: stereo processing needs both a stereo
    // source and the Stereo toggle on; otherwise mono-mixdown as before
    activeChannelCount = (stereoModeParam->get() && loadedAudio.getNumChannels() > 1) ? 2 : 1;

    // Check if window type changed (updates window function)
    static int lastWindowType = -1;
    int currentWindowType = windowTypeParam->getIndex();

    if (currentWindowType != lastWindowType)
    {
        lastWindowType = currentWindowType;
        createWindow();
    }

    // Update freeze mode state
    isInFreezeMode = freezeModeParam->get();

    // Update glide time when changed
    static float lastGlideTime = -1.0f;
    float currentGlideTime = glideParam->get();
    if (currentGlideTime != lastGlideTime)
    {
        lastGlideTime = currentGlideTime;
        smoothedFreezePosition.reset(currentSampleRate, currentGlideTime / 1000.0);
    }

    // Get time stretch factor and calculate playback speed. The ratio between
    // the loaded file's native sample rate and the current host/DAW sample
    // rate keeps pitch correct regardless of what rate the session runs at --
    // without it, a file whose native rate differs from the host rate plays
    // back pitch-shifted even with time stretch at 1.0.
    float stretch = timeStretch->get();
    float sampleRateRatio = static_cast<float>(loadedFileSampleRate / currentSampleRate);
    float playbackSpeed = sampleRateRatio / stretch;

    //==========================================================================
    // MIDI Pitch Shifting
    //
    // Applies uniformly to normal and freeze-mode output: the engine below
    // runs for a variable number of "engine samples" (numSamples /
    // midiPitchRatio) into a scratch buffer, then that scratch buffer is
    // resampled back to the block's real sample count. This changes pitch
    // without touching the OLA/grain-timing internals, which stay exactly as
    // they were for the no-MIDI (built-in Play button) case.
    //==========================================================================
    float pitchRatio = (midiNoteActive && midiPitchRatio > 0.0f) ? midiPitchRatio : 1.0f;
    int numEngineSamples = (pitchRatio != 1.0f)
        ? juce::jmax(1, static_cast<int>(std::ceil(numSamples * pitchRatio)))
        : numSamples;

    pitchScratchL.resize(static_cast<size_t>(numEngineSamples));
    pitchScratchR.resize(static_cast<size_t>(numEngineSamples));
    juce::AudioBuffer<float> engineBuffer(2, numEngineSamples);
    engineBuffer.clear();

    // FREEZE MODE processing
    if (isInFreezeMode)
    {
        // Set target position for smooth gliding
        smoothedFreezePosition.setTargetValue(freezeTargetPosition);

        for (int sample = 0; sample < numEngineSamples; ++sample)
        {
            // Smoothly glide toward target position
            freezeCurrentPosition = smoothedFreezePosition.getNextValue();

            // Add micro-movement to reduce static artifacts
            freezeMicroCounter++;
            if (freezeMicroCounter >= currentHopSize / 4)
            {
                freezeMicroCounter = 0;
                // Scale random movement by parameter (0-100%)
                float movementAmount = microMovementParam->get() / 100.0f;
                freezeMicroMovement = (static_cast<float>(rand()) / RAND_MAX - 0.5f) *
                    0.0002f * movementAmount;
            }

            // Apply playback position with micro-movement
            playbackPosition = freezeCurrentPosition + (freezeMicroMovement * loadedAudio.getNumSamples());

            // Wrap position within audio bounds
            if (playbackPosition >= loadedAudio.getNumSamples())
                playbackPosition = fmod(playbackPosition, static_cast<double>(loadedAudio.getNumSamples()));
            if (playbackPosition < 0.0)
                playbackPosition = 0.0;

            // Process new grain when counter reaches zero
            if (grainCounter == 0)
            {
                performPhaseVocoder();
                grainCounter = currentHopSize;
            }

            // Read output sample(s) from the accumulator. In mono processing
            // (or a mono source) both output channels get the same sample,
            // same as originally; in stereo processing each output channel
            // gets its own independently-analysed accumulator.
            float outL = 0.0f, outR = 0.0f;
            if (outputWritePos < outputAccum[0].size())
            {
                outL = outputAccum[0][outputWritePos];
                outputAccum[0][outputWritePos] = 0.0f;
            }
            if (activeChannelCount == 2 && outputWritePos < outputAccum[1].size())
            {
                outR = outputAccum[1][outputWritePos];
                outputAccum[1][outputWritePos] = 0.0f;
            }
            else
            {
                outR = outL;
            }

            engineBuffer.setSample(0, sample, outL);
            engineBuffer.setSample(1, sample, outR);

            outputWritePos = (outputWritePos + 1) % outputAccum[0].size();
            grainCounter--;

            // Update UI playhead position
            playheadPosition.store(static_cast<float>(freezeCurrentPosition) / loadedAudio.getNumSamples());
        }
    }
    // NORMAL PLAYBACK MODE
    else
    {
        for (int sample = 0; sample < numEngineSamples; ++sample)
        {
            // Process new grain when counter reaches zero
            if (grainCounter == 0)
            {
                performPhaseVocoder();
                grainCounter = currentHopSize;
            }

            // Read output sample(s) from the accumulator (see comment in the
            // freeze-mode block above for mono/stereo behaviour)
            float outL = 0.0f, outR = 0.0f;
            if (outputWritePos < outputAccum[0].size())
            {
                outL = outputAccum[0][outputWritePos];
                outputAccum[0][outputWritePos] = 0.0f;
            }
            if (activeChannelCount == 2 && outputWritePos < outputAccum[1].size())
            {
                outR = outputAccum[1][outputWritePos];
                outputAccum[1][outputWritePos] = 0.0f;
            }
            else
            {
                outR = outL;
            }

            engineBuffer.setSample(0, sample, outL);
            engineBuffer.setSample(1, sample, outR);

            outputWritePos = (outputWritePos + 1) % outputAccum[0].size();
            grainCounter--;

            // Advance playback position
            playbackPosition += playbackSpeed;

            // Wrap playback position
            if (playbackPosition >= loadedAudio.getNumSamples())
                playbackPosition = fmod(playbackPosition, static_cast<double>(loadedAudio.getNumSamples()));

            // Update UI playhead position
            playheadPosition.store(static_cast<float>(playbackPosition) / loadedAudio.getNumSamples());
        }
    }

    // Resample the engine's output (numEngineSamples) back to the block's
    // real sample count (numSamples). At pitchRatio == 1 (no MIDI note, or a
    // note exactly at C4) this is a plain 1:1 copy; otherwise JUCE's
    // Lagrange interpolator performs the pitch-shifting resample.
    if (pitchRatio == 1.0f)
    {
        if (outputBuffer.getNumChannels() > 0)
            outputBuffer.copyFrom(0, 0, engineBuffer, 0, 0, numSamples);
        if (outputBuffer.getNumChannels() > 1)
            outputBuffer.copyFrom(1, 0, engineBuffer, 1, 0, numSamples);
    }
    else
    {
        double resampleRatio = static_cast<double>(numEngineSamples) / static_cast<double>(numSamples);
        if (outputBuffer.getNumChannels() > 0)
            pitchInterpolatorL.process(resampleRatio, engineBuffer.getReadPointer(0), outputBuffer.getWritePointer(0), numSamples);
        if (outputBuffer.getNumChannels() > 1)
            pitchInterpolatorR.process(resampleRatio, engineBuffer.getReadPointer(1), outputBuffer.getWritePointer(1), numSamples);
    }

    // Push the engine's current position back into the automatable parameter
    // once per block (not every sample -- that would spam the host) so the
    // host can display and record it, e.g. while normal playback advances on
    // its own. Skip while the user is scrubbing, since the UI already writes
    // the parameter directly via beginChangeGesture/setValueNotifyingHost.
    if (!userScrubbing)
    {
        float currentNormalized = playheadPosition.load();
        if (std::abs(currentNormalized - lastSyncedPlayheadValue) > 1.0e-6f)
        {
            playheadParam->setValueNotifyingHost(currentNormalized);
            lastSyncedPlayheadValue = currentNormalized;
        }
    }
}

//==============================================================================
// Phase Vocoder (main DSP algorithm)
//==============================================================================

void GrainfreezeAudioProcessor::performPhaseVocoder()
{
    if (!audioLoaded)
        return;

    // Calculate read position and clamp to valid bounds
    int readPos = static_cast<int>(playbackPosition);
    readPos = juce::jlimit(0, loadedAudio.getNumSamples() - currentFftSize, readPos);

    bool sourceIsStereo = loadedAudio.getNumChannels() > 1;
    bool stereoProcessing = (activeChannelCount == 2);

    int numBins = currentFftSize / 2 + 1;
    if (spectrumMagnitudes.size() != numBins)
        spectrumMagnitudes.resize(numBins);

    bool useCepstral = (freezeTypeParam->getIndex() == 1);
    bool phaseLock = phaseLockParam->get();
    float characterNorm = characterParam->get() / 100.0f;
    float stretch = timeStretch->get();
    float expectedPhaseAdvance = juce::MathConstants<float>::twoPi * currentHopSize / currentFftSize;
    float hfBoostAmount = hfBoostParam->get() / 100.0f;
    float overlapFactor = static_cast<float>(currentFftSize) / currentHopSize;
    float normalization = 2.0f / overlapFactor;

    for (int ch = 0; ch < activeChannelCount; ++ch)
    {
        // Stereo processing: each channel reads its own source channel with
        // no mixdown. Mono processing (default): mix both source channels
        // down to one, exactly as originally -- only ever runs for ch == 0.
        const float* sourceData = loadedAudio.getReadPointer(stereoProcessing ? ch : 0);
        const float* sourceDataR = (!stereoProcessing && sourceIsStereo) ? loadedAudio.getReadPointer(1) : nullptr;

        auto& frame = analysisFrame[(size_t) ch];
        auto& fftBuf = fftBuffer[(size_t) ch];
        auto& prevPhase = previousPhase[(size_t) ch];
        auto& synPhase = synthesisPhase[(size_t) ch];
        auto& accum = outputAccum[(size_t) ch];

        // Fill analysis frame with windowed samples
        for (int i = 0; i < currentFftSize; ++i)
        {
            int sampleIndex = readPos + i;
            if (sampleIndex < loadedAudio.getNumSamples())
            {
                float sample = sourceData[sampleIndex];

                if (sourceDataR != nullptr)
                    sample = (sample + sourceDataR[sampleIndex]) * 0.5f;

                frame[(size_t) i] = sample * window[(size_t) i];
            }
            else
            {
                frame[(size_t) i] = 0.0f;
            }
        }

        // Copy to FFT buffer
        std::copy(frame.begin(), frame.end(), fftBuf.begin());

        // Forward FFT (time domain to frequency domain)
        fftAnalysis->performRealOnlyForwardTransform(fftBuf.data(), true);

        // --- Pass 1: extract raw magnitude/phase for every bin -------------
        std::vector<float> rawMagnitude((size_t) numBins);
        std::vector<float> binPhase((size_t) numBins);
        for (int bin = 0; bin < numBins; ++bin)
        {
            float real = fftBuf[(size_t) bin * 2];
            float imag = fftBuf[(size_t) bin * 2 + 1];

            rawMagnitude[(size_t) bin] = std::sqrt(real * real + imag * imag);
            binPhase[(size_t) bin] = std::atan2(imag, real);

            // Store magnitude for visualization (channel 0 only)
            if (ch == 0)
                spectrumMagnitudes[(size_t) bin] = rawMagnitude[(size_t) bin];
        }

        // Cepstral algorithm: replace the raw per-bin magnitude with a
        // smoothed spectral envelope (homomorphic liftering of the
        // log-magnitude spectrum) before resynthesis -- removes the spiky,
        // "buzzy"/phasey artifacts the raw phase vocoder otherwise produces.
        // Computed independently per channel in stereo processing.
        std::vector<float> cepstralMagnitude;
        if (useCepstral)
        {
            std::vector<float> logMag((size_t) numBins);
            for (int bin = 0; bin < numBins; ++bin)
                logMag[(size_t) bin] = std::log(juce::jmax(1.0e-8f, rawMagnitude[(size_t) bin]));

            std::vector<float> smoothedLogMag;
            cepstralSmoothMagnitude(logMag, smoothedLogMag, *fftAnalysis, currentFftSize, characterNorm);

            cepstralMagnitude.resize((size_t) numBins);
            for (int bin = 0; bin < numBins; ++bin)
                cepstralMagnitude[(size_t) bin] = std::exp(smoothedLogMag[(size_t) bin]);
        }

        // --- Pass 2: independent per-bin phase vocoder update --------------
        // This is the original, deliberately "smeared" behaviour -- every
        // bin's synthesis phase evolves purely from its own instantaneous
        // frequency estimate. Phase Lock (below) then overrides non-peak
        // bins so transients stay coherent instead.
        std::vector<float> independentPhase((size_t) numBins);
        for (int bin = 0; bin < numBins; ++bin)
        {
            float phase = binPhase[(size_t) bin];
            float phaseDiff = phase - prevPhase[(size_t) bin];
            prevPhase[(size_t) bin] = phase;

            float deviation = phaseDiff - (bin * expectedPhaseAdvance);
            while (deviation > juce::MathConstants<float>::pi)
                deviation -= juce::MathConstants<float>::twoPi;
            while (deviation < -juce::MathConstants<float>::pi)
                deviation += juce::MathConstants<float>::twoPi;

            float trueFreq = bin * expectedPhaseAdvance + deviation;
            synPhase[(size_t) bin] += trueFreq * stretch;

            while (synPhase[(size_t) bin] > juce::MathConstants<float>::pi)
                synPhase[(size_t) bin] -= juce::MathConstants<float>::twoPi;
            while (synPhase[(size_t) bin] < -juce::MathConstants<float>::pi)
                synPhase[(size_t) bin] += juce::MathConstants<float>::twoPi;

            independentPhase[(size_t) bin] = synPhase[(size_t) bin];
        }

        // --- Phase Lock (identity phase locking) ----------------------------
        // Rigidly locks each spectral peak's neighbouring bins to that
        // peak's own (independently-evolved) phase, using this hop's raw
        // analysis phase relationship between the peak and each neighbour.
        // On by default (keeps transients sharper); switch it off if you
        // want the smeared, more diffuse texture that independent per-bin
        // phase evolution gives for freeze textures and slow stretches.
        if (phaseLock)
        {
            const std::vector<float>& magForPeaks = useCepstral ? cepstralMagnitude : rawMagnitude;

            std::vector<int> peaks;
            for (int bin = 0; bin < numBins; ++bin)
            {
                float m = magForPeaks[(size_t) bin];
                float left = (bin > 0) ? magForPeaks[(size_t) bin - 1] : -1.0f;
                float right = (bin < numBins - 1) ? magForPeaks[(size_t) bin + 1] : -1.0f;
                if (m >= left && m >= right)
                    peaks.push_back(bin);
            }
            if (peaks.empty())
                peaks.push_back(0);

            int peakCursor = 0;
            for (int bin = 0; bin < numBins; ++bin)
            {
                while (peakCursor + 1 < (int) peaks.size()
                       && std::abs(peaks[(size_t) peakCursor + 1] - bin) <= std::abs(peaks[(size_t) peakCursor] - bin))
                    ++peakCursor;

                int p = peaks[(size_t) peakCursor];
                if (bin == p)
                    continue; // peak bins keep their independent evolution

                float locked = independentPhase[(size_t) p] + (binPhase[(size_t) bin] - binPhase[(size_t) p]);
                while (locked > juce::MathConstants<float>::pi)
                    locked -= juce::MathConstants<float>::twoPi;
                while (locked < -juce::MathConstants<float>::pi)
                    locked += juce::MathConstants<float>::twoPi;

                synPhase[(size_t) bin] = locked;
            }
        }

        // --- Pass 3: reconstruct complex spectrum ---------------------------
        for (int bin = 0; bin < numBins; ++bin)
        {
            float magnitude = useCepstral ? cepstralMagnitude[(size_t) bin] : rawMagnitude[(size_t) bin];

            if (!useCepstral)
            {
                // Apply high-frequency boost (scaled by parameter). Only
                // meaningful for the raw/Spectral magnitude -- once the
                // envelope has been cepstrally smoothed, the Character
                // control (which repurposes this same knob) already shapes
                // brightness/detail directly.
                float freqRatio = static_cast<float>(bin) / (currentFftSize / 2);
                float hfBoost = 1.0f + (freqRatio * hfBoostAmount);
                magnitude *= hfBoost;
            }

            fftBuf[(size_t) bin * 2] = magnitude * std::cos(synPhase[(size_t) bin]);
            fftBuf[(size_t) bin * 2 + 1] = magnitude * std::sin(synPhase[(size_t) bin]);
        }

        // Inverse FFT (frequency domain to time domain)
        fftSynthesis->performRealOnlyInverseTransform(fftBuf.data());

        // Overlap-add to output accumulator with windowing and normalization
        for (int i = 0; i < currentFftSize; ++i)
        {
            int outIndex = (outputWritePos + i) % (int) accum.size();
            accum[(size_t) outIndex] += fftBuf[(size_t) i] * window[(size_t) i] * normalization;
        }
    }
}

//==============================================================================
// Cepstral Lifter (Cepstral freeze type)
//==============================================================================

void GrainfreezeAudioProcessor::cepstralSmoothMagnitude(const std::vector<float>& logMag,
                                                          std::vector<float>& outSmoothedLogMag,
                                                          juce::dsp::FFT& fftEngine, int fftSize,
                                                          float characterNorm)
{
    const int half = fftSize / 2;
    std::vector<juce::dsp::Complex<float>> buf((size_t) fftSize), tmp((size_t) fftSize);

    // Mirror the log-magnitude spectrum into a full symmetric spectrum and
    // take its inverse FFT to get the real cepstrum.
    for (int i = 0; i <= half; ++i)          buf[(size_t) i] = { logMag[(size_t) i], 0.0f };
    for (int i = half + 1; i < fftSize; ++i) buf[(size_t) i] = { logMag[(size_t) (fftSize - i)], 0.0f };
    fftEngine.perform(buf.data(), tmp.data(), true); // inverse -> real cepstrum

    std::vector<float> cep((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        cep[(size_t) i] = tmp[(size_t) i].real();

    // Keep only the low-quefrency coefficients (the coarse spectral
    // envelope) and discard the rest -- that's what removes the fine
    // harmonic/noise detail responsible for phase-vocoder buzz when a grain
    // is held nearly static. characterNorm sets how many coefficients survive:
    // low = only the coarsest envelope (smoothest, least artifacts), high =
    // closer to the grain's full detail.
    std::vector<float> lift((size_t) fftSize, 0.0f);
    const int minK = 2, maxK = half;
    const int k = juce::jlimit(minK, maxK,
        (int) juce::jmap(characterNorm, 0.0f, 1.0f, (float) minK, (float) maxK));

    lift[0] = cep[0];
    for (int i = 1; i < k; ++i)
        lift[(size_t) i] = cep[(size_t) i] * 2.0f;
    if (k >= half) lift[(size_t) half] = cep[(size_t) half];

    // Forward FFT back to get the smoothed log-magnitude spectrum
    for (int i = 0; i < fftSize; ++i)
        buf[(size_t) i] = { lift[(size_t) i], 0.0f };
    fftEngine.perform(buf.data(), tmp.data(), false); // forward -> smoothed log spectrum

    outSmoothedLogMag.resize((size_t) half + 1);
    for (int i = 0; i <= half; ++i)
        outSmoothedLogMag[(size_t) i] = tmp[(size_t) i].real(); // real part only; phase discarded
}

//==============================================================================
// FFT Configuration
//==============================================================================

void GrainfreezeAudioProcessor::updateFftSize()
{
    // Map parameter index to actual FFT size
    int fftSizes[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
    int selectedIndex = fftSizeParam->getIndex();
    currentFftSize = fftSizes[selectedIndex];

    // Update hop size based on new FFT size
    updateHopSize();

    // Calculate FFT order (log2 of size)
    int fftOrder = 0;
    int temp = currentFftSize;
    while (temp > 1)
    {
        temp >>= 1;
        fftOrder++;
    }

    // Create FFT objects
    fftAnalysis = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);

    // Resize all buffers (both channel slots, so switching Stereo on
    // mid-session doesn't need a reallocation)
    for (int ch = 0; ch < 2; ++ch)
    {
        fftBuffer[ch].resize(currentFftSize * 2, 0.0f);
        analysisFrame[ch].resize(currentFftSize, 0.0f);
        outputAccum[ch].resize(currentFftSize * 8, 0.0f);
        previousPhase[ch].resize(currentFftSize / 2 + 1, 0.0f);
        synthesisPhase[ch].resize(currentFftSize / 2 + 1, 0.0f);
    }
    synthesisFrame.resize(currentFftSize, 0.0f);
    crossfadeBuffer.resize(currentFftSize * 2, 0.0f);
    window.resize(currentFftSize);

    // Create window function
    createWindow();

    // Reset all state
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(previousPhase[ch].begin(), previousPhase[ch].end(), 0.0f);
        std::fill(synthesisPhase[ch].begin(), synthesisPhase[ch].end(), 0.0f);
    }
    std::fill(crossfadeBuffer.begin(), crossfadeBuffer.end(), 0.0f);

    outputWritePos = 0;
    grainCounter = 0;
    needsCrossfade = false;
    crossfadeCounter = 0;

    // Update crossfade length based on parameter
    crossfadeSamples = static_cast<int>(currentHopSize * crossfadeLengthParam->get());
}

void GrainfreezeAudioProcessor::updateHopSize()
{
    // Calculate hop size from FFT size and divisor parameter
    float hopDivisor = hopSizeParam->get();
    currentHopSize = static_cast<int>(currentFftSize / hopDivisor);

    // Ensure minimum hop size
    currentHopSize = juce::jmax(1, currentHopSize);
}

//==============================================================================
// Window Functions
//==============================================================================

void GrainfreezeAudioProcessor::createWindow()
{
    // Create window based on selected type
    int windowType = windowTypeParam->getIndex();

    if (windowType == 0)
    {
        createHannWindow();
    }
    else
    {
        createBlackmanHarrisWindow();
    }
}

void GrainfreezeAudioProcessor::createHannWindow()
{
    // Standard Hann window (good general purpose)
    for (int i = 0; i < currentFftSize; ++i)
    {
        float n = static_cast<float>(i) / (currentFftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void GrainfreezeAudioProcessor::createBlackmanHarrisWindow()
{
    // Blackman-Harris window (better frequency resolution)
    for (int i = 0; i < currentFftSize; ++i)
    {
        float n = static_cast<float>(i) / (currentFftSize - 1);

        // 4-term Blackman-Harris coefficients
        const float a0 = 0.35875f;
        const float a1 = 0.48829f;
        const float a2 = 0.14128f;
        const float a3 = 0.01168f;

        window[i] = a0
            - a1 * std::cos(2.0f * juce::MathConstants<float>::pi * n)
            + a2 * std::cos(4.0f * juce::MathConstants<float>::pi * n)
            - a3 * std::cos(6.0f * juce::MathConstants<float>::pi * n);
    }
}

//==============================================================================
// Audio File Loading
//==============================================================================

void GrainfreezeAudioProcessor::loadAudioFile(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        // Read audio file into buffer
        loadedAudio.setSize(static_cast<int>(reader->numChannels),
            static_cast<int>(reader->lengthInSamples));
        reader->read(&loadedAudio, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

        // Remember the file's own sample rate. Sample data is read as-is
        // (no resampling), so if this differs from the host/DAW sample rate
        // we need to compensate the playback speed or the file will play
        // back pitch-shifted (e.g. a 48kHz file loaded into a 44.1kHz
        // session will play ~8.8% sharp/fast unless corrected).
        loadedFileSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : currentSampleRate;

        // Reset playback state
        audioLoaded = true;
        playheadPosition.store(0.0f);
        playbackPosition = 0.0;
        freezeCurrentPosition = 0.0;
        freezeTargetPosition = 0.0;
        smoothedFreezePosition.setCurrentAndTargetValue(0.0);
        grainCounter = 0;

        // Reset phase vocoder state (both channel slots)
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(previousPhase[ch].begin(), previousPhase[ch].end(), 0.0f);
            std::fill(synthesisPhase[ch].begin(), synthesisPhase[ch].end(), 0.0f);
        }
    }
}

//==============================================================================
// Playback Control
//==============================================================================

void GrainfreezeAudioProcessor::setPlayheadPosition(float normalizedPosition, bool fromParameter)
{
    float clampedPosition = juce::jlimit(0.0f, 1.0f, normalizedPosition);

    // In freeze mode, set target for smooth gliding
    if (isInFreezeMode || freezeModeParam->get())
    {
        freezeTargetPosition = clampedPosition * loadedAudio.getNumSamples();
    }
    else
    {
        // In normal mode, jump immediately
        playbackPosition = clampedPosition * loadedAudio.getNumSamples();
        playheadPosition.store(clampedPosition);
    }

    // Keep the automatable parameter in sync, unless this call came from the
    // parameter syncing itself (host automation/session restore) -- in that
    // case it's already at this value, so writing it back would be redundant
    // and would perturb lastSyncedPlayheadValue bookkeeping.
    if (!fromParameter)
    {
        playheadParam->setValueNotifyingHost(clampedPosition);
        lastSyncedPlayheadValue = clampedPosition;
    }
}

void GrainfreezeAudioProcessor::setPlaying(bool shouldPlay)
{
    playing = shouldPlay;
}

//==============================================================================
// Editor
//==============================================================================

bool GrainfreezeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* GrainfreezeAudioProcessor::createEditor()
{
    return new GrainfreezeAudioProcessorEditor(*this);
}

//==============================================================================
// State Persistence
//==============================================================================

void GrainfreezeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save all parameter states
    auto state = juce::ValueTree("GrainfreezeState");

    state.setProperty("playhead", playheadParam->get(), nullptr);
    state.setProperty("timeStretch", timeStretch->get(), nullptr);
    state.setProperty("hopSize", hopSizeParam->get(), nullptr);
    state.setProperty("fftSize", fftSizeParam->getIndex(), nullptr);
    state.setProperty("freezeMode", freezeModeParam->get(), nullptr);
    state.setProperty("glide", glideParam->get(), nullptr);
    state.setProperty("freezeType", freezeTypeParam->getIndex(), nullptr);
    state.setProperty("character", characterParam->get(), nullptr);
    state.setProperty("stereoMode", stereoModeParam->get(), nullptr);
    state.setProperty("phaseLock", phaseLockParam->get(), nullptr);
    state.setProperty("hfBoost", hfBoostParam->get(), nullptr);
    state.setProperty("microMovement", microMovementParam->get(), nullptr);
    state.setProperty("windowType", windowTypeParam->getIndex(), nullptr);
    state.setProperty("crossfadeLength", crossfadeLengthParam->get(), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void GrainfreezeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore all parameter states
    auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));

    if (state.isValid())
    {
        *playheadParam = state.getProperty("playhead", 0.0f);
        *timeStretch = state.getProperty("timeStretch", 1.0f);
        *hopSizeParam = state.getProperty("hopSize", 4.0f);
        fftSizeParam->operator=(state.getProperty("fftSize", 3));
        *freezeModeParam = state.getProperty("freezeMode", false);
        *glideParam = state.getProperty("glide", 100.0f);
        freezeTypeParam->operator=(state.getProperty("freezeType", 0));
        *characterParam = state.getProperty("character", 50.0f);
        *stereoModeParam = state.getProperty("stereoMode", false);
        *phaseLockParam = state.getProperty("phaseLock", false);
        *hfBoostParam = state.getProperty("hfBoost", 10.0f);
        *microMovementParam = state.getProperty("microMovement", 20.0f);
        windowTypeParam->operator=(state.getProperty("windowType", 1));
        *crossfadeLengthParam = state.getProperty("crossfadeLength", 2.0f);
    }
}

//==============================================================================
// Plugin Factory
//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrainfreezeAudioProcessor();
}