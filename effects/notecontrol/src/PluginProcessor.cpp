#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>
#include <algorithm>

// ============================================================================
// Parameter layout
// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
NoteControlAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dryWet", "Dry/Wet", 0.0f, 1.0f, 1.0f));
    return { params.begin(), params.end() };
}

// ============================================================================
// Constructor / Destructor
// ============================================================================
NoteControlAudioProcessor::NoteControlAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "NoteControl", createParameterLayout())
{
    pDryWet = apvts.getRawParameterValue("dryWet");
    resetRouting();
    std::fill(std::begin(binNoteClass), std::end(binNoteClass), -1);
    std::fill(std::begin(binShiftAudio), std::end(binShiftAudio), 0);
    std::fill(std::begin(binShiftPending), std::end(binShiftPending), 0);
    std::fill(std::begin(smoothedMag), std::end(smoothedMag), 0.0f);
}

NoteControlAudioProcessor::~NoteControlAudioProcessor() {}

// ============================================================================
// Routing helpers
// ============================================================================
void NoteControlAudioProcessor::resetRouting()
{
    for (int n = 0; n < 12; ++n)
        noteRouting[n] = n;
}

void NoteControlAudioProcessor::applyPentatonicRouting()
{
    // Snap every note to nearest C-major pentatonic: C(0) D(2) E(4) G(7) A(9)
    //   C#(1)  → C(0)
    //   D#(3)  → E(4)
    //   F (5)  → E(4)
    //   F#(6)  → G(7)
    //   G#(8)  → G(7)   ← nearest pentatonic below
    //   A#(10) → A(9)
    //   B (11) → A(9)   ← nearest pentatonic below
    static constexpr int kPentaMap[12] = {
        0,  // C  → C
        0,  // C# → C
        2,  // D  → D
        4,  // D# → E
        4,  // E  → E
        4,  // F  → E
        7,  // F# → G
        7,  // G  → G
        7,  // G# → G   (was A, corrected)
        9,  // A  → A
        9,  // A# → A
        9,  // B  → A   (was C, corrected)
    };
    for (int n = 0; n < 12; ++n)
        noteRouting[n] = kPentaMap[n];
}

void NoteControlAudioProcessor::shiftInputs(int semitones)
{
    // Scroll rows: the *input* note slots rotate.
    // noteRouting[n] stays put but the rows cycle — same as rotating the
    // whole array by `semitones` positions.
    // After +1: note that was on row 1 is now on row 0, etc.
    const int d = ((semitones % 12) + 12) % 12;
    if (d == 0) return;
    int tmp[12];
    for (int n = 0; n < 12; ++n)
        tmp[n] = noteRouting[(n + d) % 12];
    for (int n = 0; n < 12; ++n)
        noteRouting[n] = tmp[n];
}

void NoteControlAudioProcessor::shiftOutputs(int semitones)
{
    // Scroll columns: every non-muted target moves by `semitones`, wrapping.
    const int d = ((semitones % 12) + 12) % 12;
    if (d == 0) return;
    for (int n = 0; n < 12; ++n)
        if (noteRouting[n] >= 0)
            noteRouting[n] = (noteRouting[n] + d) % 12;
}

void NoteControlAudioProcessor::computeBinNoteAssignments()
{
    for (int b = 0; b < numBins; ++b)
    {
        binNoteClass[b] = -1;
        if (b == 0) continue;

        const double freq = static_cast<double>(b) * currentSampleRate
            / static_cast<double>(fftSize);
        if (freq <= 0.0) continue;

        const double midiF = 12.0 * std::log2(freq / 440.0) + 69.0;
        const int    midiR = static_cast<int>(std::round(midiF));
        binNoteClass[b] = ((midiR % 12) + 12) % 12;   // 0=C … 11=B
    }
}

void NoteControlAudioProcessor::buildPendingShiftTable()
{
    computeBinNoteAssignments();

    for (int b = 0; b < numBins; ++b)
    {
        binShiftPending[b] = 0;

        const int nc = binNoteClass[b];
        if (nc < 0) continue;

        const int tgt = noteRouting[nc];
        if (tgt < 0) { binShiftPending[b] = MUTE_SENTINEL; continue; }
        if (tgt == nc) { binShiftPending[b] = 0; continue; }

        // Compute octave-aware integer bin shift
        const double freq = static_cast<double>(b) * currentSampleRate
            / static_cast<double>(fftSize);
        const double midiF = 12.0 * std::log2(freq / 440.0) + 69.0;
        const int    midiR = static_cast<int>(std::round(midiF));
        const int    octave = (midiR - nc) / 12;

        const int    midiSrc = octave * 12 + nc;
        const int    midiDst = octave * 12 + tgt;
        const double freqSrc = 440.0 * std::pow(2.0, (midiSrc - 69) / 12.0);
        const double freqDst = 440.0 * std::pow(2.0, (midiDst - 69) / 12.0);

        const int cBinSrc = static_cast<int>(
            std::round(freqSrc * static_cast<double>(fftSize) / currentSampleRate));
        const int cBinDst = static_cast<int>(
            std::round(freqDst * static_cast<double>(fftSize) / currentSampleRate));

        binShiftPending[b] = cBinDst - cBinSrc;
    }

    shiftTableDirty.store(true);
}

