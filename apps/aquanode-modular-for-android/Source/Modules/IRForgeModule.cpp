#include "IRForgeModule.h"

using namespace aquanode;

//==============================================================================
// what the build thread works from
//==============================================================================
struct IRForgeModule::Source
{
    std::shared_ptr<const juce::MemoryBlock> encoded;        // a picked file, decoded on demand
    std::shared_ptr<const juce::AudioBuffer<float>> pcm;     // a recorded take
    double rate { 44100.0 };
    juce::int64 length { 0 };
    int numChannels { 1 };
    int generation { 0 };
};

struct IRForgeModule::ForgeKey
{
    int sourceGen { -1 };
    juce::int64 start { 0 }, end { 0 };
    int order { 0 };
    float character { 0.0f };
    bool linear { false };
    double rate { 0.0 };

    bool operator== (const ForgeKey& o) const
    {
        return sourceGen == o.sourceGen && start == o.start && end == o.end && order == o.order
            && std::abs (character - o.character) < 1.0e-6f && linear == o.linear && std::abs (rate - o.rate) < 1.0e-6;
    }
};

struct IRForgeModule::ShapeKey
{
    int coreGen { -1 };
    float length { 0 }, decay { 0 }, stretch { 0 }, predelay { 0 }, lowCut { 0 }, highCut { 0 }, tilt { 0 }, width { 0 };
    bool reverse { false };

    bool operator== (const ShapeKey& o) const
    {
        auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
        return coreGen == o.coreGen && same (length, o.length) && same (decay, o.decay) && same (stretch, o.stretch)
            && same (predelay, o.predelay) && same (lowCut, o.lowCut) && same (highCut, o.highCut)
            && same (tilt, o.tilt) && same (width, o.width) && reverse == o.reverse;
    }
};

namespace
{
    constexpr int kOverviewPoints = 400;
    constexpr double kMaxCoreSeconds = 4.0;        // the Length knob's top
    constexpr double kMaxFinalSeconds = 12.0;      // after Stretch and Predelay
    constexpr size_t kMaxFileBytes = 400u * 1024u * 1024u;

    juce::AudioFormatManager& forgeFormats()
    {
        struct Formats
        {
            Formats()
            {
                manager.registerBasicFormats();
               #if JUCE_USE_MP3AUDIOFORMAT
                manager.registerFormat (new juce::MP3AudioFormat(), false);
               #endif
            }
            juce::AudioFormatManager manager;
        };
        static Formats formats;   // thread-safe initialisation
        return formats.manager;
    }

    std::unique_ptr<juce::AudioFormatReader> readerFor (const juce::MemoryBlock& data)
    {
        return std::unique_ptr<juce::AudioFormatReader> (forgeFormats().createReaderFor (
            std::make_unique<juce::MemoryInputStream> (data.getData(), data.getSize(), false)));
    }

    // sequential reads from either kind of source
    struct RangeReader
    {
        RangeReader (const std::shared_ptr<const juce::MemoryBlock>& encoded,
                     const std::shared_ptr<const juce::AudioBuffer<float>>& pcmIn)
            : pcm (pcmIn)
        {
            if (encoded != nullptr)
                reader = readerFor (*encoded);
        }

        bool ok() const { return reader != nullptr || pcm != nullptr; }

        // reads numSamples starting at pos into dest[0..numChannels) at offset
        void read (juce::AudioBuffer<float>& dest, int destOffset, int numSamples, juce::int64 pos)
        {
            if (reader != nullptr)
            {
                reader->read (&dest, destOffset, numSamples, pos, true, dest.getNumChannels() > 1);
                return;
            }
            for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            {
                const int srcCh = juce::jmin (ch, pcm->getNumChannels() - 1);
                const auto avail = juce::jlimit ((juce::int64) 0, (juce::int64) numSamples, (juce::int64) pcm->getNumSamples() - pos);
                if (avail > 0)
                    dest.copyFrom (ch, destOffset, *pcm, srcCh, (int) pos, (int) avail);
                if (avail < numSamples)
                    dest.clear (ch, destOffset + (int) avail, numSamples - (int) avail);
            }
        }

        std::unique_ptr<juce::AudioFormatReader> reader;
        std::shared_ptr<const juce::AudioBuffer<float>> pcm;
    };

    void overviewOf (const juce::AudioBuffer<float>& chunk, int len, juce::int64 pos, juce::int64 total,
                     std::vector<float>& mn, std::vector<float>& mx)
    {
        for (int ch = 0; ch < chunk.getNumChannels(); ++ch)
        {
            const auto* d = chunk.getReadPointer (ch);
            for (int i = 0; i < len; ++i)
            {
                const auto bin = (size_t) juce::jlimit ((juce::int64) 0, (juce::int64) kOverviewPoints - 1,
                                                        (pos + i) * kOverviewPoints / juce::jmax ((juce::int64) 1, total));
                mn[bin] = juce::jmin (mn[bin], d[i]);
                mx[bin] = juce::jmax (mx[bin], d[i]);
            }
        }
    }

