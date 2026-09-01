#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

//==============================================================================
FullFilterAudioProcessor::FullFilterAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    rootParam = apvts.getRawParameterValue("root");
    levelParam = apvts.getRawParameterValue("level");
    lowpassParam = apvts.getRawParameterValue("lowpass");
    qParam = apvts.getRawParameterValue("q");
    amountParam = apvts.getRawParameterValue("amount");
    glideParam = apvts.getRawParameterValue("glide");
    polyphonyParam = apvts.getRawParameterValue("polyphony");
    attackParam = apvts.getRawParameterValue("attack");
    decayParam = apvts.getRawParameterValue("decay");
    sustainParam = apvts.getRawParameterValue("sustain");
    releaseParam = apvts.getRawParameterValue("release");
    adsrEnabledParam = apvts.getRawParameterValue("adsrEnabled");

    rootRangedParam = apvts.getParameter("root");
    lastKnownRootParamValue = rootParam->load();

    wavetableFormatManager.registerBasicFormats();
    wavetableFFTScratch.resize((size_t)wavetableFrameSize * 2);
    wavetableModeParam = apvts.getRawParameterValue("wavetableMode");
    apvts.addParameterListener("wavetablePosition", this);
    apvts.addParameterListener("wavetableMode", this);

    heldNotes.reserve(16);

    for (auto& voice : voices)
        for (auto& c : voice.coefficients)
            c = new Coeffs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); // unity/passthrough

    for (int n = 0; n < maxBells; ++n)
    {
        bellVolumeMult[(size_t)n].store(1.0f);
        bellFreqMult[(size_t)n].store(1.0f);
        bellManualMute[(size_t)n].store(false);
    }
}

FullFilterAudioProcessor::~FullFilterAudioProcessor()
{
    apvts.removeParameterListener("wavetablePosition", this);
    apvts.removeParameterListener("wavetableMode", this);
}

void FullFilterAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);
    if (parameterID == "wavetablePosition" || parameterID == "wavetableMode")
        analyzeWavetablePosition(apvts.getRawParameterValue("wavetablePosition")->load());
}

juce::AudioProcessorValueTreeState::ParameterLayout FullFilterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "root", 1 }, "Root",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 0.01f, 0.3f), // skewed for musical feel
        110.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "level", 1 }, "Level",
        juce::NormalisableRange<float>(-24.0f, 48.0f, 0.01f), 12.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "lowpass", 1 }, "Low Pass",
        juce::NormalisableRange<float>(0.2f, 1.0f, 0.001f), 0.2f)); // 0.2 floor: below this it's prone to exploding

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "q", 1 }, "Q",
        juce::NormalisableRange<float>(10.0f, 50.0f, 0.01f, 0.4f), // 10 floor: same reason
        20.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "amount", 1 }, "Amount",
        1, FullFilterAudioProcessor::maxBells, 64));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "glide", 1 }, "Glide",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "polyphony", 1 }, "Polyphony",
        1, FullFilterAudioProcessor::maxVoices, FullFilterAudioProcessor::maxVoices));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "attack", 1 }, "Attack",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f, 0.4f), 0.03f)); // extra-voice fade-in time

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "decay", 1 }, "Decay",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f, 0.4f), 0.1f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "sustain", 1 }, "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "release", 1 }, "Release",
        juce::NormalisableRange<float>(0.0f, 3.0f, 0.001f, 0.4f), 0.2f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "adsrEnabled", 1 }, "ADSR Enabled", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetablePosition", 1 }, "Position",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "wavetableMode", 1 }, "Mode",
        juce::StringArray{ "Wavetable to Volume", "Wavetable to Filter Position", "Wavetable to Both" },
        0));

    return { params.begin(), params.end() };
}

void FullFilterAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;

    const int numChannels = juce::jmax(getTotalNumInputChannels(), 1);

    bellFilters.clear();
    bellFilters.resize((size_t)maxVoices);
    for (auto& voiceFilters : bellFilters)
    {
        voiceFilters.resize((size_t)numChannels);
        for (auto& channelFilters : voiceFilters)
        {
            channelFilters.resize(maxBells); // default-constructs Filters, no copying involved
            for (auto& f : channelFilters)
                f.reset();
        }
    }

    heldNotes.clear();

    rootFreqSmoother.reset(sampleRate, 0.0);
    rootFreqSmoother.setCurrentAndTargetValue(rootParam->load());
    lastKnownRootParamValue = rootParam->load();

    for (int v = 0; v < maxVoices; ++v)
    {
        auto& voice = voices[(size_t)v];
        voice.currentNote = -1;
        voice.active = false;
        voice.age = 0;
        voice.freqSmoother.reset(sampleRate, 0.0);
        voice.freqSmoother.setCurrentAndTargetValue(110.0f);
        voice.envelope.setSampleRate(sampleRate);
        voice.envelope.reset();
    }
    voiceAgeCounter = 0;

    // ~15ms ramp: fast enough to feel instant, slow enough to avoid
    // audible gain "pumping" as voices are added/removed rapidly.
    voiceCountSmoother.reset(sampleRate, 0.015);
    voiceCountSmoother.setCurrentAndTargetValue(1.0f);

    updateFilters(0);
}