// Compute the octave-aware integer bin shift for note class nc → tgt at bin b
static int computeShiftForBin(int b, int nc, int tgt,
    double sampleRate, int fftSz)
{
    if (tgt < 0)  return NoteControlAudioProcessor::maxContrib; // sentinel abuse — caller handles
    if (tgt == nc) return 0;

    const double freq = static_cast<double>(b) * sampleRate / static_cast<double>(fftSz);
    const double midiF = 12.0 * std::log2(freq / 440.0) + 69.0;
    const int    midiR = static_cast<int>(std::round(midiF));
    const int    octave = (midiR - nc) / 12;

    const double freqSrc = 440.0 * std::pow(2.0, (octave * 12 + nc - 69) / 12.0);
    const double freqDst = 440.0 * std::pow(2.0, (octave * 12 + tgt - 69) / 12.0);

    const int cBinSrc = static_cast<int>(std::round(freqSrc * fftSz / sampleRate));
    const int cBinDst = static_cast<int>(std::round(freqDst * fftSz / sampleRate));

    return cBinDst - cBinSrc;
}

void NoteControlAudioProcessor::buildPendingSoftTable()
{
    // computeBinNoteAssignments() must already have been called.
    //
    // Algorithm for each bin b:
    //  1. Compute continuous MIDI note (midiF) and cents offset from each
    //     of the 12 note classes.
    //  2. Keep the nearest maxContrib note classes whose |cents| < 3*sigma.
    //  3. Apply Gaussian weighting and normalise so weights sum to 1.
    //  4. Store (shift, weight) pairs; muted notes contribute shift=MUTE_SENTINEL
    //     but weight 0 (they are simply skipped in processBlock).

    for (int b = 0; b < numBins; ++b)
    {
        softCountPending[b] = 0;

        if (b == 0) continue;   // DC — skip

        const double freq = static_cast<double>(b) * currentSampleRate
            / static_cast<double>(fftSize);
        if (freq <= 0.0) continue;

        const double midiF = 12.0 * std::log2(freq / 440.0) + 69.0;

        // For each of the 12 note classes, find the nearest octave instance
        // and compute the cents distance from bin b to that instance.
        struct Candidate { int noteClass; float centsDist; };
        Candidate candidates[12];

        for (int n = 0; n < 12; ++n)
        {
            // Nearest MIDI note of class n to midiF:
            // midiR = round(midiF), nc_round = midiR%12
            // delta needed to reach n, snapped to [-6, +6]
            const int midiR = static_cast<int>(std::round(midiF));
            const int ncRound = ((midiR % 12) + 12) % 12;
            int delta = n - ncRound;
            if (delta > 6) delta -= 12;
            if (delta < -6) delta += 12;
            const float centsDist = std::abs(
                static_cast<float>((midiF - (midiR + delta)) * 100.0));
            candidates[n] = { n, centsDist };
        }

        // Sort by cents distance ascending, keep top maxContrib within 3*sigma
        const float cutoff = 3.0f * kGaussSigma;
        std::sort(std::begin(candidates), std::end(candidates),
            [](const Candidate& a, const Candidate& b) { return a.centsDist < b.centsDist; });

        // Compute raw Gaussian weights and accumulate only non-muted contributions
        float weightSum = 0.0f;
        int   count = 0;
        float rawW[maxContrib]{};
        int   noteIdx[maxContrib]{};

        for (int k = 0; k < 12 && count < maxContrib; ++k)
        {
            if (candidates[k].centsDist > cutoff) break;

            const int nc = candidates[k].noteClass;
            const int tgt = noteRouting[nc];

            // Muted notes: skip them entirely — their energy is dropped
            if (tgt < 0) continue;

            const float w = std::exp(-0.5f * (candidates[k].centsDist / kGaussSigma)
                * (candidates[k].centsDist / kGaussSigma));
            rawW[count] = w;
            noteIdx[count] = nc;
            weightSum += w;
            ++count;
        }

        if (count == 0 || weightSum < 1e-9f) continue;

        // Store normalised (shift, weight) pairs
        for (int k = 0; k < count; ++k)
        {
            const int nc = noteIdx[k];
            const int tgt = noteRouting[nc];
            const int shift = computeShiftForBin(b, nc, tgt,
                currentSampleRate, fftSize);
            softTablePending[b][k] = { shift, rawW[k] / weightSum };
        }
        softCountPending[b] = count;
    }
}

