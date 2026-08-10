#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <utility>

const char* const SpectralLockAudioProcessor::noteNames[12] =
{ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const char* const SpectralLockAudioProcessor::scaleNames[SpectralLockAudioProcessor::numScales] =
{
    "Chromatic", "Major", "Minor", "Harm Minor", "Dorian", "Phrygian",
    "Lydian", "Mixolydian", "Penta Major", "Penta Minor", "Whole Tone", "Octaves"
};

// Each row maps input pitch class (index) -> output pitch class (value).
// Non-scale tones are pulled to the nearest scale degree.
const int SpectralLockAudioProcessor::scaleTables[SpectralLockAudioProcessor::numScales][12] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11 },   // Chromatic
    { 0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9,11 },   // Major
    { 0, 0, 2, 3, 3, 5, 5, 7, 8, 8,10,10 },   // Natural minor
    { 0, 0, 2, 3, 3, 5, 5, 7, 8, 8,11,11 },   // Harmonic minor
    { 0, 0, 2, 3, 3, 5, 5, 7, 7, 9, 9,10 },   // Dorian
    { 0, 1, 1, 3, 3, 5, 5, 7, 8, 8,10,10 },   // Phrygian
    { 0, 0, 2, 2, 4, 4, 6, 7, 7, 9, 9,11 },   // Lydian
    { 0, 0, 2, 2, 4, 5, 5, 7, 7, 9,10,10 },   // Mixolydian
    { 0, 0, 2, 2, 4, 4, 4, 7, 7, 9, 9, 9 },   // Pentatonic major
    { 0, 0, 3, 3, 3, 5, 5, 7, 7,10,10,10 },   // Pentatonic minor
    { 0, 0, 2, 2, 4, 4, 6, 6, 8, 8,10,10 },   // Whole tone
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }    // Everything to the root
};

static inline float coefFromTau (float tauSeconds, double sampleRate) noexcept
{
    return 1.0f - std::exp (-1.0f / juce::jmax (1.0f, (float) (tauSeconds * sampleRate)));
}

// deterministic per-band scatter, so panning/detune are stable across sessions
static inline float hashToBipolar (int k) noexcept
{
    juce::uint32 x = (juce::uint32) (k * 2654435761u);
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return ((float) (x & 0xffffff) / 8388608.0f) - 1.0f;   // -1 .. 1
}

SpectralLockAudioProcessor::SpectralLockAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpectralLock", createParameterLayout())
{
    for (int i = 0; i <= tableSize; ++i)
        sineTable[(size_t) i] = std::sin (juce::MathConstants<float>::twoPi
                                          * (float) i / (float) tableSize);

    pMix       = apvts.getRawParameterValue ("mix");
    pAmount    = apvts.getRawParameterValue ("amount");
    pIsolate   = apvts.getRawParameterValue ("isolate");
    pGlide     = apvts.getRawParameterValue ("glide");
    pBend      = apvts.getRawParameterValue ("bend");
    pBendRange = apvts.getRawParameterValue ("bendrange");
    pWidth     = apvts.getRawParameterValue ("width");
    pRangeLo   = apvts.getRawParameterValue ("rangelo");
    pRangeHi   = apvts.getRawParameterValue ("rangehi");
    pRoot      = apvts.getRawParameterValue ("root");
    pPitch     = apvts.getRawParameterValue ("pitch");
    pMuteLo    = apvts.getRawParameterValue ("mutelow");
    pMuteHi    = apvts.getRawParameterValue ("mutehigh");
    pMidiOn    = apvts.getRawParameterValue ("midi");
    pFreeze    = apvts.getRawParameterValue ("freeze");
    pShimmer   = apvts.getRawParameterValue ("shimmer");
    pSpray     = apvts.getRawParameterValue ("spray");
    pTilt      = apvts.getRawParameterValue ("tilt");
    pLevel     = apvts.getRawParameterValue ("level");

    for (int i = 0; i < 12; ++i)
        pMatrix[(size_t) i] = apvts.getRawParameterValue ("matrix" + juce::String (i));

    for (auto& d : display) d.store (0.0f);
    heldSorted.reserve (32);
}

