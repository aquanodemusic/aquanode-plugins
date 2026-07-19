#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectralCompareAudioProcessor::SpectralCompareAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Main",     juce::AudioChannelSet::stereo(), true)
        .withInput ("Sidechain",juce::AudioChannelSet::stereo(), false)
        .withOutput("Output",   juce::AudioChannelSet::stereo(), true))
{
    fftMain      = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSidechain = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
}

SpectralCompareAudioProcessor::~SpectralCompareAudioProcessor() {}

//==============================================================================
const juce::String SpectralCompareAudioProcessor::getName() const { return JucePlugin_Name; }
bool SpectralCompareAudioProcessor::acceptsMidi()  const { return false; }
bool SpectralCompareAudioProcessor::producesMidi() const { return false; }
bool SpectralCompareAudioProcessor::isMidiEffect() const { return false; }
double SpectralCompareAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    SpectralCompareAudioProcessor::getNumPrograms() { return 1; }
int    SpectralCompareAudioProcessor::getCurrentProgram() { return 0; }
void   SpectralCompareAudioProcessor::setCurrentProgram(int) {}
const juce::String SpectralCompareAudioProcessor::getProgramName(int) { return {}; }
void   SpectralCompareAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
bool SpectralCompareAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main output must be stereo (or mono)
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() &&
        mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Main input must match main output
    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    // Sidechain input can be empty, mono or stereo
    auto sideIn = layouts.getChannelSet(true, 1);
    if (!sideIn.isDisabled() &&
        sideIn != juce::AudioChannelSet::mono() &&
        sideIn != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
void SpectralCompareAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void SpectralCompareAudioProcessor::allocateBuffers()
{
    mainFifo     .assign(fftSize, 0.0f);
    sidechainFifo.assign(fftSize, 0.0f);
    analysisFrame.assign(fftSize * 2, 0.0f);
    window       .resize(fftSize);

    smoothedMain     .assign(numBins, 0.0f);
    smoothedSidechain.assign(numBins, 0.0f);

    mainFifoIndex      = 0;
    sidechainFifoIndex = 0;

    createWindow();
}

//==============================================================================
void SpectralCompareAudioProcessor::setFFTSize(int newSize)
{
    if (newSize != 1024 && newSize != 2048 && newSize != 4096 && newSize != 8192)
        return;

    int newOrder = (newSize == 1024) ? 10
                 : (newSize == 2048) ? 11
                 : (newSize == 4096) ? 12 : 13;

    suspendProcessing(true);

    fftOrder = newOrder;
    fftSize  = newSize;
    hopSize  = fftSize / 4;
    numBins  = fftSize / 2 + 1;

    fftMain      = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSidechain = std::make_unique<juce::dsp::FFT>(fftOrder);

    allocateBuffers();

    suspendProcessing(false);
}

//==============================================================================
void SpectralCompareAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    allocateBuffers();
}

void SpectralCompareAudioProcessor::releaseResources() {}

//==============================================================================
// processBlock — audio passes through dry; we only read it for FFT analysis
//==============================================================================
void SpectralCompareAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int mainNumCh = getMainBusNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    // ---- Detect sidechain bus ----
    // Bus index 1 is the sidechain. getBusBuffer returns an empty buffer if not connected.
    auto sidechainBuffer = getBusBuffer(buffer, true, 1);
    const int sideNumCh  = sidechainBuffer.getNumChannels();

    // ---- Audio simply passes through (main bus) ----
    // (nothing to do; the buffer is already the output)

    // ---- Feed FIFOs and fire FFT hops ----
    for (int sample = 0; sample < numSamples; ++sample)
    {
        // ---- Main signal: average all input channels ----
        {
            float sum = 0.0f;
            for (int ch = 0; ch < mainNumCh; ++ch)
                sum += buffer.getSample(ch, sample);
            float mono = (mainNumCh > 0) ? sum / mainNumCh : 0.0f;

            if (mainFifoIndex < fftSize)
                mainFifo[mainFifoIndex] = mono;
            ++mainFifoIndex;
        }

        // ---- Sidechain signal ----
        {
            float sum = 0.0f;
            for (int ch = 0; ch < sideNumCh; ++ch)
                sum += sidechainBuffer.getSample(ch, sample);
            float mono = (sideNumCh > 0) ? sum / sideNumCh : 0.0f;

            if (sidechainFifoIndex < fftSize)
                sidechainFifo[sidechainFifoIndex] = mono;
            ++sidechainFifoIndex;
        }

        // ---- Fire a main FFT hop ----
        if (mainFifoIndex >= fftSize)
        {
            // Apply window and forward FFT
            for (int i = 0; i < fftSize; ++i)
                analysisFrame[i] = mainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftMain->performRealOnlyForwardTransform(analysisFrame.data(), true);

            {
                // Always update — freeze is handled visually in the editor
                juce::ScopedLock l(displayLock);
                const float norm   = 2.0f / (float)fftSize;   // normalise to 0 dBFS
                const float smooth = smoothMain.load(std::memory_order_relaxed);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    float re  = analysisFrame[bin * 2];
                    float im  = analysisFrame[bin * 2 + 1];
                    float mag = std::sqrt(re * re + im * im) * norm;
                    smoothedMain[bin] = smoothedMain[bin] * smooth + mag * (1.0f - smooth);
                }
            }

            // Shift FIFO by hopSize
            std::copy(mainFifo.begin() + hopSize,
                      mainFifo.begin() + fftSize,
                      mainFifo.begin());
            std::fill(mainFifo.begin() + (fftSize - hopSize),
                      mainFifo.begin() + fftSize, 0.0f);
            mainFifoIndex -= hopSize;
        }

        // ---- Fire a sidechain FFT hop ----
        if (sidechainFifoIndex >= fftSize)
        {
            for (int i = 0; i < fftSize; ++i)
                analysisFrame[i] = sidechainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftSidechain->performRealOnlyForwardTransform(analysisFrame.data(), true);

            {
                juce::ScopedLock l(displayLock);
                const float norm   = 2.0f / (float)fftSize;
                const float smooth = smoothSidechain.load(std::memory_order_relaxed);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    float re  = analysisFrame[bin * 2];
                    float im  = analysisFrame[bin * 2 + 1];
                    float mag = std::sqrt(re * re + im * im) * norm;
                    smoothedSidechain[bin] = smoothedSidechain[bin] * smooth + mag * (1.0f - smooth);
                }
            }

            std::copy(sidechainFifo.begin() + hopSize,
                      sidechainFifo.begin() + fftSize,
                      sidechainFifo.begin());
            std::fill(sidechainFifo.begin() + (fftSize - hopSize),
                      sidechainFifo.begin() + fftSize, 0.0f);
            sidechainFifoIndex -= hopSize;
        }
    }
}