void NoteControlAudioProcessor::rebuildBinShiftTable()
{
    juce::ScopedLock sl(shiftTableLock);
    buildPendingShiftTable();
    buildPendingSoftTable();
    std::copy(std::begin(binShiftPending), std::end(binShiftPending),
        std::begin(binShiftAudio));
    for (int b = 0; b < numBins; ++b)
    {
        softCountAudio[b] = softCountPending[b];
        for (int k = 0; k < maxContrib; ++k)
            softTableAudio[b][k] = softTablePending[b][k];
    }
    shiftTableDirty.store(false);
}

// ============================================================================
// Prepare / Release
// ============================================================================
bool NoteControlAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

void NoteControlAudioProcessor::allocateBuffers()
{
    // Ring length for the OLA accumulator must accommodate fftSize synthesis
    // output plus headroom for the full window. Using 2*fftSize is safe.
    const int ringLen = fftSize * 2;

    for (int ch = 0; ch < maxCh; ++ch)
    {
        inputFifo[ch].assign(fftSize, 0.0f);
        outputAccum[ch].assign(ringLen, 0.0f);

        fifoWritePos[ch] = 0;

        // readHead starts at 0.
        // writeHead starts (fftSize - hopSize) positions ahead so the first
        // OLA frame writes to the right place for the latency we report.
        accumReadPos[ch] = 0;
        accumWritePos[ch] = fftSize - hopSize;   // initial lead = latency
    }

    hopSampleCount = 0;
    setLatencySamples(fftSize - hopSize);
}

void NoteControlAudioProcessor::createWindow()
{
    window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize));
}

void NoteControlAudioProcessor::prepareToPlay(double sampleRate, int /*spb*/)
{
    currentSampleRate = sampleRate;
    fftEngine = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
    createWindow();
    std::fill(std::begin(smoothedMag), std::end(smoothedMag), 0.0f);
    rebuildBinShiftTable();
}

void NoteControlAudioProcessor::releaseResources()
{
    fftEngine.reset();
}