bool FullFilterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

//==============================================================================
void FullFilterAudioProcessor::setBellVolumeMultiplier(int bellIndex, float multiplier)
{
    if (bellIndex < 0 || bellIndex >= maxBells)
        return;
    bellVolumeMult[(size_t)bellIndex].store(juce::jlimit(0.0f, 2.0f, multiplier));
}

float FullFilterAudioProcessor::getBellVolumeMultiplier(int bellIndex) const
{
    if (bellIndex < 0 || bellIndex >= maxBells)
        return 1.0f;
    return bellVolumeMult[(size_t)bellIndex].load();
}

void FullFilterAudioProcessor::setBellFrequencyMultiplier(int bellIndex, float multiplier)
{
    if (bellIndex < 0 || bellIndex >= maxBells)
        return;
    bellFreqMult[(size_t)bellIndex].store(juce::jlimit(0.5f, 2.0f, multiplier));
}

float FullFilterAudioProcessor::getBellFrequencyMultiplier(int bellIndex) const
{
    if (bellIndex < 0 || bellIndex >= maxBells)
        return 1.0f;
    return bellFreqMult[(size_t)bellIndex].load();
}

void FullFilterAudioProcessor::resetAllBellFrequencies()
{
    for (int n = 0; n < maxBells; ++n)
        bellFreqMult[(size_t)n].store(1.0f);
}

bool FullFilterAudioProcessor::isBellManuallyMuted(int bellIndex) const
{
    if (bellIndex < 0 || bellIndex >= maxBells)
        return false;
    return bellManualMute[(size_t)bellIndex].load();
}

void FullFilterAudioProcessor::setEvenHarmonicsMuted(bool shouldMute)
{
    evenHarmonicsMuted.store(shouldMute);

    // "Every 2nd filter" = bell index 1, 3, 5... = harmonic number 2, 4, 6...
    // Muting those leaves only the odd harmonics, which is what turns a full
    // sawtooth-shaped stack into a square-wave-shaped one.
    for (int n = 1; n < maxBells; n += 2)
        bellManualMute[(size_t)n].store(shouldMute);
}

//==============================================================================
namespace
{
    struct Partial { float ratio; float amplitude; };

    // Approximate idealized partial models (frequency ratio relative to the
    // fundamental, and relative amplitude) for a handful of classic
    // acoustic objects, drawn from standard simplified descriptions of
    // struck-bar and struck-plate/bell acoustics. These are meant to give
    // each preset a recognisable character, not to be an exact physical
    // simulation.
    const std::vector<Partial>& partialsForAcousticPreset(FullFilterAudioProcessor::AcousticPreset preset)
    {
        // Free-free bar (beam) transverse-mode ratios, extended further up
        // the series than before so the stack still has character even at
        // a high Amount setting.
        static const std::vector<Partial> beam
        {
            { 1.00f, 1.00f }, { 2.756f, 0.55f }, { 5.404f, 0.30f },
            { 8.933f, 0.15f }, { 13.34f, 0.08f }, { 18.64f, 0.04f },
            { 24.81f, 0.025f }, { 31.81f, 0.016f }, { 39.63f, 0.010f },
            { 48.29f, 0.006f }
        };
        // Struck-bell partials, extended with more of the upper "ring"
        // partials real bells carry (twelfth, upper octaves, etc.) so the
        // preset sounds noticeably more like a bell instead of a simple
        // octave-plus-fifth stack.
        static const std::vector<Partial> bell
        {
            { 0.5f, 0.55f },   // hum tone, an octave below the strike note
            { 1.0f, 1.00f },   // prime / strike note
            { 1.19f, 0.55f },  // minor third
            { 1.5f, 0.60f },   // quint
            { 2.0f, 0.85f },   // nominal (octave)
            { 2.4f, 0.35f },   // upper partial
            { 3.0f, 0.35f },   // twelfth
            { 4.0f, 0.28f },   // upper octave
            { 5.33f, 0.20f },
            { 6.0f, 0.16f },
            { 8.0f, 0.11f },
            { 10.4f, 0.07f },
            { 13.3f, 0.045f },
            { 17.0f, 0.03f }
        };
        // Chladni-pattern plate modes, extended with several more inharmonic
        // upper partials for a busier, more metallic shimmer.
        static const std::vector<Partial> plate
        {
            { 1.00f, 1.00f }, { 2.81f, 0.75f }, { 3.85f, 0.65f },
            { 5.28f, 0.60f }, { 5.98f, 0.55f }, { 8.32f, 0.45f },
            { 9.75f, 0.40f }, { 11.13f, 0.35f }, { 12.44f, 0.30f },
            { 14.62f, 0.24f }, { 16.85f, 0.18f }, { 19.20f, 0.13f },
            { 21.90f, 0.09f }, { 24.70f, 0.06f }
        };
        // Struck-bar-over-resonator (vibraphone) modes: a strong stretched
        // octave-and-change plus its higher, more sparsely spaced overtones.
        static const std::vector<Partial> vibraphone
        {
            { 1.00f, 1.00f }, { 4.00f, 0.45f }, { 9.20f, 0.12f },
            { 16.8f, 0.05f }, { 26.4f, 0.022f }, { 38.1f, 0.010f }
        };
        // Marimba: strong fundamental, a quiet quasi-tenth, and a
        // characteristic bright 4th-partial "tube" resonance around the
        // 10th harmonic, plus a couple of faint higher partials.
        static const std::vector<Partial> marimba
        {
            { 1.00f, 1.00f }, { 3.00f, 0.20f }, { 6.00f, 0.10f },
            { 10.0f, 0.16f }, // tuned tube reinforces this one above its neighbours
            { 15.0f, 0.05f }, { 21.0f, 0.025f }
        };

        switch (preset)
        {
        case FullFilterAudioProcessor::AcousticPreset::Beam:       return beam;
        case FullFilterAudioProcessor::AcousticPreset::Bell:       return bell;
        case FullFilterAudioProcessor::AcousticPreset::Plate:      return plate;
        case FullFilterAudioProcessor::AcousticPreset::Vibraphone: return vibraphone;
        case FullFilterAudioProcessor::AcousticPreset::Marimba:    return marimba;
        }
        return beam;
    }
}