    //==========================================================================
    // log|H| -> real cepstrum -> lifter -> minimum (or zero) phase IR.
    // Doubling the positive quefrencies and dropping the negative ones is the
    // folding that makes the result minimum phase (the Hilbert relation
    // between log magnitude and phase); the symmetric lifter keeps zero phase.
    //==========================================================================
    void cepstralIR (const std::vector<float>& logMag, std::vector<float>& out, int order, int lifter, bool linear)
    {
        const int n = 1 << order, half = n / 2;
        juce::dsp::FFT fft (order);
        std::vector<juce::dsp::Complex<float>> a ((size_t) n), b ((size_t) n);

        for (int i = 0; i <= half; ++i)
            a[(size_t) i] = { logMag[(size_t) i], 0.0f };
        for (int i = half + 1; i < n; ++i)
            a[(size_t) i] = { logMag[(size_t) (n - i)], 0.0f };
        fft.perform (a.data(), b.data(), true);                     // -> cepstrum

        const int k = juce::jlimit (2, half, lifter);
        std::fill (a.begin(), a.end(), juce::dsp::Complex<float> { 0.0f, 0.0f });
        a[0] = { b[0].real(), 0.0f };
        for (int i = 1; i < k; ++i)
        {
            if (linear)
            {
                a[(size_t) i]       = { b[(size_t) i].real(), 0.0f };
                a[(size_t) (n - i)] = { b[(size_t) (n - i)].real(), 0.0f };
            }
            else
            {
                a[(size_t) i] = { b[(size_t) i].real() * 2.0f, 0.0f };
            }
        }
        if (k >= half)
            a[(size_t) half] = { b[(size_t) half].real(), 0.0f };

        fft.perform (a.data(), b.data(), false);                    // -> log spectrum (+ phase)
        for (int i = 0; i < n; ++i)
        {
            const float mag = std::exp (juce::jlimit (-60.0f, 60.0f, b[(size_t) i].real()));
            const float ph = b[(size_t) i].imag();
            a[(size_t) i] = { mag * std::cos (ph), mag * std::sin (ph) };
        }
        fft.perform (a.data(), b.data(), true);                     // -> impulse response

        out.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = b[(size_t) i].real();
        if (linear)
            std::rotate (out.begin(), out.begin() + half, out.end());   // centre it
    }

    void stretchInPlace (juce::AudioBuffer<float>& buf, float ratio, int maxLen)
    {
        if (std::abs (ratio - 1.0f) < 1.0e-4f)
            return;
        const int inLen = buf.getNumSamples();
        const int outLen = juce::jlimit (16, maxLen, (int) ((float) inLen * ratio));
        juce::AudioBuffer<float> dst (buf.getNumChannels(), outLen);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const auto* in = buf.getReadPointer (ch);
            auto* o = dst.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i)
            {
                const float pos = (float) i / ratio;
                const int i0 = (int) pos;
                const float fr = pos - (float) i0;
                const float a = i0 < inLen ? in[i0] : 0.0f;
                const float b = i0 + 1 < inLen ? in[i0 + 1] : 0.0f;
                o[i] = a + (b - a) * fr;
            }
        }
        buf = std::move (dst);
    }

}

//==============================================================================
IRForgeModule::IRForgeModule() : juce::Thread ("IRForge forge")
{
    lastForge = std::make_unique<ForgeKey>();
    lastShape = std::make_unique<ShapeKey>();
}

IRForgeModule::~IRForgeModule()
{
    signalThreadShouldExit();
    notify();
    stopThread (5000);
}

void IRForgeModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    engineRate.store (sr);

    if (recording.exchange (false))
        takePending.store (true);   // the take so far is kept

    {
        const juce::ScopedLock sl (convLock);
        convolution.prepare ({ sr, (juce::uint32) kBlock, 2 });
        convolution.reset();
    }
    convPrepared.store (true);

    fadeCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    smCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
    reset();

    reshapeRequested.store (true);
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
    notify();
}

void IRForgeModule::reset()
{
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
            inBuf[ch][i] = outBuf[ch][i] = dlyBuf[ch][i] = 0.0f;
    blockPos = 0;
    fade = irReady.load() ? 1.0f : 0.0f;
    matchGain = param (pMatch) > 0.5f ? juce::jlimit (0.001f, 20.0f, energyGain.load() * kMatchLevel) : 1.0f;
    smMix = param (pMix) * 0.01f;
    smOut = juce::Decibels::decibelsToGain (param (pOutput));
    {
        const juce::ScopedTryLock tl (convLock);   // never wait on the audio thread
        if (tl.isLocked())
            convolution.reset();
    }
}

//==============================================================================
void IRForgeModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // 1. the recorder taps Rec In
    if (recording.load (std::memory_order_acquire))
    {
        const int p = recPos.load (std::memory_order_relaxed);
        if (p < recBuffer.getNumSamples())
        {
            recBuffer.setSample (0, p, inputs[0][0]);
            recBuffer.setSample (1, p, inputs[0][1]);
            recPos.store (p + 1, std::memory_order_release);
        }
        else
        {
            recording.store (false, std::memory_order_release);   // three minutes: auto stop
            takePending.store (true, std::memory_order_release);
        }
    }

    // 2. Audio In through the convolver
    const float inL = inputs[1][0], inR = inputs[1][1];
    const float target = irReady.load (std::memory_order_relaxed) ? 1.0f : 0.0f;

    if (target < 0.5f && fade < 1.0e-4f)
    {
        fade = 0.0f;
        outputs[0][0] = inL;    // nothing forged yet: dry, no latency
        outputs[0][1] = inR;
        return;
    }

    fade += fadeCoef * (target - fade);
    const float matchTarget = param (pMatch) > 0.5f
                                ? juce::jlimit (0.001f, 20.0f, energyGain.load (std::memory_order_relaxed) * kMatchLevel)
                                : 1.0f;
    matchGain += smCoef * (matchTarget - matchGain);
    smMix += smCoef * (juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f) - smMix);
    smOut += smCoef * (juce::Decibels::decibelsToGain (param (pOutput)) - smOut);

    const float wetL = outBuf[0][blockPos], wetR = outBuf[1][blockPos];
    const float dryL = dlyBuf[0][blockPos], dryR = dlyBuf[1][blockPos];
    dlyBuf[0][blockPos] = inBuf[0][blockPos] = inL;
    dlyBuf[1][blockPos] = inBuf[1][blockPos] = inR;

    if (++blockPos >= kBlock)
    {
        blockPos = 0;
        float* inPtrs[2] = { inBuf[0], inBuf[1] };
        float* outPtrs[2] = { outBuf[0], outBuf[1] };
        juce::dsp::AudioBlock<float> inBlock (inPtrs, 2, (size_t) kBlock);
        juce::dsp::AudioBlock<float> outBlock (outPtrs, 2, (size_t) kBlock);
        convolution.process (juce::dsp::ProcessContextNonReplacing<float> (inBlock, outBlock));
    }

    const float g = matchGain * smOut;
    const float mixedL = dryL * (1.0f - smMix) + wetL * g * smMix;
    const float mixedR = dryR * (1.0f - smMix) + wetR * g * smMix;
    outputs[0][0] = inL * (1.0f - fade) + mixedL * fade;
    outputs[0][1] = inR * (1.0f - fade) + mixedR * fade;
}

