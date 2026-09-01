#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

//==============================================================================
// FullFilter — a bank of bell (peaking) filters stacked on the harmonic
// series of a root frequency, essentially "EQing in the shape of a sawtooth".
//
// Polyphony: up to maxVoices (12) notes can sound at once.
//   - Voice 0 ("base voice") is always on: last-note-priority, and holds
//     its last frequency after all notes are released (there is always at
//     least one set of filters sounding). It only glides between notes
//     (per the Glide knob, linearly ramping every bell's frequency together
//     since they're all derived from the root) when Polyphony == 1; at any
//     higher Polyphony setting it snaps instantly, since overlapping notes
//     are handled by the extra voices instead.
//   - Voices 1..(maxVoices-1) ("extra voices") only activate once more than
//     one note is held at the same time, so a single note is never doubled
//     up. They snap straight to their pitch (no glide) and are shaped by a
//     per-voice ADSR envelope (Attack/Decay/Sustain/Release knobs). The
//     "ADSR" toggle switches between the full envelope and a simplified
//     mode where only Attack applies and the voice releases instantly over
//     a short fixed time on note-off. The Polyphony knob (1-maxVoices-1
//     extra slots) caps how many extra voices may be active at once
//     (Polyphony == 1 = purely monophonic, matching the old behaviour).
//
// Per-bell editing: each of the maxBells harmonic slots (shared by every
// voice — only root frequency differs per voice) can be individually:
//   - volume-multiplied (0x-2x, default 1x) via the volume bar editor
//   - frequency-multiplied (0.5x-2x, default 1x, relative to where the
//     harmonic naturally sits) via the frequency bar editor
//   - manually muted (used by the "square wave" button, which mutes every
//     2nd filter — i.e. the even harmonics — turning the sawtooth stack
//     into an odd-harmonics-only square-ish stack)
// Any bell whose *effective* frequency (after the multiplier) falls below
// 10 Hz or above 20 kHz is automatically muted, regardless of the above.
//==============================================================================
class FullFilterAudioProcessor : public juce::AudioProcessor,
    private juce::AudioProcessorValueTreeState::Listener
{
public:
    FullFilterAudioProcessor();
    ~FullFilterAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FullFilter"; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int maxBells = 128;
    static constexpr int maxVoices = 12;

    // Read by the editor's visualizers at UI frame rate. Written once per
    // audio block in updateFilters(). Gain is in dB; a bell that is muted
    // (manually, via the square-wave button, above Nyquist, or outside the
    // 10 Hz-20 kHz audible-range clamp) is reported at -100 dB so the UI
    // can skip/dim it. These mirror the BASE voice (voice 0) only — the
    // visualizers show "the note you're currently holding", not every
    // polyphonic voice.
    std::array<std::atomic<float>, maxBells> bellFrequencies{};
    std::array<std::atomic<float>, maxBells> bellGainsDb{};

    // How many of the maxBells filters are currently active (Amount knob).
    // Shared by every voice — only the root frequency differs per voice.
    std::atomic<int> activeBellCount{ 64 };

    //==========================================================================
    // Per-bell editing, called from the editor's bar-chart components (message
    // thread). Backed by plain atomics so the audio thread can read them
    // lock-free in computeVoiceBells(). Values are shared across every voice.
    void setBellVolumeMultiplier(int bellIndex, float multiplier); // clamped 0-2x
    float getBellVolumeMultiplier(int bellIndex) const;

    void setBellFrequencyMultiplier(int bellIndex, float multiplier); // clamped 0.5x-2x
    float getBellFrequencyMultiplier(int bellIndex) const;

    // Resets every bell's frequency multiplier back to 1x (no edit), used
    // by the editor's "Reset Freq" button.
    void resetAllBellFrequencies();

    bool isBellManuallyMuted(int bellIndex) const;

    // Mutes/unmutes every 2nd filter (index 1, 3, 5... i.e. harmonics
    // 2, 4, 6...), which is what turns the full harmonic (sawtooth-ish)
    // stack into an odd-harmonics-only (square-ish) stack.
    void setEvenHarmonicsMuted(bool shouldMute);
    bool getEvenHarmonicsMuted() const { return evenHarmonicsMuted.load(); }

    // Acoustic-object presets: reshape every bell's volume/frequency
    // multipliers at once to give the harmonic stack the approximate
    // character of a real struck/resonant object (idealized textbook
    // partial models, not a precise physical simulation). Overwrites
    // whatever per-bell volume/frequency edits are currently in place.
    enum class AcousticPreset { Beam, Bell, Plate, Vibraphone, Marimba };
    void applyAcousticPreset(AcousticPreset preset);

    //==========================================================================
    // Wavetable import: loads a single-channel (or channel-0-of-multichannel)
    // wavetable .wav file made of consecutive fixed-length frames — one full
    // cycle of the waveform per frame, wavetableFrameSize samples each (2048,
    // the standard single-cycle length used by e.g. Serum-style wavetables).
    // The frame count is whatever the file actually contains (fileLength /
    // 2048 — could be 1, 128, 255, 377, anything), determined at load time.
    //
    // On load, every frame is analyzed once (FFT per frame; bin k of a
    // single-cycle frame directly IS harmonic k, so the first maxBells bins
    // map straight onto the maxBells bells) and the per-frame harmonic
    // magnitudes are cached, normalised 0..1 (0 = silent, 1 = the frame's
    // strongest of the first maxBells harmonics). The "Position" knob is a
    // plain 0..1 APVTS float parameter — DAW-automatable — that scans across
    // that cache: 0 = first frame, 1 = last frame, and any value in between
    // linearly interpolates the two neighbouring frames' harmonic strengths.
    //
    // The "Mode" combo (also an APVTS parameter, so automatable/persisted
    // like everything else here) decides what that interpolated per-bell
    // strength drives:
    //   - Volume:           strength -> bell volume multiplier (0..2x, same
    //                       as before) - traces the wavetable's spectral
    //                       shape straight onto the bar-chart EQ.
    //   - Filter Position:  strength -> bell frequency multiplier (0.5x..2x,
    //                       log-centred on 1x) - louder harmonics in the
    //                       wavetable frame get detuned further from where
    //                       they'd naturally sit, so the *shape* of the
    //                       wavetable warps the harmonic series' tuning
    //                       instead of its loudness. Deliberately weirder;
    //                       this is the one likely to produce genuinely
    //                       unfamiliar timbres.
    //   - Both:             both of the above at once, from the same
    //                       per-bell strength value.
    // Whichever target(s) aren't selected are reset to neutral (1x) rather
    // than left stale from a previous mode, so switching modes always gives
    // a clean, predictable result.
    //
    // Because the FFTs all happen up front at load time, responding to
    // either the Position knob or the Mode combo (including host automation,
    // which can arrive on the audio thread) is just a cheap per-bell lerp
    // over the cache.
    static constexpr int wavetableFrameSize = 2048;

    // Called on the message thread (Wavetable Import button / FileChooser
    // callback). Loads the file, analyzes every frame it contains, and
    // (unless analyzeAfterLoad is false) applies the current Position knob
    // value immediately. Returns false (leaving any previously loaded
    // wavetable untouched) if the file can't be read as audio or is shorter
    // than one frame.
    // analyzeAfterLoad is false only when reloading on state-restore, since
    // in that case the bell volume multipliers restored just above already
    // reflect the analysis (plus any edits made after it) - re-applying the
    // Position knob would wipe those edits.
    bool loadWavetableFile(const juce::File& file, bool analyzeAfterLoad = true);

    // Re-interpolates the cached per-frame harmonic analysis at the given
    // 0..1 position and (per the current "wavetableMode" parameter) writes
    // the result into the bell volume and/or frequency multipliers. No-op
    // if no wavetable is loaded. Called whenever the "wavetablePosition" or
    // "wavetableMode" parameter changes, from whatever thread that happens
    // on (GUI or host automation) - see the apvts parameter listener set up
    // in the constructor.
    void analyzeWavetablePosition(float position01);

    bool hasWavetableLoaded() const { return wavetableFrameCount > 0; }
    int getWavetableFrameCount() const { return wavetableFrameCount; }
    juce::String getWavetableFileName() const { return wavetableFileName; }

private:
    // juce::AudioProcessorValueTreeState::Listener override: reacts to the
    // "wavetablePosition" parameter changing, from any source (GUI knob drag
    // or host automation) and on whatever thread that arrives on.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    // Recomputes one voice's bell coefficients from the shared knob values
    // (level/lowpass/q/amount) and the shared per-bell volume/frequency/mute
    // edits, at the given root frequency, and pushes them into that voice's
    // filter chains for every channel. voiceIndex == 0 additionally
    // publishes to bellFrequencies/bellGainsDb/activeBellCount for the UI.
    void computeVoiceBells(int voiceIndex, float rootFreq);

    // Recomputes coefficients for every active voice. Called once per block;
    // numSamples is the block size, needed to advance rootFreqSmoother by
    // the correct amount of real time even though it's only sampled once
    // per block rather than once per sample (see updateFilters()'s use of
    // SmoothedValue::skip() for why this matters for Glide).
    void updateFilters(int numSamples);

    // Monophonic base-voice MIDI, last-note-priority — unchanged from the
    // original design. A played note overwrites the Root parameter itself
    // (so the knob visibly follows, and the frequency is preserved after
    // note-off instead of springing back). Moving between notes while one
    // is already held glides over the Glide knob's time; a fresh note
    // played from silence, or a manual knob turn, snaps instantly.
    void handleMidiMessage(const juce::MidiMessage& message);
    void setRootFrequency(float freqHz, bool glide);

    // Extra-voice (polyphony) voice-stealing note assignment.
    void triggerExtraVoice(int note, float freqHz);
    void releaseExtraVoice(int note);

    // One polyphonic voice: its own filter chains (per channel), its own
    // root-frequency smoother, and (for extra voices) an amplitude envelope.
    // Voice 0 is the always-on base voice and never uses envelope/currentNote/
    // active — those fields are extra-voice-only.
    //
    // The envelope is a real ADSR (juce::ADSR). Its Attack/Decay/Sustain/
    // Release are driven by the Attack/Decay/Sustain/Release knobs whenever
    // the "ADSR" toggle is on; when the toggle is off, the envelope is still
    // used (simpler than branching the audio loop) but is fed a fixed
    // Decay=0/Sustain=1/Release=extraVoiceReleaseSeconds so it reproduces the
    // original simple attack-fade/fixed-release behaviour exactly, with only
    // Attack still following its knob.
    struct Voice
    {
        int currentNote = -1;   // extra voices only; -1 = not assigned to a note
        bool active = false;    // extra voices only; base voice is implicitly always active
        juce::uint32 age = 0;   // extra voices only; used for voice stealing, higher = more recent

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> freqSmoother;
        juce::ADSR envelope; // extra voices only

        std::array<Coeffs::Ptr, maxBells> coefficients;
    };

    std::array<Voice, maxVoices> voices;
    juce::uint32 voiceAgeCounter = 0;

    // Fallback release time used only when the ADSR toggle is off, so extra
    // voices still release quickly and click-free without the Release knob
    // being in play. When the toggle is on, the Release knob is used instead.
    static constexpr float extraVoiceReleaseSeconds = 0.03f;

    // Per-bell edits (shared by every voice). Defaults are "no edit":
    // volume 1x, frequency 1x, not manually muted.
    std::array<std::atomic<float>, maxBells> bellVolumeMult{};
    std::array<std::atomic<float>, maxBells> bellFreqMult{};
    std::array<std::atomic<bool>, maxBells> bellManualMute{};
    std::atomic<bool> evenHarmonicsMuted{ false };

    // [voice][channel][bell], always allocated to maxBells so changing the
    // Amount knob never resizes (and thus never allocates) on the audio thread.
    std::vector<std::vector<std::vector<Filter>>> bellFilters;

    double currentSampleRate = 44100.0;

    std::atomic<float>* rootParam = nullptr;
    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* lowpassParam = nullptr;
    std::atomic<float>* qParam = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* polyphonyParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* adsrEnabledParam = nullptr; // AudioParameterBool, read as 0.0f/1.0f

    juce::RangedAudioParameter* rootRangedParam = nullptr; // for Hz <-> normalised conversion

    std::atomic<float>* wavetableModeParam = nullptr; // AudioParameterChoice index, read as a float

    //==========================================================================
    // Wavetable import state. Loading/analysis (loadWavetableFile,
    // computeFrameHarmonics) only ever happens on the message thread (the
    // Wavetable Import button's FileChooser callback, or state restore).
    // wavetableFrameHarmonics is read from analyzeWavetablePosition(), which
    // can run on the audio thread (host automation of "wavetablePosition"),
    // but only ever as a read - by the time any parameter change can fire,
    // the cache for the currently-loaded file is already fully built.
    juce::AudioFormatManager wavetableFormatManager;
    std::vector<float> wavetableSamples;      // whole file, channel 0 only, concatenated frames
    int wavetableFrameCount = 0;              // wavetableSamples.size() / wavetableFrameSize
    juce::String wavetableFileName;
    juce::String wavetableFilePath;           // full path, persisted for best-effort reload

    // One entry per frame; each entry holds that frame's normalised (0..1)
    // harmonic strength for bells 0..maxBells-1 (bell n = harmonic n+1).
    // Rebuilt wholesale by loadWavetableFile(). What this 0..1 strength
    // actually drives (bell volume, bell frequency, or both) is decided at
    // apply-time by analyzeWavetablePosition() from the current
    // "wavetableMode" parameter - it isn't baked into the cache.
    std::vector<std::array<float, maxBells>> wavetableFrameHarmonics;

    juce::dsp::FFT wavetableFFT{ 11 };        // order 11 -> 2048-point, matches wavetableFrameSize
    std::vector<float> wavetableFFTScratch;   // sized 2 * wavetableFrameSize, reused per frame analyzed

    // Runs the FFT for a single frame and fills 'out' with its normalised
    // 0..1 per-bell harmonic strength (1 = that frame's strongest of the
    // first maxBells harmonics). Message-thread only (called from
    // loadWavetableFile()).
    void computeFrameHarmonics(int frameIndex, std::array<float, maxBells>& out);

    // Smoothly (linearly) interpolates the audible BASE-voice root frequency
    // toward the Root parameter's value. Glide (a non-instant ramp) is only
    // ever armed when Polyphony == 1 — see setRootFrequency(). At any other
    // polyphony setting the smoother is always snapped instantly, since
    // overlapping notes are handled by the extra voices instead.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rootFreqSmoother;

    // Tracks the (smoothed) summed envelope level across all voices each
    // sample, used to scale the mixed output down by ~1/sqrt(N) so that
    // playing more overlapping notes doesn't linearly increase loudness.
    // See processBlock() for the compensation math.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> voiceCountSmoother;

    // Tracks what we last wrote (or read back) from the Root parameter, so
    // updateFilters() can tell a genuine external change (manual knob turn,
    // host automation, preset load) apart from our own MIDI-driven writes.
    // setRootFrequency() always keeps this exactly in sync with the
    // parameter's actual post-write value, so no separate "ignore the next
    // read" flag is needed.
    float lastKnownRootParamValue = 110.0f;

    // Monophonic base-voice note stack: back() is the currently sounding note.
    // Reserved up front so noteOn/off never allocate on the audio thread.
    std::vector<int> heldNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullFilterAudioProcessor)
};