juce::AudioProcessorValueTreeState::ParameterLayout
SpectralLockAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray notes;
    for (int i = 0; i < 12; ++i) notes.add (noteNames[i]);

    auto pct = [] (float v, int) { return String (v * 100.0f, 0) + " %"; };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 },
        "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 },
        "Amount", NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "isolate", 1 },
        "Isolate", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 },
        "Glide", NormalisableRange<float> (0.0f, 1.0f, 0.0f, 0.4f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bend", 1 },
        "Bend", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "bendrange", 1 },
        "Bend Range", 1, 24, 2));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 },
        "Width", NormalisableRange<float> (0.0f, 2.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "rangelo", 1 },
        "Range Low", NormalisableRange<float> (bankLowHz, bankHighHz, 0.0f, 0.3f), 65.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "rangehi", 1 },
        "Range High", NormalisableRange<float> (bankLowHz, bankHighHz, 0.0f, 0.3f), 2200.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "root", 1 },
        "Root", notes, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pitch", 1 },
        "Pitch", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mutelow",  1 }, "Mute Low",  false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mutehigh", 1 }, "Mute High", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "midi",     1 }, "MIDI In",   false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze",   1 }, "Freeze",    false));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "shimmer", 1 },
        "Shimmer", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "spray", 1 },
        "Spray", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tilt", 1 },
        "Tilt", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 },
        "Level", NormalisableRange<float> (0.0f, 2.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    // one AudioParameterChoice per input pitch class
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { "matrix" + juce::String (i), 1 },
            juce::String ("Matrix ") + noteNames[i], notes, i));

    return layout;
}

void SpectralLockAudioProcessor::buildFilterbank()
{
    numBands = 0;
    const float ratio = std::pow (2.0f, 1.0f / (float) bandsPerOctave);
    float f = bankLowHz;

    while (f < bankHighHz && numBands < maxBands)
    {
        auto& b = bands[(size_t) numBands];
        b = Band();
        b.fc     = f;
        b.noteIn = ftom (f);
        b.detune = hashToBipolar (numBands * 7 + 3);
        b.freq   = f;
        b.targetFreq = f;
        b.phase  = 0.5f * (hashToBipolar (numBands) + 1.0f);   // scatter start phases

        // RBJ constant-skirt bandpass, unity peak gain
        const float w0    = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float cw    = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * bandQ);
        const float a0    = 1.0f + alpha;

        b.b0 =  alpha / a0;
        b.b1 =  0.0f;
        b.b2 = -alpha / a0;
        b.a1 = (-2.0f * cw) / a0;
        b.a2 = (1.0f - alpha) / a0;

        ++numBands;
        f *= ratio;
    }

    firstBand = 0;
    lastBand  = numBands - 1;
}

void SpectralLockAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    buildFilterbank();

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    loSplit.prepare (spec);
    hiSplit.prepare (spec);
    loSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    hiSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    loSplit.reset();
    hiSplit.reset();

    gateAttack   = coefFromTau (0.001f, sr);
    displayDecay = 0.85f;
    globalPeak   = 0.0f;
    transFast = transSlow = 0.0f;
    audioGate = 1.0f;

    smMix.reset     (sr, 0.02); smMix.setCurrentAndTargetValue (pMix->load());
    smAmount.reset  (sr, 0.02); smAmount.setCurrentAndTargetValue (pAmount->load());
    smWidth.reset   (sr, 0.02); smWidth.setCurrentAndTargetValue (pWidth->load());
    smLevel.reset   (sr, 0.02); smLevel.setCurrentAndTargetValue (pLevel->load());
    smShimmer.reset (sr, 0.02); smShimmer.setCurrentAndTargetValue (pShimmer->load());

    // latency is essentially zero - the filterbank runs in real time; the
    // only "delay" is the envelope follower's own inertia.
    setLatencySamples (0);
}

bool SpectralLockAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpectralLockAudioProcessor::updateRangeWindow (float loHz, float hiHz)
{
    if (hiHz < loHz) std::swap (loHz, hiHz);

    int lo = 0, hi = numBands - 1;
    while (lo < numBands - 1 && bands[(size_t) lo].fc < loHz) ++lo;
    while (hi > 0          && bands[(size_t) hi].fc > hiHz) --hi;
    if (hi < lo) hi = lo;

    // silence anything that just fell outside the window so it can't leak
    // stale energy back in when the range is widened again
    if (lo != firstBand || hi != lastBand)
    {
        for (int k = 0; k < numBands; ++k)
        {
            if (k >= lo && k <= hi) continue;
            auto& b = bands[(size_t) k];
            b.s1a = b.s2a = b.s1b = b.s2b = 0.0f;
            b.envSq = b.amp = 0.0f;
            b.gate = 0.0f;
        }
    }

    firstBand = lo;
    lastBand  = hi;
}