//==============================================================================
// message thread
//==============================================================================
void IRForgeModule::toggleRecording()
{
    if (recording.load())
    {
        recording.store (false, std::memory_order_release);
        takePending.store (true, std::memory_order_release);
        notify();
        return;
    }
    if (takePending.load())
        return;   // the last take is still being picked up

    const juce::ScopedLock sl (recLock);
    const double sr = engineRate.load();
    const int capacity = (int) (sr * kMaxRecordSeconds);
    if (recBuffer.getNumSamples() != capacity)
        recBuffer.setSize (2, capacity, false, false, false);
    recRate = sr;
    recPos.store (0, std::memory_order_release);
    recording.store (true, std::memory_order_release);
}

double IRForgeModule::recordedSeconds() const
{
    return recRate > 0.0 ? (double) recPos.load (std::memory_order_relaxed) / recRate : 0.0;
}

void IRForgeModule::clearAll()
{
    recording.store (false);
    takePending.store (false);
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        source.reset();
        info.reset();
        core.reset();
        view.reset();
        errorText = {};
    }
    sourceGen.fetch_add (1);
    coreGen.fetch_add (1);
    irReady.store (false);
    notify();
}

void IRForgeModule::loadSourceData (std::shared_ptr<juce::MemoryBlock> fileData, const juce::String& name)
{
    if (fileData == nullptr || fileData->getSize() == 0)
        return;
    {
        const juce::ScopedLock sl (loadLock);
        pendingLoad = std::move (fileData);
        pendingName = name;
    }
    loading.store (true);
    notify();
}

void IRForgeModule::openSourceChooser()
{
    sourceChooser = std::make_unique<juce::FileChooser> (
        "Load a sound to forge", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    juce::WeakReference<SynthModule> weakThis (this);
    sourceChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weakThis] (const juce::FileChooser& fc)
        {
            auto* self = dynamic_cast<IRForgeModule*> (weakThis.get());
            if (self == nullptr)
                return;

            auto data = std::make_shared<juce::MemoryBlock>();
            juce::String name;
           #if JUCE_ANDROID
            // a content:// document has no path: read it as a stream
            const auto url = fc.getURLResult();
            if (url.isEmpty())
                return;
            auto doc = juce::AndroidDocument::fromDocument (url);
            if (! doc.hasValue())
                return;
            if (auto in = doc.createInputStream())
                in->readIntoMemoryBlock (*data, (int) kMaxFileBytes);
            name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
           #else
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;
            if ((size_t) file.getSize() > kMaxFileBytes)
            {
                const juce::SpinLock::ScopedLockType sl (self->stateLock);
                self->errorText = "That file is too large";
                return;
            }
            file.loadFileAsData (*data);
            name = file.getFileNameWithoutExtension();
           #endif
            self->loadSourceData (std::move (data), name);
        });
}

//==============================================================================
std::shared_ptr<const IRForgeModule::SourceInfo> IRForgeModule::sourceInfo() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return info;
}

std::shared_ptr<const IRForgeModule::IRView> IRForgeModule::irView() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return view;
}

std::shared_ptr<const IRForgeModule::Source> IRForgeModule::currentSource() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return source;
}

juce::String IRForgeModule::lastError() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return errorText;
}

juce::Range<double> IRForgeModule::effectiveRange() const
{
    const auto inf = sourceInfo();
    if (inf == nullptr || inf->seconds <= 0.0)
        return {};
    const double total = inf->seconds;
    double s = juce::jlimit (0.0, 1.0, (double) getParameter ("rangeStart") * 0.01) * total;
    double e = juce::jlimit (0.0, 1.0, (double) getParameter ("rangeEnd") * 0.01) * total;
    if (e < s)
        std::swap (s, e);
    e = juce::jmin (e, s + kMaxRangeSeconds);
    if (e - s < 0.02)
    {
        e = juce::jmin (total, s + 0.02);
        s = juce::jmax (0.0, e - 0.02);
    }
    return { s, e };
}

//==============================================================================
// the patch keeps the core IR
//==============================================================================
bool IRForgeModule::hasAudioToSave() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return core != nullptr;
}

std::shared_ptr<const juce::AudioBuffer<float>> IRForgeModule::getAudioToSave (double& rate) const
{
    std::shared_ptr<const juce::AudioBuffer<float>> c;
    bool linear = false;
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        c = core;
        rate = coreRate;
        linear = coreLinear;
    }
    if (c == nullptr)
        return nullptr;

    // keep what the IR uses now (Length, plus a little room to lengthen it
    // again) rather than the whole forged core: a 400 ms IR is a small WAV
    const int want = (int) (getParameter ("length") * 0.001 * rate * 1.25) + 64;
    const int len = c->getNumSamples();
    if (want >= len)
        return c;
    auto trimmed = std::make_shared<juce::AudioBuffer<float>> (c->getNumChannels(), want);
    const int start = linear ? (len - want) / 2 : 0;
    for (int ch = 0; ch < c->getNumChannels(); ++ch)
        trimmed->copyFrom (ch, 0, *c, ch, start, want);
    return trimmed;
}

void IRForgeModule::restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
{
    if (audio == nullptr || audio->getNumSamples() < 16 || rate <= 0.0)
        return;
    // the Phase toggle is restored before the audio, and nothing re-forges
    // without a source, so it still says how this core was made
    publishCore (std::move (audio), rate, getParameter ("linear") > 0.5f);
    notify();
}

void IRForgeModule::publishCore (std::shared_ptr<juce::AudioBuffer<float>> newCore, double rate, bool linear)
{
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        core = std::move (newCore);
        coreRate = rate;
        coreLinear = linear;
    }
    coreGen.fetch_add (1);
}

//==============================================================================
// the build thread
//==============================================================================
void IRForgeModule::run()
{
    while (! threadShouldExit())
    {
        decodeLoadRequest();
        finishRecordingIfStopped();
        forgeIfNeeded();
        shapeIfNeeded();
        wait (50);
    }
}