// ============================================================================
// processBlock  —  WOLA with explicit read/write heads
// ============================================================================
void NoteControlAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int   numCh = std::min(buffer.getNumChannels(), maxCh);
    const int   numSamples = buffer.getNumSamples();
    const float dryWet = pDryWet ? pDryWet->load() : 1.0f;
    const int   ringLen = fftSize * 2;

    // Swap in updated shift table if the editor changed the routing
    if (shiftTableDirty.load())
    {
        juce::ScopedLock sl(shiftTableLock);
        if (shiftTableDirty.load())
        {
            std::copy(std::begin(binShiftPending), std::end(binShiftPending),
                std::begin(binShiftAudio));
            for (int b = 0; b < numBins; ++b)
            {
                softCountAudio[b] = softCountPending[b];
                for (int k = 0; k < maxContrib; ++k)
                    softTableAudio[b][k] = softTablePending[b][k];
            }
            shiftTableDirty.store(false);
        }
    }

    // Read gate range once per block (lock-free atomics)
    const int gateLo = gatePosTobin(juce::jlimit(0.0f, 1.0f, gateStart.load()));
    const int gateHi = gatePosTobin(juce::jlimit(0.0f, 1.0f, gateEnd  .load()));

    // Dry copy for blend
    juce::AudioBuffer<float> dryBuf;
    if (dryWet < 0.9999f)
        dryBuf.makeCopyOf(buffer);

    // Reusable thread-local FFT workspace
    static thread_local std::vector<float> analysis(fftSize * 2, 0.0f);
    static thread_local std::vector<float> scattered(fftSize * 2, 0.0f);

    // Normalisation for double-Hann WOLA at 75 % overlap:
    // sum_m w²[n - m·H] = 1.5  →  norm = 1 / 1.5 = 2/3
    const float norm = 2.0f / 3.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        // --- Push one sample into each channel's input ring FIFO ---
        for (int ch = 0; ch < numCh; ++ch)
        {
            inputFifo[ch][fifoWritePos[ch]] = buffer.getSample(ch, s);
        }
        // Advance shared write head (channels stay in lock-step)
        for (int ch = 0; ch < numCh; ++ch)
            fifoWritePos[ch] = (fifoWritePos[ch] + 1) % fftSize;

        ++hopSampleCount;

        // --- Process FFT frame every hopSize samples ---
        if (hopSampleCount >= hopSize)
        {
            hopSampleCount = 0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                // Build windowed analysis frame (most-recent fftSize samples)
                const int oldest = fifoWritePos[ch]; // oldest = current write pos (ring just advanced)
                for (int i = 0; i < fftSize; ++i)
                {
                    const int ri = (oldest + i) % fftSize;
                    analysis[i] = inputFifo[ch][ri] * window[i];
                    analysis[i + fftSize] = 0.0f;
                }

                // Forward FFT (real-only, in-place)
                fftEngine->performRealOnlyForwardTransform(analysis.data(), true);

                // Scatter bins — hard mode or soft Gaussian enhance mode
                std::fill(scattered.begin(), scattered.end(), 0.0f);
                const bool enhance = enhanceMode.load();

                if (!enhance)
                {
                    // --------------------------------------------------------
                    // HARD MODE: each bin owned 100% by one note, integer shift
                    // Bins outside [gateLo, gateHi] are passed through unchanged.
                    // --------------------------------------------------------
                    for (int b = 0; b < numBins; ++b)
                    {
                        // Outside gate: passthrough
                        if (b < gateLo || b > gateHi)
                        {
                            scattered[2 * b]     = analysis[2 * b];
                            scattered[2 * b + 1] = analysis[2 * b + 1];
                            continue;
                        }

                        const int shift = binShiftAudio[b];
                        if (shift == MUTE_SENTINEL) continue;

                        const int dst = b + shift;
                        if (dst < 0 || dst >= numBins) continue;

                        scattered[2 * dst] += analysis[2 * b];
                        scattered[2 * dst + 1] += analysis[2 * b + 1];
                    }
                }
                else
                {
                    // --------------------------------------------------------
                    // ENHANCE MODE: Gaussian soft weights across note boundaries
                    //
                    // Each bin blends its energy between up to maxContrib notes
                    // weighted by a Gaussian (sigma = kGaussSigma cents).
                    // Weights are pre-normalised so total energy is conserved.
                    //
                    // After scatter we apply phase coherence:  for every
                    // destination bin that received contributions, we keep its
                    // accumulated magnitude but replace the phase with the
                    // phase of whichever single source contributed the most
                    // energy.  This removes the destructive-interference
                    // artefacts you'd otherwise get when two shifted bins land
                    // on the same destination with unrelated phases.
                    // --------------------------------------------------------

                    // Per-destination: track dominant-contributor phase
                    static thread_local std::vector<float> dominantMag(numBins, 0.0f);
                    static thread_local std::vector<float> dominantPhRe(numBins, 0.0f);
                    static thread_local std::vector<float> dominantPhIm(numBins, 0.0f);
                    std::fill(dominantMag.begin(), dominantMag.end(), 0.0f);
                    std::fill(dominantPhRe.begin(), dominantPhRe.end(), 0.0f);
                    std::fill(dominantPhIm.begin(), dominantPhIm.end(), 0.0f);

                    for (int b = 0; b < numBins; ++b)
                    {
                        const float re_b = analysis[2 * b];
                        const float im_b = analysis[2 * b + 1];

                        // Outside gate: passthrough
                        if (b < gateLo || b > gateHi)
                        {
                            scattered[2 * b]     = re_b;
                            scattered[2 * b + 1] = im_b;
                            continue;
                        }

                        const int   cnt = softCountAudio[b];

                        for (int k = 0; k < cnt; ++k)
                        {
                            const int   shift = softTableAudio[b][k].shift;
                            const float w = softTableAudio[b][k].weight;

                            if (shift == MUTE_SENTINEL) continue;

                            const int dst = b + shift;
                            if (dst < 0 || dst >= numBins) continue;

                            // Weighted accumulation
                            scattered[2 * dst] += re_b * w;
                            scattered[2 * dst + 1] += im_b * w;

                            // Track which source contributed most magnitude to dst
                            const float contrib = (re_b * re_b + im_b * im_b) * w * w;
                            if (contrib > dominantMag[dst])
                            {
                                dominantMag[dst] = contrib;
                                dominantPhRe[dst] = re_b;
                                dominantPhIm[dst] = im_b;
                            }
                        }
                    }

                    // Phase coherence pass:
                    // For each output bin, keep the accumulated *magnitude*
                    // but snap the *phase* to that of the dominant contributor.
                    // This eliminates phase-cancel artefacts at note boundaries
                    // where two shifted sources with unrelated phases overlap.
                    for (int b = 0; b < numBins; ++b)
                    {
                        const float accRe = scattered[2 * b];
                        const float accIm = scattered[2 * b + 1];
                        const float accMag = std::sqrt(accRe * accRe + accIm * accIm);

                        if (accMag < 1e-12f) continue;

                        // Normalise dominant phase vector
                        const float dRe = dominantPhRe[b];
                        const float dIm = dominantPhIm[b];
                        const float dMag = std::sqrt(dRe * dRe + dIm * dIm);

                        if (dMag < 1e-12f) continue;

                        // Replace phase, keep accumulated magnitude
                        const float scale = accMag / dMag;
                        scattered[2 * b] = dRe * scale;
                        scattered[2 * b + 1] = dIm * scale;
                    }
                }

                // Reconstruct conjugate mirror for real IFFT
                for (int b = 1; b < numBins - 1; ++b)
                {
                    scattered[2 * (fftSize - b)] = scattered[2 * b];
                    scattered[2 * (fftSize - b) + 1] = -scattered[2 * b + 1];
                }

                // Update display magnitude (channel 0 only)
                if (ch == 0)
                {
                    const float alpha = 0.1f;
                    juce::ScopedLock sl(displayLock);
                    for (int b = 0; b < numBins; ++b)
                    {
                        const float re = scattered[2 * b];
                        const float im = scattered[2 * b + 1];
                        const float m = std::sqrt(re * re + im * im);
                        smoothedMag[b] += alpha * (m - smoothedMag[b]);
                    }
                }

                // Inverse FFT (real-only, in-place)
                fftEngine->performRealOnlyInverseTransform(scattered.data());

                // Overlap-add into accumulator at writeHead, apply synthesis window
                int wh = accumWritePos[ch];
                for (int i = 0; i < fftSize; ++i)
                {
                    outputAccum[ch][wh] += scattered[i] * window[i] * norm;
                    wh = (wh + 1) % ringLen;
                }

                // Advance write head by one hop
                accumWritePos[ch] = (accumWritePos[ch] + hopSize) % ringLen;
            }
        }

        // --- Drain one sample per channel from the accumulator ---
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float out = outputAccum[ch][accumReadPos[ch]];
            outputAccum[ch][accumReadPos[ch]] = 0.0f;  // clear after read
            buffer.setSample(ch, s, out);
            accumReadPos[ch] = (accumReadPos[ch] + 1) % ringLen;
        }
    }

    // Dry/wet blend
    if (dryWet < 0.9999f)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* dry = dryBuf.getReadPointer(ch);
            float* wet = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s)
                wet[s] = dry[s] * (1.0f - dryWet) + wet[s] * dryWet;
        }
    }
}