void FullFilterAudioProcessor::applyAcousticPreset(AcousticPreset preset)
{
    const auto& partials = partialsForAcousticPreset(preset);

    for (int n = 0; n < maxBells; ++n)
    {
        const int harmonicNumber = n + 1; // this bell's natural (un-multiplied) position

        if ((size_t)n < partials.size())
        {
            const auto& partial = partials[(size_t)n];

            // bellFreqMult is relative to the harmonic's natural position
            // (harmonicNumber * root), so divide the desired absolute ratio
            // by that to get the multiplier to store. The 0.5x-2x clamp on
            // per-bell frequency edits means very stretched partials (e.g.
            // a vibraphone's 4th-partial-at-2-octaves) get pulled in to the
            // nearest edge rather than reproduced exactly — still enough to
            // read as characterful, just not perfectly physical.
            const float desiredMultiplier = partial.ratio / (float)harmonicNumber;
            setBellFrequencyMultiplier(n, desiredMultiplier);

            // Amplitude is 0..1 in the table; scale up slightly so the
            // dominant partial(s) sit a bit above unity and stand out.
            setBellVolumeMultiplier(n, partial.amplitude * 1.3f);
        }
        else
        {
            // Beyond this object's characteristic partials: leave frequency
            // alone and fade the bell almost silent rather than hard-muting
            // it, so switching presets or dragging a bar manually afterwards
            // doesn't have to fight a separate mute flag.
            setBellFrequencyMultiplier(n, 1.0f);
            setBellVolumeMultiplier(n, 0.02f);
        }
    }
}