//==============================================================================
// Display data accessors (call from message thread)
//==============================================================================
void SpectralCompareAudioProcessor::getMainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedMain.begin(), smoothedMain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void SpectralCompareAudioProcessor::getSidechainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedSidechain.begin(), smoothedSidechain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

//==============================================================================
// Colors
//==============================================================================
void SpectralCompareAudioProcessor::setBackgroundColor(juce::Colour c)        { backgroundColor = c; }
void SpectralCompareAudioProcessor::setGridColor(juce::Colour c)               { gridColor = c; }
void SpectralCompareAudioProcessor::setMainSpectrumColor(juce::Colour c)       { mainSpectrumColor = c; }
void SpectralCompareAudioProcessor::setSidechainSpectrumColor(juce::Colour c)  { sidechainSpectrumColor = c; }
void SpectralCompareAudioProcessor::setDeltaColor(juce::Colour c)              { deltaColor = c; }
void SpectralCompareAudioProcessor::setDiffColor(juce::Colour c)               { diffColor = c; }
void SpectralCompareAudioProcessor::setSidebarColor(juce::Colour c)            { sidebarColor = c; }

void SpectralCompareAudioProcessor::resetColors()
{
    backgroundColor       = juce::Colour(0xffffffff);
    gridColor             = juce::Colour(0xff444444);
    mainSpectrumColor     = juce::Colour(0xff00ff00);
    sidechainSpectrumColor= juce::Colour(0xffff00ff);
    deltaColor            = juce::Colour(0xffffff00);
    diffColor             = juce::Colour(0xff00ccff);
    sidebarColor          = juce::Colour(0xff1a1a1a);
}

//==============================================================================
// State persistence
//==============================================================================
void SpectralCompareAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement root("SpectralCompareState");
    root.setAttribute("version", 1);
    root.setAttribute("fftSize",              fftSize);
    root.setAttribute("bgColor",              (int)backgroundColor.getARGB());
    root.setAttribute("gridColor",            (int)gridColor.getARGB());
    root.setAttribute("mainColor",            (int)mainSpectrumColor.getARGB());
    root.setAttribute("sidechainColor",       (int)sidechainSpectrumColor.getARGB());
    root.setAttribute("deltaColor",           (int)deltaColor.getARGB());
    root.setAttribute("diffColor",            (int)diffColor.getARGB());
    root.setAttribute("sidebarColor",         (int)sidebarColor.getARGB());
    copyXmlToBinary(root, destData);
}

void SpectralCompareAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (!xml->hasTagName("SpectralCompareState")) return;
        setFFTSize(xml->getIntAttribute("fftSize", 2048));
        backgroundColor       = juce::Colour((juce::uint32)xml->getIntAttribute("bgColor",       (int)0xffffffff));
        gridColor             = juce::Colour((juce::uint32)xml->getIntAttribute("gridColor",      (int)0xff444444));
        mainSpectrumColor     = juce::Colour((juce::uint32)xml->getIntAttribute("mainColor",      (int)0xff00ff00));
        sidechainSpectrumColor= juce::Colour((juce::uint32)xml->getIntAttribute("sidechainColor", (int)0xffff00ff));
        deltaColor            = juce::Colour((juce::uint32)xml->getIntAttribute("deltaColor",     (int)0xffffff00));
        diffColor             = juce::Colour((juce::uint32)xml->getIntAttribute("diffColor",      (int)0xff00ccff));
        sidebarColor          = juce::Colour((juce::uint32)xml->getIntAttribute("sidebarColor",   (int)0xff1a1a1a));
    }
}

//==============================================================================
bool SpectralCompareAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SpectralCompareAudioProcessor::createEditor()
{
    return new SpectralCompareAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralCompareAudioProcessor();
}