// ============================================================================
// FFT display
// ============================================================================
void NoteControlAudioProcessor::getFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock sl(displayLock);
    const int n = std::min(numBinsOut, numBins);
    std::copy(smoothedMag, smoothedMag + n, out);
}

// ============================================================================
// State save/restore
// ============================================================================
void NoteControlAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::XmlElement xml("NoteControlState");
    xml.addChildElement(state.createXml().release());
    auto* re = xml.createNewChildElement("NoteRouting");
    for (int n = 0; n < 12; ++n)
        re->setAttribute("n" + juce::String(n), noteRouting[n]);
    xml.setAttribute("enhance",   (int)enhanceMode.load());
    xml.setAttribute("gateStart", (double)gateStart.load());
    xml.setAttribute("gateEnd",   (double)gateEnd  .load());
    copyXmlToBinary(xml, destData);
}

void NoteControlAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (auto* el = xml->getChildByName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*el));
        if (auto* re = xml->getChildByName("NoteRouting"))
        {
            for (int n = 0; n < 12; ++n)
                noteRouting[n] = re->getIntAttribute("n" + juce::String(n), n);
            rebuildBinShiftTable();
        }
        enhanceMode.store(xml->getIntAttribute("enhance", 0) != 0);
        gateStart.store((float)xml->getDoubleAttribute("gateStart", 0.0));
        gateEnd  .store((float)xml->getDoubleAttribute("gateEnd",   1.0));
    }
}

// ============================================================================
// Plugin factory
// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoteControlAudioProcessor();
}