void SpectralLockAudioProcessor::updateBandMapping()
{
    const int   root      = (int) pRoot->load();
    const float pitch     = pPitch->load();
    const float bend      = pBend->load() * pBendRange->load();
    const float spray     = pSpray->load();
    const float tilt      = pTilt->load();
    const float width     = pWidth->load();
    const bool  midiOn    = pMidiOn->load() > 0.5f;
    const bool  useMidi   = midiOn && ! heldSorted.empty();

    midiIsActive.store (useMidi);

    int matrix[12];
    for (int i = 0; i < 12; ++i)
        matrix[i] = juce::jlimit (0, 11, (int) pMatrix[(size_t) i]->load());

    // per-band envelope time constants: roughly four cycles, floored at 3 ms
    for (int k = firstBand; k <= lastBand; ++k)
    {
        auto& b = bands[(size_t) k];

        // envelope follower time constant
        b.envCoef = coefFromTau (juce::jmax (0.004f, 5.0f / b.fc), sr);

        // which note is this band listening to?
        const int n = juce::roundToInt (b.noteIn);
        float outNote;

        if (useMidi)
        {
            // magnetise to the nearest held note; anything further than half an
            // octave away from any note simply drops out
            int   best = heldSorted.front();
            int   bestDist = std::abs (n - best);
            for (int held : heldSorted)
            {
                const int d = std::abs (n - held);
                if (d < bestDist) { bestDist = d; best = held; }
            }
            b.muted = (bestDist > 6);
            outNote = (float) best;
        }
        else
        {
            b.muted = false;
            const int rel = n - root;
            const int oct = (int) std::floor ((float) rel / 12.0f);
            const int pc  = rel - oct * 12;
            outNote = (float) (root + oct * 12 + matrix[pc]);
        }

        // transpose, bend, spray
        outNote += pitch + bend + spray * b.detune * 0.5f;
        b.targetFreq = juce::jlimit (10.0f, (float) sr * 0.45f, mtof (outNote));

        // spectral tilt around 700 Hz
        const float t = std::pow (b.fc / 700.0f, tilt * 0.9f);
        b.ampGain = juce::jlimit (0.05f, 8.0f, t);

        // stereo placement
        const float pan = juce::jlimit (-1.0f, 1.0f, hashToBipolar (k * 13 + 1) * width);
        const float a   = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        b.panL = std::cos (a);
        b.panR = std::sin (a);
    }
}

void SpectralLockAudioProcessor::collectMidi (juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())            noteHeld[(size_t) m.getNoteNumber()] = true;
        else if (m.isNoteOff())      noteHeld[(size_t) m.getNoteNumber()] = false;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            noteHeld.fill (false);
    }

    heldSorted.clear();
    for (int i = 0; i < 128; ++i)
        if (noteHeld[(size_t) i])
            heldSorted.push_back (i);
}

void SpectralLockAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const bool stereo    = numCh > 1;

    collectMidi (midi);
    midi.clear();

    const float loHz    = pRangeLo->load();
    const float hiHz    = juce::jmax (pRangeHi->load(), loHz + 20.0f);
    updateRangeWindow (loHz, hiHz);
    updateBandMapping();

    loSplit.setCutoffFrequency (juce::jlimit (20.0f, (float) sr * 0.45f, loHz));
    hiSplit.setCutoffFrequency (juce::jlimit (20.0f, (float) sr * 0.45f, hiHz));

    const float isolate  = pIsolate->load();
    const bool  freeze   = pFreeze->load() > 0.5f;
    const bool  muteLo   = pMuteLo->load() > 0.5f;
    const bool  muteHi   = pMuteHi->load() > 0.5f;

    smMix    .setTargetValue (pMix->load());
    smAmount .setTargetValue (pAmount->load());
    smLevel  .setTargetValue (pLevel->load());
    smShimmer.setTargetValue (pShimmer->load());

    // GLIDE: 0.4 ms .. 500 ms
    const float glideTau  = 0.0004f + pGlide->load() * pGlide->load() * 0.5f;
    const float glideCoef = coefFromTau (glideTau, sr);

    // ISOLATE: threshold rises with the dial, release gets brutally short
    gateRelease = coefFromTau (juce::jmap (isolate, 0.09f, 0.0015f), sr);
    const float gateThresh = isolate * 0.30f * juce::jmax (globalPeak, 1.0e-4f)
                             + isolate * 2.0e-4f;

    const float transFastC = coefFromTau (0.0015f, sr);
    const float transSlowC = coefFromTau (0.120f,  sr);
    const float agC        = coefFromTau (0.010f,  sr);

    const float invSR = 1.0f / (float) sr;

    float* left  = buffer.getWritePointer (0);
    float* right = stereo ? buffer.getWritePointer (1) : nullptr;

    float blockPeak = 0.0f;
    float meter     = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = left[n];
        const float inR = stereo ? right[n] : inL;
        const float mono = 0.5f * (inL + inR);

        // dry three-way split
        float lowL, restL, midL, highL;
        loSplit.processSample (0, inL, lowL, restL);
        hiSplit.processSample (0, restL, midL, highL);

        float lowR = lowL, midR = midL, highR = highL;
        if (stereo)
        {
            float restR;
            loSplit.processSample (1, inR, lowR, restR);
            hiSplit.processSample (1, restR, midR, highR);
        }

        // filterbank / oscillator bank
        float tonalL = 0.0f, tonalR = 0.0f;
        const float shimmer = smShimmer.getNextValue();

        for (int k = firstBand; k <= lastBand; ++k)
        {
            auto& b = bands[(size_t) k];

            // two cascaded biquads, transposed direct form II
            float y = b.b0 * mono + b.s1a;
            b.s1a = b.b1 * mono - b.a1 * y + b.s2a;
            b.s2a = b.b2 * mono - b.a2 * y;

            const float x2 = y;
            float y2 = b.b0 * x2 + b.s1b;
            b.s1b = b.b1 * x2 - b.a1 * y2 + b.s2b;
            b.s2b = b.b2 * x2 - b.a2 * y2;

            // RMS envelope; time constant scales with the band's own period
            if (! freeze)
            {
                b.envSq += b.envCoef * (y2 * y2 - b.envSq);
                b.amp = std::sqrt (b.envSq) * 1.41421356f;
            }

            // ISOLATE - per-oscillator noise gate
            const float gTarget = (! b.muted && b.amp > gateThresh) ? 1.0f : 0.0f;
            b.gate += (gTarget > b.gate ? gateAttack : gateRelease) * (gTarget - b.gate);

            // GLIDE on the oscillator frequency
            b.freq += glideCoef * (b.targetFreq - b.freq);

            b.phase += b.freq * invSR;
            if (b.phase >= 1.0f) b.phase -= 1.0f;

            float s = sine (b.phase);
            if (shimmer > 0.001f)
            {
                float p2 = b.phase * 2.0f;
                p2 -= (float) (int) p2;
                s += shimmer * 0.7f * sine (p2);          // phase-locked octave up
            }

            s *= b.amp * b.gate * b.ampGain;
            tonalL += s * b.panL;
            tonalR += s * b.panR;

            blockPeak = juce::jmax (blockPeak, b.amp);
        }

        // transient detector on the dry mid band
        const float rect = std::abs (midL) + std::abs (midR);
        transFast += transFastC * (rect - transFast);
        transSlow += transSlowC * (rect - transSlow);
        const float trans = juce::jlimit (0.0f, 1.0f,
                              (transFast / (transSlow + 1.0e-5f) - 1.0f) * 0.7f);

        // ISOLATE's hidden audio gate
        const float agTarget = (transSlow > 6.0e-4f) ? 1.0f : 0.0f;
        audioGate += agC * (agTarget - audioGate);
        const float ag = 1.0f - isolate * (1.0f - audioGate);

        // AMOUNT: tonal vs noise
        const float amount    = smAmount.getNextValue();
        const float noiseGain = juce::jmin (1.0f, (1.0f - amount)
                                                  + trans * (1.0f - amount * 0.6f));
        const float level = smLevel.getNextValue() * 0.55f;   // filterbank sum trim

        float wetL = tonalL * amount * level * ag + midL * noiseGain;
        float wetR = tonalR * amount * level * ag + midR * noiseGain;

        if (! muteLo)  { wetL += lowL;  wetR += lowR;  }
        if (! muteHi)  { wetL += highL; wetR += highR; }

        const float mix = smMix.getNextValue();
        const float outL = inL * (1.0f - mix) + wetL * mix;
        const float outR = inR * (1.0f - mix) + wetR * mix;

        left[n] = outL;
        if (stereo) right[n] = outR;
        else        left[n]  = 0.5f * (outL + outR);

        meter = juce::jmax (meter, std::abs (outL));
    }

    // peak tracking + UI feedback
    globalPeak = juce::jmax (blockPeak, globalPeak * 0.92f);
    outputMeter.store (meter);

    for (auto& d : display)
        d.store (d.load() * displayDecay);

    if (numBands > 0)
    {
        for (int k = firstBand; k <= lastBand; ++k)
        {
            const int bin = juce::jlimit (0, numDisplayBins - 1,
                                          (k * numDisplayBins) / juce::jmax (1, numBands));
            const float v = juce::jlimit (0.0f, 1.0f,
                                std::sqrt (bands[(size_t) k].amp * bands[(size_t) k].gate * 6.0f));
            if (v > display[(size_t) bin].load())
                display[(size_t) bin].store (v);
        }
    }
}

void SpectralLockAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectralLockAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SpectralLockAudioProcessor::createEditor()
{
    return new SpectralLockAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralLockAudioProcessor();
}