void IRForgeModule::decodeLoadRequest()
{
    std::shared_ptr<juce::MemoryBlock> data;
    juce::String name;
    {
        const juce::ScopedLock sl (loadLock);
        data = std::move (pendingLoad);
        name = pendingName;
    }
    if (data == nullptr)
        return;

    auto reader = readerFor (*data);
    if (reader == nullptr || reader->lengthInSamples < 32 || reader->sampleRate <= 0.0)
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        errorText = "Can't read that file";
        loading.store (false);
        return;
    }

    // one pass over the whole file for the overview
    const auto total = reader->lengthInSamples;
    const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);
    auto inf = std::make_shared<SourceInfo>();
    inf->name = name;
    inf->seconds = (double) total / reader->sampleRate;
    inf->ovMin.assign (kOverviewPoints, 0.0f);
    inf->ovMax.assign (kOverviewPoints, 0.0f);
    juce::AudioBuffer<float> chunk (numCh, 1 << 16);
    for (juce::int64 pos = 0; pos < total && ! threadShouldExit(); pos += chunk.getNumSamples())
    {
        const int len = (int) juce::jmin ((juce::int64) chunk.getNumSamples(), total - pos);
        reader->read (&chunk, 0, len, pos, true, numCh > 1);
        overviewOf (chunk, len, pos, total, inf->ovMin, inf->ovMax);
    }

    auto src = std::make_shared<Source>();
    src->encoded = std::move (data);
    src->rate = reader->sampleRate;
    src->length = total;
    src->numChannels = numCh;
    src->generation = sourceGen.load() + 1;

    // a fresh source starts with its first three minutes selected
    setParameter ("rangeStart", 0.0f);
    setParameter ("rangeEnd", (float) juce::jmin (100.0, 100.0 * kMaxRangeSeconds / juce::jmax (0.001, inf->seconds)));

    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        source = std::move (src);
        info = std::move (inf);
        errorText = {};
    }
    sourceGen.fetch_add (1);
    loading.store (false);
}

void IRForgeModule::finishRecordingIfStopped()
{
    if (! takePending.load (std::memory_order_acquire) || recording.load (std::memory_order_acquire))
        return;

    // the audio thread may still be finishing its last write: give it a moment
    juce::Thread::sleep (20);

    const int len = recPos.load (std::memory_order_acquire);
    if (len < 256)
    {
        takePending.store (false);
        return;
    }

    auto take = std::make_shared<juce::AudioBuffer<float>> (2, len);
    {
        const juce::ScopedLock sl (recLock);
        if (len > recBuffer.getNumSamples())
        {
            takePending.store (false);
            return;
        }
        for (int ch = 0; ch < 2; ++ch)
            take->copyFrom (ch, 0, recBuffer, ch, 0, len);
    }

    auto inf = std::make_shared<SourceInfo>();
    inf->name = "Recording";
    inf->seconds = (double) len / recRate;
    inf->ovMin.assign (kOverviewPoints, 0.0f);
    inf->ovMax.assign (kOverviewPoints, 0.0f);
    overviewOf (*take, len, 0, len, inf->ovMin, inf->ovMax);

    auto src = std::make_shared<Source>();
    src->pcm = take;
    src->rate = recRate;
    src->length = len;
    src->numChannels = 2;
    src->generation = sourceGen.load() + 1;

    setParameter ("rangeStart", 0.0f);
    setParameter ("rangeEnd", 100.0f);

    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        source = std::move (src);
        info = std::move (inf);
        errorText = {};
    }
    sourceGen.fetch_add (1);
    takePending.store (false);
}