//==============================================================================
void FullFilterAudioProcessor::computeVoiceBells(int voiceIndex, float rootFreq)
{
    auto& voice = voices[(size_t)voiceIndex];

    const float levelDb = levelParam->load();
    const float lowpassAmt = lowpassParam->load();
    const float q = qParam->load();

    const int count = juce::jlimit(1, maxBells, (int)std::lround(amountParam->load()));

    const double nyquist = currentSampleRate * 0.5;
    const double safeCeiling = nyquist * 0.95; // stay clear of the edge

    for (int n = 0; n < count; ++n)
    {
        const int harmonicNumber = n + 1;
        const double naturalFreq = (double)rootFreq * harmonicNumber;
        const double freq = naturalFreq * (double)bellFreqMult[(size_t)n].load(); // apply per-bell frequency edit

        const bool outsideAudibleRange = freq < 10.0 || freq > 20000.0;
        const bool muted = bellManualMute[(size_t)n].load() || outsideAudibleRange;

        if (freq >= safeCeiling || muted)
        {
            // Above what this sample rate can represent, or explicitly
            // muted/out of the 10 Hz-20 kHz range — leave it neutral.
            voice.coefficients[(size_t)n] = new Coeffs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

            if (voiceIndex == 0)
            {
                bellFrequencies[(size_t)n].store((float)freq);
                bellGainsDb[(size_t)n].store(-100.0f);
            }
            continue;
        }

        // Harmonics crowd together on a log scale as n grows, so a fixed Q
        // and gain start to overlap and reinforce each other — with a high
        // Amount and a pushed-up Q knob that reinforcement piles up fast
        // and gets disproportionately, unpleasantly loud near the top.
        // To keep that in check by default, upper harmonics are given a
        // bit less gain and a bit more Q (narrower, less overlap) than the
        // knobs alone would produce — purely a function of position in the
        // stack, layered underneath whatever the user dials in.
        const float harmonicFraction = (count > 1) ? (float)n / (float)(count - 1) : 0.0f;

        const float intrinsicTrimDb = -6.0f * harmonicFraction;   // up to -6 dB by the top bell
        const float intrinsicQScale = 1.0f + 1.5f * harmonicFraction; // up to 2.5x Q by the top bell

        // Tilt: as lowpassAmt goes 0 -> 1, higher harmonics get pushed down
        // harder, up to -36 dB of extra attenuation at the top of the stack.
        const float tiltDb = -lowpassAmt * 36.0f * harmonicFraction;

        // Per-bell volume edit (from the volume bar editor), 0x-2x -> dB.
        const float bellVolDb = 20.0f * std::log10(juce::jmax(0.0001f, bellVolumeMult[(size_t)n].load()));

        const float totalDb = levelDb + tiltDb + intrinsicTrimDb + bellVolDb;
        const float gainFactor = juce::Decibels::decibelsToGain(totalDb);
        const float effectiveQ = q * intrinsicQScale;

        voice.coefficients[(size_t)n] = Coeffs::makePeakFilter(currentSampleRate, (float)freq, effectiveQ, gainFactor);

        if (voiceIndex == 0)
        {
            bellFrequencies[(size_t)n].store((float)freq);
            bellGainsDb[(size_t)n].store(totalDb);
        }
    }

    for (auto& channelFilters : bellFilters[(size_t)voiceIndex])
        for (int n = 0; n < count; ++n)
            channelFilters[(size_t)n].coefficients = voice.coefficients[(size_t)n];

    if (voiceIndex == 0)
        activeBellCount.store(count);
}

void FullFilterAudioProcessor::updateFilters(int numSamples)
{
    // If the Root parameter changed by any means other than our own writes
    // in setRootFrequency() — a manual knob turn, host automation, preset
    // load — snap the smoother straight there; glide only ever applies to
    // MIDI note changes. setRootFrequency() keeps lastKnownRootParamValue
    // exactly in sync with what it wrote (re-reading the parameter after
    // writing it, since the normalised round trip can quantise slightly),
    // so any mismatch seen here is a genuine external change, not our own.
    const float paramValue = rootParam->load();
    if (std::abs(paramValue - lastKnownRootParamValue) > 0.0001f)
    {
        lastKnownRootParamValue = paramValue;
        rootFreqSmoother.setCurrentAndTargetValue(paramValue);
    }

    // rootFreqSmoother's ramp length was set in real seconds via reset()
    // (see setRootFrequency()), which assumes one getNextValue()-equivalent
    // step per sample. This function is only called once per block, so
    // advance it by the block's full sample count in one go — not by a
    // single step — or the ramp would crawl along at 1/blockSize of the
    // Glide knob's intended speed.
    const float baseRoot = numSamples > 0 ? rootFreqSmoother.skip(numSamples)
        : rootFreqSmoother.getCurrentValue();
    computeVoiceBells(0, baseRoot);

    const int poly = juce::jlimit(1, maxVoices, (int)std::lround(polyphonyParam->load()));

    for (int v = 1; v < maxVoices; ++v)
    {
        auto& voice = voices[(size_t)v];

        if (v > poly - 1)
        {
            // Polyphony knob was turned down below this voice's slot:
            // force it silent and free immediately.
            if (voice.active)
            {
                voice.currentNote = -1;
                voice.active = false;
                voice.envelope.reset();
            }
            continue;
        }

        if (!voice.active)
            continue;

        const float root = voice.freqSmoother.getNextValue(); // snapped on trigger, no glide
        computeVoiceBells(v, root);
    }
}