void IRForgeModule::forgeIfNeeded()
{
    const auto src = currentSource();
    if (src == nullptr)
        return;

    const double er = engineRate.load();
    const auto range = effectiveRange();

    ForgeKey k;
    k.sourceGen = src->generation;
    k.start = juce::jlimit ((juce::int64) 0, src->length - 16, (juce::int64) (range.getStart() * src->rate));
    k.end = juce::jlimit (k.start + 16, src->length, (juce::int64) (range.getEnd() * src->rate));
    k.order = juce::jlimit (kMinFftOrder, kMaxFftOrder, kMinFftOrder + (int) std::lround (getParameter ("fft")));
    // the analysis must be long enough for the IR Length asked for
    const double lengthSamples = getParameter ("length") * 0.001 * er;
    while ((double) (1 << k.order) < lengthSamples && k.order < kMaxFftOrder)
        ++k.order;
    k.character = juce::jlimit (0.0f, 1.0f, getParameter ("character") * 0.01f);
    k.linear = getParameter ("linear") > 0.5f;
    k.rate = er;

    if (*lastForge == k)
        return;

    busy.store (true);
    const int n = 1 << k.order, half = n / 2;
    const int outCh = juce::jmin (2, src->numChannels);

    // --- averaged magnitude spectrum of the range (cached per range / size)
    const auto specKey = juce::String (k.sourceGen) + ":" + juce::String (k.start) + ":" + juce::String (k.end)
                       + ":" + juce::String (k.order) + ":" + juce::String (er);
    if (specKey != cachedSpectrumKey)
    {
        RangeReader rr (src->encoded, src->pcm);
        if (! rr.ok())
        {
            busy.store (false);
            return;
        }

        const auto cropLen = k.end - k.start;
        juce::dsp::FFT fft (k.order);
        std::vector<juce::dsp::Complex<float>> a ((size_t) n), b ((size_t) n);
        std::vector<double> power[2];
        for (int ch = 0; ch < outCh; ++ch)
            power[ch].assign ((size_t) half + 1, 0.0);

        std::vector<float> window;
        auto addFrame = [&] (const juce::AudioBuffer<float>& frame, int frameLen)
        {
            if ((int) window.size() != frameLen)
            {
                window.resize ((size_t) frameLen);
                for (int i = 0; i < frameLen; ++i)
                    window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i
                                                                 / (float) juce::jmax (1, frameLen - 1));
            }
            for (int ch = 0; ch < outCh; ++ch)
            {
                const auto* d = frame.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                    a[(size_t) i] = { i < frameLen ? d[i] * window[(size_t) i] : 0.0f, 0.0f };
                fft.perform (a.data(), b.data(), false);
                for (int i = 0; i <= half; ++i)
                    power[ch][(size_t) i] += (double) std::norm (b[(size_t) i]);
            }
        };

        juce::AudioBuffer<float> frame (outCh, n);
        frame.clear();
        int frames = 0;
        if (cropLen <= n)
        {
            // shorter than one frame: one Hann window over the whole range
            rr.read (frame, 0, (int) cropLen, k.start);
            addFrame (frame, (int) cropLen);
            frames = 1;
        }
        else
        {
            // Welch: Hann frames with half overlap, read strictly forwards
            const int hop = half;
            rr.read (frame, 0, n, k.start);
            juce::int64 pos = k.start + n;
            for (;;)
            {
                addFrame (frame, n);
                ++frames;
                if (pos + hop > k.end || threadShouldExit())
                    break;
                for (int ch = 0; ch < outCh; ++ch)
                    std::memmove (frame.getWritePointer (ch), frame.getReadPointer (ch, hop), sizeof (float) * (size_t) hop);
                rr.read (frame, hop, hop, pos);
                pos += hop;
            }
        }

        // onto this engine's frequency grid (the source may run at another rate)
        const double ratio = er / src->rate;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& pw = power[juce::jmin (ch, outCh - 1)];
            std::vector<float> srcLog ((size_t) half + 1);
            float floorLog = 1.0e9f;
            for (int i = 0; i <= half; ++i)
            {
                srcLog[(size_t) i] = 0.5f * std::log ((float) (pw[(size_t) i] / frames) + 1.0e-14f);
                floorLog = juce::jmin (floorLog, srcLog[(size_t) i]);
            }
            auto& dst = cachedLogMag[ch];
            dst.resize ((size_t) half + 1);
            for (int j = 0; j <= half; ++j)
            {
                const double p = (double) j * ratio;
                if (p >= (double) half)
                {
                    dst[(size_t) j] = floorLog;
                    continue;
                }
                const int i0 = (int) p;
                const float fr = (float) (p - i0);
                dst[(size_t) j] = srcLog[(size_t) i0] + fr * (srcLog[(size_t) juce::jmin (half, i0 + 1)] - srcLog[(size_t) i0]);
            }
        }
        cachedSpectrumKey = specKey;
    }

    if (threadShouldExit())
        return;

    // --- CHARACTER: the lifter, geometric over the quefrency range
    const int lifter = juce::jlimit (4, half, (int) std::round (4.0f * std::pow ((float) half / 4.0f, k.character)));
    const int coreLen = juce::jmin (n, (int) (kMaxCoreSeconds * er));
    auto newCore = std::make_shared<juce::AudioBuffer<float>> (outCh, coreLen);
    std::vector<float> imp;
    for (int ch = 0; ch < outCh; ++ch)
    {
        cepstralIR (cachedLogMag[ch], imp, k.order, lifter, k.linear);
        const int start = k.linear ? juce::jlimit (0, n - coreLen, half - coreLen / 2) : 0;
        newCore->copyFrom (ch, 0, imp.data() + start, coreLen);
    }
    const float peak = newCore->getMagnitude (0, coreLen);
    if (peak > 1.0e-12f)
        newCore->applyGain (0.99f / peak);

    // a Clear or a new source while forging makes this one stale
    if (sourceGen.load() == k.sourceGen)
    {
        publishCore (std::move (newCore), er, k.linear);
        *lastForge = k;
    }
    busy.store (false);
}