void FullFilterAudioProcessor::setRootFrequency(float freqHz, bool glide)
{
    if (rootRangedParam == nullptr)
        return;

    // Clamp into the Root parameter's own range before writing it, and
    // write the parameter itself so the knob visibly follows the note and
    // the value is what gets saved/restored and shown to the host.
    const auto range = rootRangedParam->getNormalisableRange();
    const float clamped = juce::jlimit(range.start, range.end, freqHz);

    rootRangedParam->setValueNotifyingHost(rootRangedParam->convertTo0to1(clamped));

    // Immediately re-read the parameter rather than trusting 'clamped': the
    // normalised 0-1 round trip through the (skewed) Root range can quantise
    // the value very slightly. Tracking the *actual* post-write value here
    // means the next updateFilters() call sees no discrepancy at all and
    // never mistakes our own write for an external knob turn — which is
    // what was silently re-snapping the smoother and killing glide after a
    // single sample.
    const float actualValue = rootParam->load();
    lastKnownRootParamValue = actualValue;

    // Glide only ever applies in pure monophonic mode (Polyphony == 1).
    // At any higher Polyphony setting, overlapping notes are handled by the
    // extra voices instead, so the base voice always snaps instantly.
    const int poly = juce::jlimit(1, maxVoices, (int)std::lround(polyphonyParam->load()));
    const bool shouldGlide = glide && poly == 1;

    if (shouldGlide)
    {
        // Linearly ramp from wherever the smoother currently sits to the new
        // note's frequency over the Glide knob's time (0 = instant). Because
        // every bell's frequency is (rootFreq * harmonicNumber * per-bell
        // multiplier), ramping this single root value linearly carries every
        // filter in the stack from the old note to the new note together.
        const float rampSeconds = juce::jmax(0.0f, glideParam->load());
        const float startPoint = rootFreqSmoother.getCurrentValue();
        rootFreqSmoother.reset(currentSampleRate, rampSeconds);
        rootFreqSmoother.setCurrentAndTargetValue(startPoint);
        rootFreqSmoother.setTargetValue(actualValue);
    }
    else
    {
        rootFreqSmoother.setCurrentAndTargetValue(actualValue);
    }
}

void FullFilterAudioProcessor::triggerExtraVoice(int note, float freqHz)
{
    const int poly = juce::jlimit(1, maxVoices, (int)std::lround(polyphonyParam->load()));
    const int numExtraSlots = poly - 1;
    if (numExtraSlots <= 0)
        return;

    // Prefer a free slot; otherwise steal the oldest-triggered slot in use
    // (simple round-robin voice stealing).
    int chosen = -1;
    juce::uint32 oldestAge = std::numeric_limits<juce::uint32>::max();

    for (int v = 1; v <= numExtraSlots; ++v)
    {
        auto& voice = voices[(size_t)v];

        if (!voice.active)
        {
            chosen = v;
            break;
        }

        if (voice.age < oldestAge)
        {
            oldestAge = voice.age;
            chosen = v;
        }
    }

    if (chosen < 0)
        return;

    auto& voice = voices[(size_t)chosen];
    voice.currentNote = note;
    voice.active = true;
    voice.age = ++voiceAgeCounter;

    // Extra voices snap straight to pitch — glide is a base-voice-only "the
    // note you're currently holding" effect.
    voice.freqSmoother.reset(currentSampleRate, 0.0);
    voice.freqSmoother.setCurrentAndTargetValue(freqHz);

    // Attack always follows its knob. Decay/Sustain/Release only follow
    // theirs when the ADSR toggle is on; when it's off the envelope is
    // pinned to the original behaviour (instant full sustain, fixed short
    // release) so Attack alone still works as a simple fade-in.
    const bool useFullAdsr = adsrEnabledParam != nullptr && adsrEnabledParam->load() > 0.5f;

    juce::ADSR::Parameters adsrParams;
    adsrParams.attack = juce::jmax(0.0f, attackParam->load());
    adsrParams.decay = useFullAdsr ? juce::jmax(0.0f, decayParam->load()) : 0.0f;
    adsrParams.sustain = useFullAdsr ? juce::jlimit(0.0f, 1.0f, sustainParam->load()) : 1.0f;
    adsrParams.release = useFullAdsr ? juce::jmax(0.0f, releaseParam->load()) : extraVoiceReleaseSeconds;

    voice.envelope.setParameters(adsrParams);
    voice.envelope.noteOn();
}

void FullFilterAudioProcessor::releaseExtraVoice(int note)
{
    for (int v = 1; v < maxVoices; ++v)
    {
        auto& voice = voices[(size_t)v];

        if (voice.active && voice.currentNote == note)
        {
            voice.currentNote = -1;
            voice.envelope.noteOff();
            // Stays 'active' (still audible while it fades out) until
            // processBlock() notices the envelope has reached silence.
        }
    }
}

void FullFilterAudioProcessor::handleMidiMessage(const juce::MidiMessage& message)
{
    const bool useFullAdsr = adsrEnabledParam != nullptr && adsrEnabledParam->load() > 0.5f;

    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        const bool wasEmpty = heldNotes.empty();

        heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back(note);

        const float freq = (float)juce::MidiMessage::getMidiNoteInHertz(note);

        setRootFrequency(freq, /*glide*/ !wasEmpty);

        // --- Trigger Base Voice Envelope ---
        if (useFullAdsr)
        {
            juce::ADSR::Parameters adsrParams;
            adsrParams.attack = juce::jmax(0.0f, attackParam->load());
            adsrParams.decay = juce::jmax(0.0f, decayParam->load());
            adsrParams.sustain = juce::jlimit(0.0f, 1.0f, sustainParam->load());
            adsrParams.release = juce::jmax(0.0f, releaseParam->load());

            voices[0].envelope.setParameters(adsrParams);

            // Trigger noteOn if transitioning from silence or re-triggering
            if (wasEmpty || !voices[0].envelope.isActive())
                voices[0].envelope.noteOn();
        }

        if (!wasEmpty)
            triggerExtraVoice(note, freq);
    }
    else if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();
        heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());

        if (!heldNotes.empty())
        {
            const float freq = (float)juce::MidiMessage::getMidiNoteInHertz(heldNotes.back());
            setRootFrequency(freq, /*glide*/ true);
        }
        else
        {
            // --- Release Base Voice Envelope ---
            if (useFullAdsr)
                voices[0].envelope.noteOff();
        }

        releaseExtraVoice(note);
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        heldNotes.clear();
        voices[0].envelope.reset();

        for (int v = 1; v < maxVoices; ++v)
        {
            auto& voice = voices[(size_t)v];
            voice.currentNote = -1;
            voice.active = false;
            voice.envelope.reset();
        }
    }
}

void FullFilterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto metadata : midiMessages)
        handleMidiMessage(metadata.getMessage());

    const int numSamples = buffer.getNumSamples();
    updateFilters(numSamples);

    const int numChannels = juce::jmin(buffer.getNumChannels(), (int)bellFilters[0].size());
    const int count = activeBellCount.load();
    juce::AudioBuffer<float>& buf = buffer;
    float* channelPtrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numChannels && ch < 2; ++ch)
        channelPtrs[ch] = buf.getWritePointer(ch);

    const bool useFullAdsr = adsrEnabledParam != nullptr && adsrEnabledParam->load() > 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        float voiceEnv[maxVoices];

        // Base voice logic: If ADSR mode is disabled, keep full level (1.0f)
        if (useFullAdsr)
            voiceEnv[0] = voices[0].envelope.getNextSample();
        else
            voiceEnv[0] = 1.0f;

        for (int v = 1; v < maxVoices; ++v)
            voiceEnv[v] = voices[(size_t)v].active ? voices[(size_t)v].envelope.getNextSample() : 0.0f;

        // --- Multi-voice gain compensation ---
        // Sum the per-voice envelope levels this sample (not just a binary
        // active count) so compensation tracks actual contributing energy,
        // including fade-in/out overlap during voice-stealing/ADSR tails.
        // A smoothed value is used so the gain doesn't jump/click every
        // time a voice starts or stops - it eases toward the new target
        // over voiceCountSmoother's ramp time instead.
        float envSum = 0.0f;
        for (int v = 0; v < maxVoices; ++v)
            envSum += voiceEnv[v];
        voiceCountSmoother.setTargetValue(juce::jmax(1.0f, envSum));
        const float smoothedCount = voiceCountSmoother.getNextValue();

        // sqrt(N) compensation: voices are uncorrelated (different pitches/
        // phases), so their peaks don't all line up the way N identical
        // in-phase signals would - a full 1/N would usually over-attenuate
        // and make single-voice passages sound weak by comparison. 1/sqrt(N)
        // is the standard compromise used for this kind of unison/polyphony
        // gain staging.
        const float compensation = 1.0f / std::sqrt(smoothedCount);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = channelPtrs[ch][i];
            float mixed = 0.0f;

            // Base voice output scaled by voiceEnv[0]
            if (voiceEnv[0] > 0.0f)
            {
                float s = dry;
                auto& chFilters = bellFilters[0][(size_t)ch];
                for (int n = 0; n < count; ++n)
                    s = chFilters[(size_t)n].processSample(s);
                mixed += s * voiceEnv[0];
            }

            // Extra voices
            for (int v = 1; v < maxVoices; ++v)
            {
                if (voiceEnv[v] <= 0.0f)
                    continue;

                float s = dry;
                auto& chFilters = bellFilters[(size_t)v][(size_t)ch];
                for (int n = 0; n < count; ++n)
                    s = chFilters[(size_t)n].processSample(s);
                mixed += s * voiceEnv[v];
            }

            channelPtrs[ch][i] = mixed * compensation;
        }
    }

    for (int v = 1; v < maxVoices; ++v)
    {
        auto& voice = voices[(size_t)v];
        if (voice.active && voice.currentNote < 0 && !voice.envelope.isActive())
            voice.active = false;
    }
}