void IRForgeModule::shapeIfNeeded()
{
    std::shared_ptr<const juce::AudioBuffer<float>> c;
    double rate = 0.0;
    bool centred = false;
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        c = core;
        rate = coreRate;
        centred = coreLinear;
    }
    if (c == nullptr || rate <= 0.0 || ! convPrepared.load())
        return;

    ShapeKey k;
    k.coreGen = coreGen.load();
    k.length = getParameter ("length");
    k.decay = getParameter ("decay") * 0.01f;
    k.stretch = getParameter ("stretch");
    k.predelay = getParameter ("predelay");
    k.lowCut = getParameter ("lowCut");
    k.highCut = getParameter ("highCut");
    k.tilt = getParameter ("tilt");
    k.width = getParameter ("width") * 0.01f;
    k.reverse = getParameter ("reverse") > 0.5f;

    const bool requested = reshapeRequested.exchange (false);
    if (*lastShape == k && ! requested)
        return;

    busy.store (true);
    const int coreLen = c->getNumSamples();
    const int numCh = juce::jmin (2, c->getNumChannels());

    // --- Length: from the start (minimum phase) or around the centre (linear)
    const int irLen = juce::jlimit (16, coreLen, (int) (k.length * 0.001 * rate));
    juce::AudioBuffer<float> ir (numCh, irLen);
    const int start = centred ? juce::jlimit (0, coreLen - irLen, coreLen / 2 - irLen / 2) : 0;
    for (int ch = 0; ch < numCh; ++ch)
        ir.copyFrom (ch, 0, *c, ch, start, irLen);

    // --- Decay, and fades so the cut never clicks
    const int fadeLen = juce::jmin (irLen / 4, (int) (0.010 * rate));
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = ir.getWritePointer (ch);
        if (centred)
        {
            const int mid = irLen / 2;
            if (k.decay > 0.001f)
            {
                const float tau = (float) irLen / (1.0f + 24.0f * k.decay);
                for (int i = 0; i < irLen; ++i)
                    d[i] *= std::exp (-std::abs ((float) (i - mid)) / tau);
            }
            for (int i = 0; i < fadeLen; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeLen);
                d[i] *= w;
                d[irLen - 1 - i] *= w;
            }
        }
        else
        {
            if (k.decay > 0.001f)
            {
                const float tau = (float) irLen / (0.5f + 12.0f * k.decay);
                for (int i = 0; i < irLen; ++i)
                    d[i] *= std::exp (-(float) i / tau);
            }
            for (int i = 0; i < fadeLen; ++i)
                d[irLen - 1 - i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeLen);
        }
        if (k.reverse)
            std::reverse (d, d + irLen);
    }

    // --- Stretch (time, not pitch of the spectrum's shape)
    const int maxLen = (int) (kMaxFinalSeconds * rate);
    stretchInPlace (ir, juce::jlimit (0.25f, 4.0f, k.stretch), maxLen);
    const int n = ir.getNumSamples();

    // --- Low / High Cut and Tilt on the IR itself
    auto runFilter = [&] (const juce::IIRCoefficients& coeffs)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            juce::IIRFilter f;
            f.setCoefficients (coeffs);
            f.processSamples (ir.getWritePointer (ch), n);
        }
    };
    const double nyq = rate * 0.45;
    if (k.lowCut > 21.0f)
    {
        const auto hp = juce::IIRCoefficients::makeHighPass (rate, juce::jmin ((double) k.lowCut, nyq), 0.707);
        runFilter (hp);
        runFilter (hp);   // 24 dB/oct: musical IRs carry a lot of low end
    }
    if (k.highCut < 19500.0f && k.highCut < nyq)
        runFilter (juce::IIRCoefficients::makeLowPass (rate, k.highCut, 0.707));
    if (std::abs (k.tilt) > 0.01f)
    {
        runFilter (juce::IIRCoefficients::makeLowShelf (rate, 250.0, 0.707, juce::Decibels::decibelsToGain (-k.tilt * 1.5f)));
        runFilter (juce::IIRCoefficients::makeHighShelf (rate, juce::jmin (4000.0, nyq), 0.707,
                                                         juce::Decibels::decibelsToGain (k.tilt * 1.5f)));
    }

    // --- Predelay and Width
    const int pre = juce::jlimit (0, (int) (0.25 * rate), (int) (k.predelay * 0.001 * rate));
    juce::AudioBuffer<float> fin (2, n + pre);
    fin.clear();
    for (int ch = 0; ch < 2; ++ch)
        fin.copyFrom (ch, pre, ir, juce::jmin (ch, numCh - 1), 0, n);
    if (std::abs (k.width - 1.0f) > 0.001f)
    {
        auto* L = fin.getWritePointer (0);
        auto* R = fin.getWritePointer (1);
        for (int i = 0; i < fin.getNumSamples(); ++i)
        {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * k.width;
            L[i] = m + s;
            R[i] = m - s;
        }
    }

    const float peak = fin.getMagnitude (0, fin.getNumSamples());
    if (peak > 1.0e-12f)
        fin.applyGain (0.99f / peak);

    // energy, for Gain Match: a dense musical IR otherwise changes the level a lot
    double energy = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* d = fin.getReadPointer (ch);
        for (int i = 0; i < fin.getNumSamples(); ++i)
            energy += (double) d[i] * d[i];
    }
    energy *= 0.5;
    const float newEnergyGain = energy > 1.0e-12 ? (float) (1.0 / std::sqrt (energy)) : 1.0f;

    // --- the card's picture: spectrum (log frequency) and envelope
    auto v = std::make_shared<IRView>();
    v->seconds = (double) fin.getNumSamples() / rate;
    {
        const int order = juce::jlimit (8, 16, (int) std::ceil (std::log2 ((double) juce::jmax (256, fin.getNumSamples()))));
        const int fn = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) fn * 2, 0.0f);
        const int len = juce::jmin (fn, fin.getNumSamples());
        for (int i = 0; i < len; ++i)
            buf[(size_t) i] = 0.5f * (fin.getSample (0, i) + fin.getSample (1, i));
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float top = -200.0f;
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            // a third-ish of an octave around each point, so the line reads as a shape
            const double f0 = 20.0 * std::pow (1000.0, (double) p / (kDisplayPoints - 1));
            const double f1 = f0 * std::pow (1000.0, 1.0 / (kDisplayPoints - 1));
            int b0 = (int) (f0 * fn / rate), b1 = juce::jmax (b0 + 1, (int) (f1 * fn / rate));
            b0 = juce::jlimit (1, fn / 2, b0);
            b1 = juce::jlimit (b0 + 1, fn / 2 + 1, b1);
            float m = 0.0f;
            for (int b = b0; b < b1; ++b)
                m = juce::jmax (m, buf[(size_t) b]);
            v->spectrumDb[(size_t) p] = juce::Decibels::gainToDecibels (m, -200.0f);
            top = juce::jmax (top, v->spectrumDb[(size_t) p]);
        }
        for (auto& db : v->spectrumDb)
            db -= top;
    }
    {
        const int total = fin.getNumSamples();
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            const int a = (int) ((juce::int64) p * total / kDisplayPoints);
            const int b = juce::jmax (a + 1, (int) ((juce::int64) (p + 1) * total / kDisplayPoints));
            float m = 0.0f;
            for (int i = a; i < b && i < total; ++i)
                m = juce::jmax (m, std::abs (fin.getSample (0, i)), std::abs (fin.getSample (1, i)));
            v->envelopeDb[(size_t) p] = juce::Decibels::gainToDecibels (m, -90.0f);
        }
    }

    // a Clear while shaping drops this one
    if (coreGen.load() == k.coreGen)
    {
        energyGain.store (newEnergyGain);
        installIR (std::move (fin), rate);
        const juce::SpinLock::ScopedLockType sl (stateLock);
        view = std::move (v);
    }
    *lastShape = k;
    busy.store (false);
}

void IRForgeModule::installIR (juce::AudioBuffer<float>&& ir, double rate)
{
    {
        const juce::ScopedLock sl (convLock);
        convolution.loadImpulseResponse (std::move (ir), rate,
                                         juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);
    }
    irReady.store (true);
}