//==============================================================================
bool FullFilterAudioProcessor::loadWavetableFile(const juce::File& file, bool analyzeAfterLoad)
{
    std::unique_ptr<juce::AudioFormatReader> reader(wavetableFormatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int numSamples = (int)juce::jmin<juce::int64>(reader->lengthInSamples, 1 << 24); // sanity cap

    juce::AudioBuffer<float> temp(1, numSamples); // channel 0 only, per-frame FFT only needs mono
    reader->read(&temp, 0, numSamples, 0, true, false);

    // Any frame count is fine - could be 1, 128, 255, 377, whatever the file
    // actually contains.
    const int frameCount = numSamples / wavetableFrameSize;
    if (frameCount <= 0)
        return false; // shorter than a single frame - nothing meaningful to analyze

    wavetableSamples.assign(temp.getReadPointer(0), temp.getReadPointer(0) + numSamples);
    wavetableFrameCount = frameCount;
    wavetableFileName = file.getFileName();
    wavetableFilePath = file.getFullPathName();

    // Analyze every frame up front so scanning the Position knob afterwards
    // (including live host automation) is just a cheap lerp over this cache
    // rather than an FFT per change.
    wavetableFrameHarmonics.assign((size_t)frameCount, {});
    for (int f = 0; f < frameCount; ++f)
        computeFrameHarmonics(f, wavetableFrameHarmonics[(size_t)f]);

    if (analyzeAfterLoad)
    {
        const float position = apvts.getRawParameterValue("wavetablePosition")->load();
        analyzeWavetablePosition(position);
    }
    return true;
}

void FullFilterAudioProcessor::computeFrameHarmonics(int frameIndex, std::array<float, maxBells>& out)
{
    // Copy this frame's samples into the FFT scratch buffer (the trailing
    // half is working space juce::dsp::FFT needs for a real-only transform).
    std::fill(wavetableFFTScratch.begin(), wavetableFFTScratch.end(), 0.0f);
    const float* frameStart = wavetableSamples.data() + (size_t)frameIndex * (size_t)wavetableFrameSize;
    std::copy(frameStart, frameStart + wavetableFrameSize, wavetableFFTScratch.begin());

    // No window: a wavetable frame is by construction exactly one periodic
    // cycle, so an unwindowed transform reads harmonic bin k directly as
    // harmonic k of the frame's fundamental - windowing would only smear it.
    wavetableFFT.performFrequencyOnlyForwardTransform(wavetableFFTScratch.data());

    // Bin 0 is DC (offset, not a harmonic); bin k (k>=1) is harmonic k.
    // Normalise so the strongest of the first maxBells harmonics reads as
    // 1.0 and the rest scale relative to it - what that 0..1 strength gets
    // mapped to (bell volume range, bell frequency range, or both) is
    // decided later by analyzeWavetablePosition(), not baked in here.
    float peak = 0.0f;
    for (int n = 0; n < maxBells; ++n)
        peak = juce::jmax(peak, wavetableFFTScratch[(size_t)n + 1]);

    for (int n = 0; n < maxBells; ++n)
    {
        const float magnitude = wavetableFFTScratch[(size_t)n + 1];
        out[(size_t)n] = peak > 1.0e-9f ? (magnitude / peak) : 0.0f; // 0..1
    }
}

void FullFilterAudioProcessor::analyzeWavetablePosition(float position01)
{
    if (wavetableFrameCount <= 0)
        return;

    position01 = juce::jlimit(0.0f, 1.0f, position01);

    std::array<float, maxBells> strength{};

    if (wavetableFrameCount == 1)
    {
        strength = wavetableFrameHarmonics[0];
    }
    else
    {
        // Map 0..1 across the available frames and linearly interpolate
        // between the two neighbouring frames' cached harmonic strengths.
        const float framePos = position01 * (float)(wavetableFrameCount - 1);
        const int frameLo = juce::jlimit(0, wavetableFrameCount - 1, (int)std::floor(framePos));
        const int frameHi = juce::jlimit(0, wavetableFrameCount - 1, frameLo + 1);
        const float frac = framePos - (float)frameLo;

        const auto& lo = wavetableFrameHarmonics[(size_t)frameLo];
        const auto& hi = wavetableFrameHarmonics[(size_t)frameHi];

        for (int n = 0; n < maxBells; ++n)
            strength[(size_t)n] = lo[(size_t)n] + (hi[(size_t)n] - lo[(size_t)n]) * frac;
    }

    // 0 = Volume, 1 = Filter Position, 2 = Both (matches the "wavetableMode"
    // AudioParameterChoice order). Whichever target isn't selected gets
    // reset to neutral rather than left stale from a previous mode.
    const int mode = wavetableModeParam != nullptr ? (int)wavetableModeParam->load() : 0;
    const bool drivesVolume = mode == 0 || mode == 2;
    const bool drivesFrequency = mode == 1 || mode == 2;

    // Safety gate, applied in every mode: a harmonic that's essentially
    // absent from the wavetable frame (silence, or just FFT noise floor -
    // e.g. every non-fundamental bin of a pure sine) must not turn into a
    // full-volume resonance once Filter Position mode retunes it elsewhere.
    // Volume mode already makes near-zero-strength harmonics near-silent as
    // a side effect of strength*2; Filter Position mode doesn't touch volume
    // at all, so without this gate a sine wavetable would leave all 128
    // bells sitting at full, unedited 1x gain, just redistributed across
    // new (denser) frequencies - a wall of resonance from content that was
    // never actually in the source frame, plus occasional exact-frequency
    // overlaps between bells that happen to retune to the same spot. Muting
    // anything below the threshold sidesteps both: only harmonics that
    // genuinely have energy in the frame get retuned and heard.
    constexpr float silentStrengthThreshold = 0.01f;

    for (int n = 0; n < maxBells; ++n)
    {
        const bool silent = strength[(size_t)n] < silentStrengthThreshold;

        if (silent)
        {
            setBellVolumeMultiplier(n, 0.0f);
            setBellFrequencyMultiplier(n, 1.0f);
            continue;
        }

        setBellVolumeMultiplier(n, drivesVolume ? strength[(size_t)n] * 2.0f : 1.0f); // 0..2x

        if (drivesFrequency)
        {
            // 0.5x..2x, log-centred - louder harmonics in the wavetable
            // frame get detuned further from where they'd naturally sit,
            // so the wavetable's *shape* warps the harmonic series' tuning
            // rather than its loudness. This is the "unheard timbres" one.
            setBellFrequencyMultiplier(n, std::pow(2.0f, strength[(size_t)n] * 2.0f - 1.0f));
        }
        else
        {
            setBellFrequencyMultiplier(n, 1.0f);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* FullFilterAudioProcessor::createEditor()
{
    return new FullFilterAudioProcessorEditor(*this);
}

void FullFilterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    // Persist the per-bell edits alongside the regular parameters, as
    // compact comma-separated attributes on the root XML element.
    juce::String volStr, freqStr;
    juce::String muteStr;
    for (int n = 0; n < maxBells; ++n)
    {
        volStr << bellVolumeMult[(size_t)n].load();
        freqStr << bellFreqMult[(size_t)n].load();
        muteStr << (bellManualMute[(size_t)n].load() ? '1' : '0');
        if (n + 1 < maxBells)
        {
            volStr << ",";
            freqStr << ",";
        }
    }
    xml->setAttribute("bellVolumeMultipliers", volStr);
    xml->setAttribute("bellFrequencyMultipliers", freqStr);
    xml->setAttribute("bellManualMutes", muteStr);
    xml->setAttribute("wavetableFilePath", wavetableFilePath);

    copyXmlToBinary(*xml, destData);
}

void FullFilterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    if (xml->hasAttribute("bellVolumeMultipliers"))
    {
        auto tokens = juce::StringArray::fromTokens(xml->getStringAttribute("bellVolumeMultipliers"), ",", "");
        for (int n = 0; n < juce::jmin(maxBells, tokens.size()); ++n)
            bellVolumeMult[(size_t)n].store(tokens[n].getFloatValue());
    }

    if (xml->hasAttribute("bellFrequencyMultipliers"))
    {
        auto tokens = juce::StringArray::fromTokens(xml->getStringAttribute("bellFrequencyMultipliers"), ",", "");
        for (int n = 0; n < juce::jmin(maxBells, tokens.size()); ++n)
            bellFreqMult[(size_t)n].store(tokens[n].getFloatValue());
    }

    if (xml->hasAttribute("bellManualMutes"))
    {
        auto str = xml->getStringAttribute("bellManualMutes");
        for (int n = 0; n < juce::jmin(maxBells, str.length()); ++n)
            bellManualMute[(size_t)n].store(str[n] == '1');
    }

    if (xml->hasAttribute("wavetableFilePath"))
    {
        // Best-effort: re-import the same file so the Frame knob has
        // something to scrub through again. If the file has moved/been
        // deleted, this quietly no-ops - the bell volume multipliers above
        // (the actual analysis result) are already restored either way.
        const juce::File file(xml->getStringAttribute("wavetableFilePath"));
        if (file.existsAsFile())
            loadWavetableFile(file, /*analyzeAfterLoad*/ false);
    }
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FullFilterAudioProcessor();
}