//==============================================================================
// The card: REC / LOAD / CLEAR, the three toggles, the source with its range,
// and the forged IR.
//==============================================================================
class IRForgePanel : public juce::Component,
                     public ExtraContentParamListener,
                     private juce::Timer
{
public:
    explicit IRForgePanel (IRForgeModule& m) : module (&m)
    {
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = mod();
        if (m == nullptr)
            return;

        const auto well = juce::Colours::black.withAlpha (0.55f);
        const auto text = juce::Colours::white.withAlpha (0.82f);
        const auto dim = juce::Colours::white.withAlpha (0.45f);
        const auto cyan = juce::Colour (0xff4fd1e0);
        const auto amber = juce::Colour (0xfff0a830);

        // buttons
        for (int b = 0; b < kNumButtons; ++b)
        {
            auto r = buttonBounds (b);
            const bool lit = buttonLit (*m, b);
            const bool rec = b == 0 && m->isRecording();
            g.setColour (rec ? juce::Colour (0xffe0453a) : lit ? cyan.withAlpha (0.9f) : well);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (rec || lit ? juce::Colour (0xff10202a) : text);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (buttonLabel (*m, b), r, juce::Justification::centred, false);
        }

        // source
        auto sv = sourceBounds();
        g.setColour (well);
        g.fillRoundedRectangle (sv, 4.0f);
        const auto inf = m->sourceInfo();
        auto wave = sv.reduced (4.0f, 14.0f).withTrimmedBottom (-10.0f);
        if (inf != nullptr && ! inf->ovMin.empty())
        {
            const auto range = m->effectiveRange();
            const float x0 = wave.getX() + wave.getWidth() * (float) (range.getStart() / juce::jmax (0.001, inf->seconds));
            const float x1 = wave.getX() + wave.getWidth() * (float) (range.getEnd() / juce::jmax (0.001, inf->seconds));
            const int pts = (int) inf->ovMin.size();
            const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
            for (int i = 0; i < pts; ++i)
            {
                const float x = wave.getX() + wave.getWidth() * (float) i / (float) pts;
                const bool in = x >= x0 && x <= x1;
                g.setColour (in ? cyan.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.22f));
                const float top = mid - juce::jlimit (0.0f, 1.0f, inf->ovMax[(size_t) i]) * half;
                const float bot = mid - juce::jlimit (-1.0f, 0.0f, inf->ovMin[(size_t) i]) * half;
                g.fillRect (juce::Rectangle<float> (x, top, juce::jmax (1.0f, wave.getWidth() / (float) pts), juce::jmax (1.0f, bot - top)));
            }
            g.setColour (amber);
            g.fillRect (juce::Rectangle<float> (x0 - 0.5f, wave.getY(), 1.5f, wave.getHeight()));
            g.fillRect (juce::Rectangle<float> (x1 - 0.5f, wave.getY(), 1.5f, wave.getHeight()));
        }

        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        juce::String line;
        juce::Colour lineColour = text;
        if (m->isRecording())
        {
            line = "REC  " + clock (m->recordedSeconds()) + " / 3:00";
            if (! m->recInputConnected())
                line << "  -  Rec In is not patched";
            lineColour = juce::Colour (0xffff8a80);
        }
        else if (m->isLoading())
            line = "Reading the file...";
        else if (inf != nullptr)
        {
            const auto r = m->effectiveRange();
            line = inf->name + "   " + clock (r.getStart()) + " - " + clock (r.getEnd())
                 + "  (" + juce::String (r.getLength(), 1) + " s)";
        }
        else if (m->hasIR())
            line = "IR kept in the patch - REC or LOAD to re-forge";
        else
            line = "No source - REC from Rec In or LOAD a file";
        const auto err = m->lastError();
        if (err.isNotEmpty() && ! m->isRecording())
        {
            line = err;
            lineColour = juce::Colour (0xffff8a80);
        }
        g.setColour (lineColour);
        g.drawText (line, sv.reduced (6.0f, 2.0f).removeFromTop (12.0f), juce::Justification::centredLeft, true);

        // impulse response
        auto iv = irBounds();
        g.setColour (well);
        g.fillRoundedRectangle (iv, 4.0f);
        const auto v = m->irView();
        auto spec = iv.reduced (4.0f).withTrimmedTop (12.0f).withTrimmedBottom (16.0f);
        auto env = iv.reduced (4.0f).removeFromBottom (14.0f);
        if (v != nullptr && m->hasIR())
        {
            // octave grid
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            for (double f = 31.25; f < 20000.0; f *= 2.0)
            {
                const float x = spec.getX() + spec.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
                g.fillRect (juce::Rectangle<float> (x, spec.getY(), 1.0f, spec.getHeight()));
            }

            juce::Path p;
            for (int i = 0; i < IRForgeModule::kDisplayPoints; ++i)
            {
                const float x = spec.getX() + spec.getWidth() * (float) i / (float) (IRForgeModule::kDisplayPoints - 1);
                const float y = spec.getY() + spec.getHeight() * juce::jlimit (0.0f, 1.0f, -v->spectrumDb[(size_t) i] / 60.0f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            juce::Path fill (p);
            fill.lineTo (spec.getRight(), spec.getBottom());
            fill.lineTo (spec.getX(), spec.getBottom());
            fill.closeSubPath();
            g.setColour (amber.withAlpha (0.18f));
            g.fillPath (fill);
            g.setColour (amber);
            g.strokePath (p, juce::PathStrokeType (1.3f));

            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (env);
            const float w = env.getWidth() / (float) IRForgeModule::kDisplayPoints;
            g.setColour (cyan.withAlpha (0.85f));
            for (int i = 0; i < IRForgeModule::kDisplayPoints; ++i)
            {
                const float h = env.getHeight() * juce::jlimit (0.0f, 1.0f, (v->envelopeDb[(size_t) i] + 60.0f) / 60.0f);
                g.fillRect (juce::Rectangle<float> (env.getX() + w * (float) i, env.getBottom() - h, juce::jmax (1.0f, w - 0.3f), h));
            }
        }

        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        juce::String irLine = "IMPULSE RESPONSE";
        if (m->isBusy())
            irLine << "  -  forging...";
        else if (v != nullptr && m->hasIR())
            irLine << "  -  " << juce::String (v->seconds, 2) << " s";
        else
            irLine << "  -  none, Audio In passes dry";
        g.drawText (irLine, iv.reduced (6.0f, 2.0f).removeFromTop (11.0f), juce::Justification::centredLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        if (m == nullptr)
            return;
        for (int b = 0; b < kNumButtons; ++b)
        {
            if (! buttonBounds (b).contains (e.position))
                continue;
            switch (b)
            {
                case 0: m->toggleRecording(); break;
                case 1: m->openSourceChooser(); break;
                case 2: m->clearAll(); break;
                default:
                {
                    const auto id = toggleParam (b);
                    m->setParameter (id, m->getParameter (id) > 0.5f ? 0.0f : 1.0f);
                    break;
                }
            }
            repaint();
            return;
        }
    }

private:
    static constexpr int kNumButtons = 6, kButtonH = 22;

    IRForgeModule* mod() const { return dynamic_cast<IRForgeModule*> (module.get()); }

    static juce::String toggleParam (int b) { return b == 3 ? "linear" : b == 4 ? "reverse" : "match"; }

    static juce::String buttonLabel (const IRForgeModule& m, int b)
    {
        switch (b)
        {
            case 0:  return m.isRecording() ? "STOP" : "REC";
            case 1:  return "LOAD";
            case 2:  return "CLEAR";
            case 3:  return "LIN PH";
            case 4:  return "REVERSE";
            default: return "MATCH";
        }
    }

    static bool buttonLit (const IRForgeModule& m, int b)
    {
        return b >= 3 && m.getParameter (toggleParam (b)) > 0.5f;
    }

    juce::Rectangle<float> buttonBounds (int b) const
    {
        // REC LOAD CLEAR | LIN PH REVERSE MATCH, with a gap between the groups
        const float gap = 8.0f;
        const float w = ((float) getWidth() - gap) / (float) kNumButtons;
        const float x = (float) b * w + (b >= 3 ? gap : 0.0f);
        return { x, 0.0f, w - 4.0f, (float) kButtonH };
    }

    juce::Rectangle<float> sourceBounds() const { return { 0.0f, (float) kButtonH + 4.0f, (float) getWidth(), 66.0f }; }
    juce::Rectangle<float> irBounds() const
    {
        const float y = sourceBounds().getBottom() + 4.0f;
        return { 0.0f, y, (float) getWidth(), (float) getHeight() - y };
    }

    static juce::String clock (double secs)
    {
        secs = juce::jmax (0.0, secs);
        const int mins = (int) (secs / 60.0);
        const double rest = secs - mins * 60.0;
        return juce::String (mins) + ":" + (rest < 10.0 ? "0" : "") + juce::String (rest, 1);
    }

    void timerCallback() override
    {
        auto* m = mod();
        if (m == nullptr)
            return;
        // a new source resets the range: let the bars follow
        const int gen = m->sourceGeneration();
        if (gen != lastGen)
        {
            lastGen = gen;
            if (paramValuesChangedByContent)
                paramValuesChangedByContent();
        }
        repaint();
    }

    juce::WeakReference<aquanode::SynthModule> module;
    int lastGen { -1 };
};

std::unique_ptr<juce::Component> IRForgeModule::createExtraContentComponent()
{
    return std::make_unique<IRForgePanel> (*this);
}

//==============================================================================
static ModuleDescriptor irForgeDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.irforge";
    d.displayName = "IRForge";
    d.description =
        "Forges an impulse response from a sound and convolves Audio In with it. REC records Rec In (up to "
        "three minutes, STOP ends the take); LOAD takes a sound file instead and Range Start / End pick up to "
        "three minutes of it. Character is the heart: low keeps only the smooth tone of the source (a room, a "
        "cabinet), high keeps its harmonics, so noise through it sings the source's chord. The forged IR is "
        "saved with the patch - the source sound is not. Until something is forged, Audio In passes dry. "
        "64 samples of latency.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 63;
    d.sockets = {
        audioIn  ("recIn",    "Rec In"),
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };

    d.params = {
        makeRotary ("character", "Character", 0.0f, 100.0f, 75.0f, 0, "%", false, 1.0f).noMod(),
        makeRotary ("length",    "Length",    5.0f, 4000.0f, 400.0f, 0, "ms", true, 1.0f).noMod(),
        makeRotary ("decay",     "Decay",     0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f).noMod(),
        makeRotary ("mix",       "Mix",       0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
        makeRotary ("output",    "Output",    -24.0f, 24.0f, 0.0f, 0, "dB"),

        makeSteppedList ("fft", "FFT", { "4k", "8k", "16k", "32k", "64k", "128k", "256k", "512k" }, 6, 1).noMod(),
        makeRotary ("stretch",  "Stretch",  0.25f, 4.0f, 1.0f, 1, "x", true).noMod(),
        makeRotary ("predelay", "Predelay", 0.0f, 250.0f, 0.0f, 1, "ms").noMod(),
        makeRotary ("tilt",     "Tilt",     -6.0f, 6.0f, 0.0f, 1, "dB").noMod(),
        makeRotary ("width",    "Width",    0.0f, 200.0f, 100.0f, 1, "%", false, 1.0f).noMod(),

        makeRotary ("lowCut",  "Low Cut",  20.0f, 2000.0f, 30.0f, 2, "Hz", true, 1.0f).noMod(),
        makeRotary ("highCut", "High Cut", 500.0f, 20000.0f, 20000.0f, 2, "Hz", true, 1.0f).noMod(),

        makeHBar ("rangeStart", "Range Start", 0.0f, 100.0f, 0.0f, 3, "%").noMod(),
        makeHBar ("rangeEnd",   "Range End",   0.0f, 100.0f, 100.0f, 4, "%").noMod(),

        // toggles: the lit buttons in the card
        makeCombo ("linear",  "Phase",      { "Minimum Phase", "Linear Phase" }, 0, 5, 2).hide(),
        makeCombo ("reverse", "Reverse",    { "Forward", "Reverse" }, 0, 5, 2).hide(),
        makeCombo ("match",   "Gain Match", { "Raw Level", "Gain Match" }, 1, 5, 2).hide()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (IRForgeModule, irForgeDescriptor)
