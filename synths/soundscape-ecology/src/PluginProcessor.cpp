/*
  ==============================================================================

    PluginProcessor.cpp
    Soundscape Ecology — implementation.

    Measured values from reference recordings are cited at the point of use so
    that anyone changing a constant knows what it was fitted to.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace soundeco;

//==============================================================================
// Habitat
//==============================================================================
void Habitat::prepare(double sampleRate, int)
{
    sr = sampleRate;
    // Mutually prime delay lengths, short: forest ambience is early-reflection
    // dense, not a long tail.
    const int base[kNumLines] = { 421, 587, 683, 811, 947, 1103, 1279, 1439 };
    for (int i = 0; i < kNumLines; ++i)
    {
        lengths[(size_t)i] = (int)(base[i] * sr / 44100.0);
        lines[(size_t)i].assign((size_t)juce::jmax(16, lengths[(size_t)i] * 3), 0.0f);
        writePos[(size_t)i] = 0;
        damp[(size_t)i].setCutoff(4000.0f, sr);
    }
    mixSmoothed.reset(sr, 0.05);
}

void Habitat::setParams(float size, float damping, float mix)
{
    feedback = 0.35f + 0.55f * juce::jlimit(0.0f, 1.0f, size);
    const float fc = 1200.0f + 8000.0f * (1.0f - juce::jlimit(0.0f, 1.0f, damping));
    for (auto& d : damp) d.setCutoff(fc, sr);
    mixAmount = juce::jlimit(0.0f, 1.0f, mix);
    mixSmoothed.setTargetValue(mixAmount);

    for (int i = 0; i < kNumLines; ++i)
    {
        const int base[kNumLines] = { 421, 587, 683, 811, 947, 1103, 1279, 1439 };
        const float scale = 0.45f + 1.35f * juce::jlimit(0.0f, 1.0f, size);
        lengths[(size_t)i] = juce::jlimit(8, (int)lines[(size_t)i].size() - 2,
            (int)(base[i] * scale * sr / 44100.0));
    }
}

void Habitat::reset()
{
    for (auto& l : lines) std::fill(l.begin(), l.end(), 0.0f);
    for (auto& d : damp)  d.reset();
}

void Habitat::process(juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer(0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;

    for (int s = 0; s < n; ++s)
    {
        const float inL = L[s], inR = R[s];
        const float in = 0.5f * (inL + inR);

        float taps[kNumLines];
        for (int i = 0; i < kNumLines; ++i)
        {
            auto& line = lines[(size_t)i];
            int readPos = writePos[(size_t)i] - lengths[(size_t)i];
            if (readPos < 0) readPos += (int)line.size();
            taps[i] = line[(size_t)readPos];
        }

        // Hadamard mixing, cheap and diffuse. The sign of each term is the
        // parity of the bitwise AND of the two indices (Walsh-Hadamard).
        auto parity = [](int v) noexcept
            {
                v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
                return v & 1;
            };

        float mixed[kNumLines];
        for (int i = 0; i < kNumLines; ++i)
        {
            float acc = 0.0f;
            for (int j = 0; j < kNumLines; ++j)
                acc += (parity(i & j) ? -taps[j] : taps[j]);
            mixed[i] = acc * (1.0f / std::sqrt((float)kNumLines));
        }

        float wetL = 0.0f, wetR = 0.0f;
        for (int i = 0; i < kNumLines; ++i)
        {
            auto& line = lines[(size_t)i];
            const float fb = damp[(size_t)i].lp(mixed[i]) * feedback;
            line[(size_t)writePos[(size_t)i]] = in + fb;
            if (++writePos[(size_t)i] >= (int)line.size()) writePos[(size_t)i] = 0;
            ((i & 1) ? wetR : wetL) += taps[i];
        }
        // Measured: the wet path (4 of 8 lines per channel, scaled 0.35) sits
        // ~4 dB quieter than dry on its own, and a plain linear crossfade
        // between two signals of different level dips even lower in the
        // middle -- turning Habitat Mix up made presets progressively
        // quieter, worst around 0.7-0.8, exactly as reported. Gain-match the
        // wet path first, then use an equal-power (not linear) crossfade,
        // which is the standard fix for a dry/wet blend not sagging in the
        // middle when the two sides are not perfectly correlated.
        wetL *= 0.35f * 1.65f; wetR *= 0.35f * 1.65f;

        const float m = mixSmoothed.getNextValue();
        const float wetGain = std::sin(m * juce::MathConstants<float>::halfPi);
        const float dryGain = std::cos(m * juce::MathConstants<float>::halfPi);
        L[s] = inL * dryGain + wetL * wetGain;
        R[s] = inR * dryGain + wetR * wetGain;
    }
}

//==============================================================================
// Parameter layout
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SoundscapeEcologyAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using P = juce::AudioParameterFloat;
    using PB = juce::AudioParameterBool;
    using PC = juce::AudioParameterChoice;
    using PI = juce::AudioParameterInt;

    const juce::String slotNames[kNumSlots] = { "Slot 1", "Slot 2", "Slot 3" };

    for (int s = 0; s < kNumSlots; ++s)
    {
        // Which class this slot holds. Defaults give the familiar
        // biophony / geophony / anthrophony layout, but any slot can hold any
        // class, so the model list is no longer fixed per slot.
        layout.add(std::make_unique<PC>(juce::ParameterID{ pid::phony(s), 1 },
            "Slot " + juce::String(s + 1) + " Class",
            juce::StringArray{ "Biophony", "Geophony",
                                "Anthrophony" }, s));

        // A plain index rather than a named choice, because the set of names
        // changes with the class.
        layout.add(std::make_unique<PI>(juce::ParameterID{ pid::model(s), 1 },
            "Slot " + juce::String(s + 1) + " Model",
            0, kMaxModelsPerClass - 1, 0));
        layout.add(std::make_unique<PB>(juce::ParameterID{ pid::enabled(s), 1 },
            slotNames[s] + " On", true));
        // Skewed rather than linear: a linear level slider reached -2.5 dB by
        // half travel, so almost the whole usable range was crammed into the
        // bottom quarter and it jumped from silent to loud. A 0.45 skew puts
        // half travel near -10 dB, giving the lower half a genuine
        // quiet-to-moderate range while the top still reaches the same
        // maximum.
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::level(s), 1 },
            slotNames[s] + " Level",
            juce::NormalisableRange<float>(0.0f, 1.5f, 0.001f, 0.45f), 0.8f));

        for (int k = 0; k < kNumModelKnobs; ++k)
            layout.add(std::make_unique<P>(juce::ParameterID{ pid::knob(s, k), 1 },
                slotNames[s] + " P" + juce::String(k + 1),
                juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

        // --- articulation: the rhythm layer -------------------------------
        // Kept separate from the physics because species identity in real
        // bioacoustics lives in the rhythm. Kanda (1960) found the songs of
        // five Japanese cicadas overlap heavily in spectrum and are told apart
        // by pulse pattern alone.
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::rate(s), 1 },
            slotNames[s] + " Rate",
            juce::NormalisableRange<float>(0.02f, 24.0f, 0.001f, 0.35f), 1.5f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::phrase(s), 1 },
            slotNames[s] + " Phrase",
            juce::NormalisableRange<float>(1.0f, 64.0f, 0.01f, 0.5f), 1.0f));
        // Species rhythm. Kanda 1960: Japanese cicada spectra overlap so
        // heavily that species are told apart by the pattern of pulse
        // amplitude alone. PHRASE sets how many syllables fall inside one
        // event; RHYTHM shapes them from shallow tremolo (abura-zemi's
        // "frying oil") to sharp decaying syllables (higurashi's kana-kana).
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::rhythm(s), 1 },
            slotNames[s] + " Rhythm",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::jitter(s), 1 },
            slotNames[s] + " Jitter",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::swing(s), 1 },
            slotNames[s] + " Swing",
            juce::NormalisableRange<float>(-0.5f, 0.5f, 0.001f), 0.0f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::attack(s), 1 },
            slotNames[s] + " Attack",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.08f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::decay(s), 1 },
            slotNames[s] + " Decay",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.5f), 0.35f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::duration(s), 1 },
            slotNames[s] + " Duration",
            juce::NormalisableRange<float>(0.02f, 4.0f, 0.001f, 0.5f), 0.5f));

        // --- the Field -----------------------------------------------------
        layout.add(std::make_unique<PI>(juce::ParameterID{ pid::population(s), 1 },
            slotNames[s] + " Population", 1, kMaxIndividuals, 10));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::spread(s), 1 },
            slotNames[s] + " Spread",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.6f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::distance(s), 1 },
            slotNames[s] + " Distance",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::diversity(s), 1 },
            slotNames[s] + " Diversity",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));
        // Synchrony: weakly-coupled oscillators. 0 = a wash, 1 = a machine.
        // The interesting region is around 0.3, where waves of synchrony form
        // and dissolve across the population.
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::synchrony(s), 1 },
            slotNames[s] + " Synchrony",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.3f));
        // Height above ground. Ecologically real — crickets and frogs at 0 m,
        // birds at 8 m, wind in the canopy at 15 — and it drives the
        // ground-reflection comb, which is a strong distance and height cue.
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::elevation(s), 1 },
            slotNames[s] + " Elevation",
            juce::NormalisableRange<float>(0.02f, 30.0f, 0.01f, 0.4f), 0.15f));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::parallax(s), 1 },
            slotNames[s] + " Parallax",
            juce::NormalisableRange<float>(0.05f, 1.0f, 0.001f), 0.5f));

        layout.add(std::make_unique<PB>(juce::ParameterID{ pid::drawPitchOn(s), 1 },
            slotNames[s] + " Draw Pitch", false));
        layout.add(std::make_unique<PB>(juce::ParameterID{ pid::drawAmpOn(s), 1 },
            slotNames[s] + " Draw Amp", false));
        layout.add(std::make_unique<P>(juce::ParameterID{ pid::drawDepth(s), 1 },
            slotNames[s] + " Draw Depth",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    }

    // --- global -----------------------------------------------------------
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::temperature, 1 }, "Temperature",
        juce::NormalisableRange<float>(-5.0f, 42.0f, 0.1f), 22.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::humidity, 1 }, "Humidity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::timeOfDay, 1 }, "Time of Day",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::niche, 1 }, "Niche",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    // Weather coupling. At 0 the slots are independent layers; at 1 a gust
    // raises the wind, moves the canopy and quietens the animals at once.
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::weather, 1 }, "Weather",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.45f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::habSize, 1 }, "Habitat Size",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::habDamping, 1 }, "Habitat Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.65f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::habMix, 1 }, "Habitat Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    // ---- MIDI ------------------------------------------------------------
    layout.add(std::make_unique<PC>(juce::ParameterID{ pid::midiMode, 1 }, "MIDI Mode",
        juce::StringArray{ "Free Run", "Gated", "Gated + Pitch" }, 1));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::midiAttack, 1 }, "Note Attack",
        juce::NormalisableRange<float>(0.001f, 8.0f, 0.001f, 0.35f), 0.35f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::midiRelease, 1 }, "Note Release",
        juce::NormalisableRange<float>(0.005f, 20.0f, 0.001f, 0.35f), 1.6f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::midiVelSens, 1 }, "Velocity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.4f));
    layout.add(std::make_unique<PI>(juce::ParameterID{ pid::midiRoot, 1 }, "Root Note", 0, 127, 60));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::midiPitch, 1 }, "Pitch Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    layout.add(std::make_unique<P>(juce::ParameterID{ pid::output, 1 }, "Output",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{ pid::width, 1 }, "Width",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));

    return layout;
}

//==============================================================================
// Factory presets
//
// Values are given in REAL units, not normalised, and converted on load. Only
// parameters that differ from the default are listed, so each preset reads as
// a description of what makes it distinctive.
//
// Where a preset has a night variant, Time of Day morphs between them across
// only the differing parameters.
//==============================================================================
const std::vector<FactoryPreset>& soundeco::getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets =
    {
        //----------------------------------------------------------------
        { "Cricket Field at Dusk",
          "01_cricket_field_night: 4600 Hz harp, Q 60, even teeth, 144 ms chirps",
          {
            { "s0_phony", 0 }, { "s0_model", 0 }, { "s0_elev", 0.060f }, { "s0_on", 1 }, { "s0_k0", 0.433f },
            { "s0_k1", 0.500f }, { "s0_k2", 0.050f }, { "s0_k3", 0.650f },
            { "s0_k4", 0.500f }, { "s0_k5", 0.333f }, { "s0_rate", 2.510f },
            { "s0_dur", 0.145f }, { "s0_phrase", 1.000f }, { "s0_rhythm", 0.000f },
            { "s0_jitter", 0.200f }, { "s0_attack", 0.030f }, { "s0_decay", 0.280f },
            { "s0_pop", 26 }, { "s0_spread", 0.750f }, { "s0_dist", 0.300f },
            { "s0_div", 0.400f }, { "s0_sync", 0.320f }, { "s0_para", 0.550f },
            { "s0_level", 1.000f }, { "s1_phony", 1 }, { "s1_model", 3 }, { "s1_elev", 10.000f }, { "s1_on", 1 },
            { "s1_k0", 0.140f }, { "s1_k1", 0.800f }, { "s1_k2", 0.090f },
            { "s1_k5", 0.600f }, { "s1_level", 0.260f }, { "s1_pop", 3 },
            { "s1_dur", 3.500f }, { "s1_rate", 0.300f }, { "s1_attack", 0.300f },
            { "s1_decay", 0.350f }, { "s2_on", 0 }, { "temperature", 24.000f },
            { "humidity", 0.620f }, { "timeofday", 0.300f }, { "weather", 0.650f }, { "habsize", 0.420f },
            { "habdamping", 0.720f }, { "habmix", 0.160f }
          },
          { { "temperature", 13.0f }, { "s0_rate", 1.2f }, { "s0_pop", 14 }, { "s0_sync", 0.45f }, { "s1_level", 0.15f }, { "habmix", 0.24f } } },
        { "Katydid Rasp",
          "02_tooth_jitter: irregular teeth, low mirror Q, broadband end",
          {
            { "s0_phony", 0 }, { "s0_model", 1 }, { "s0_elev", 0.500f }, { "s0_on", 1 }, { "s0_k0", 0.600f },
            { "s0_k1", 0.308f }, { "s0_k2", 0.920f }, { "s0_k3", 0.300f },
            { "s0_k4", 0.500f }, { "s0_k5", 0.500f }, { "s0_rate", 3.200f },
            { "s0_dur", 0.180f }, { "s0_phrase", 1.000f }, { "s0_jitter", 0.300f },
            { "s0_attack", 0.050f }, { "s0_decay", 0.300f }, { "s0_pop", 18 },
            { "s0_spread", 0.700f }, { "s0_dist", 0.300f }, { "s0_div", 0.500f },
            { "s0_sync", 0.150f }, { "s0_level", 1.000f }, { "s1_on", 0 },
            { "s2_on", 0 }, { "temperature", 27.000f }, { "timeofday", 0.300f },
            { "weather", 0.150f }, { "habsize", 0.450f }, { "habmix", 0.140f }
          },
          { } },
        { "Cicada Abura-zemi",
          "12_cicada_abura: 4300 Hz, Q 52, 5 ribs, 390 Hz muscle, frying-oil tremolo",
          {
            { "s0_phony", 0 }, { "s0_model", 2 }, { "s0_elev", 4.000f }, { "s0_on", 1 }, { "s0_k0", 0.224f },
            { "s0_k1", 0.200f }, { "s0_k2", 0.600f }, { "s0_k3", 0.533f },
            { "s0_k4", 0.250f }, { "s0_k5", 0.550f }, { "s0_k6", 0.000f },
            { "s0_k7", 0.340f }, { "s0_rate", 0.328f }, { "s0_dur", 2.800f },
            { "s0_phrase", 10.500f }, { "s0_rhythm", 0.000f }, { "s0_jitter", 0.450f },
            { "s0_attack", 0.050f }, { "s0_decay", 0.120f }, { "s0_pop", 20 },
            { "s0_spread", 0.850f }, { "s0_dist", 0.280f }, { "s0_div", 0.500f },
            { "s0_sync", 0.040f }, { "s0_level", 1.000f }, { "s1_on", 0 },
            { "s2_on", 0 }, { "temperature", 33.000f }, { "humidity", 0.800f },
            { "timeofday", 0.850f }, { "weather", 0.150f }, { "habsize", 0.550f }, { "habdamping", 0.680f },
            { "habmix", 0.180f }
          },
          { } },
        { "Cicada Minmin-zemi",
          "13_cicada_minmin: 2.5 kHz phase leaping to the 13.5 kHz band",
          {
            { "s0_phony", 0 }, { "s0_model", 2 }, { "s0_elev", 4.000f }, { "s0_on", 1 }, { "s0_k0", 0.084f },
            { "s0_k1", 0.527f }, { "s0_k2", 0.400f }, { "s0_k3", 0.333f },
            { "s0_k4", 0.375f }, { "s0_k5", 0.500f }, { "s0_k6", 0.850f },
            { "s0_k7", 0.120f }, { "s0_rate", 0.280f }, { "s0_dur", 3.400f },
            { "s0_phrase", 12.000f }, { "s0_rhythm", 0.200f }, { "s0_swing", 0.300f },
            { "s0_jitter", 0.300f },
            { "s0_attack", 0.080f }, { "s0_decay", 0.200f }, { "s0_pop", 6 },
            { "s0_spread", 0.550f }, { "s0_dist", 0.220f }, { "s0_div", 0.280f },
            { "s0_sync", 0.060f }, { "s0_level", 1.000f }, { "s1_on", 0 },
            { "s2_on", 0 }, { "temperature", 31.000f }, { "humidity", 0.750f },
            { "timeofday", 0.800f }, { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.660f },
            { "habmix", 0.160f }
          },
          { } },
        { "Cicada Higurashi",
          "12_cicada_higurashi: 9 decaying kana-kana syllables, Q 95",
          {
            { "s0_phony", 0 }, { "s0_model", 2 }, { "s0_elev", 4.000f }, { "s0_on", 1 }, { "s0_k0", 0.272f },
            { "s0_k1", 0.591f }, { "s0_k2", 0.200f }, { "s0_k3", 0.422f },
            { "s0_k4", 0.150f }, { "s0_k5", 0.500f }, { "s0_k6", 0.220f },
            { "s0_k7", 0.160f }, { "s0_rate", 0.300f }, { "s0_dur", 3.000f },
            { "s0_phrase", 9.000f }, { "s0_rhythm", 1.000f }, { "s0_jitter", 0.250f },
            { "s0_attack", 0.040f }, { "s0_decay", 0.350f }, { "s0_pop", 8 },
            { "s0_spread", 0.700f }, { "s0_dist", 0.350f }, { "s0_div", 0.350f },
            { "s0_sync", 0.050f }, { "s0_level", 1.000f }, { "s1_on", 0 },
            { "s2_on", 0 }, { "temperature", 26.000f }, { "humidity", 0.800f },
            { "timeofday", 0.250f }, { "weather", 0.150f }, { "habsize", 0.600f }, { "habdamping", 0.700f },
            { "habmix", 0.220f }
          },
          { } },
        { "Rain on Leaves",
          "04_rain_on_leaves: exact port. Click plus entrained Minnaert bubble",
          {
            { "s1_phony", 1 }, { "s1_model", 5 }, { "s1_elev", 6.000f }, { "s1_on", 1 }, { "s1_k0", 0.350f },
            { "s1_k1", 0.500f }, { "s1_k2", 0.500f }, { "s1_k3", 1.000f },
            { "s1_k4", 0.050f }, { "s1_k5", 0.280f }, { "s1_k6", 0.300f },
            { "s1_k7", 0.500f }, { "s1_rate", 0.500f }, { "s1_dur", 2.000f },
            { "s1_attack", 0.100f }, { "s1_decay", 0.200f }, { "s1_pop", 8 },
            { "s1_spread", 0.700f }, { "s1_dist", 0.200f }, { "s1_div", 0.550f },
            { "s1_sync", 0.000f }, { "s1_level", 1.000f }, { "s0_on", 0 },
            { "s2_on", 0 }, { "humidity", 0.950f }, { "temperature", 13.000f },
            { "weather", 0.150f }, { "habsize", 0.300f }, { "habdamping", 0.800f }, { "habmix", 0.130f }
          },
          { } },
        { "Atlantic Surf",
          "05_surf_minnaert_bubbles: exact port. Bubbles gated by a beating swell",
          {
            { "s1_phony", 1 }, { "s1_model", 6 }, { "s1_elev", 0.300f }, { "s1_on", 1 }, { "s1_k0", 0.300f },
            { "s1_k1", 0.500f }, { "s1_k2", 0.500f }, { "s1_k3", 0.850f },
            { "s1_k4", 0.350f }, { "s1_k5", 0.300f }, { "s1_k6", 0.500f },
            { "s1_k7", 0.500f }, { "s1_rate", 0.300f }, { "s1_dur", 3.000f },
            { "s1_attack", 0.200f }, { "s1_decay", 0.300f }, { "s1_pop", 8 },
            { "s1_spread", 0.900f }, { "s1_dist", 0.450f }, { "s1_div", 0.600f },
            { "s1_sync", 0.180f }, { "s1_level", 1.000f }, { "s0_on", 0 },
            { "s2_on", 0 }, { "humidity", 0.900f }, { "weather", 0.150f }, { "habsize", 0.620f },
            { "habdamping", 0.550f }, { "habmix", 0.110f }
          },
          { } },
        { "Pine Wind",
          "06_wind_aeolian: vortex shedding from branch-diameter obstacles",
          {
            { "s1_phony", 1 }, { "s1_model", 3 }, { "s1_elev", 10.000f }, { "s1_on", 1 }, { "s1_k0", 0.300f },
            { "s1_k1", 0.850f }, { "s1_k2", 0.220f }, { "s1_k3", 0.550f },
            { "s1_k4", 0.450f }, { "s1_k5", 0.700f }, { "s1_k6", 0.300f },
            { "s1_k7", 0.350f }, { "s1_rate", 0.300f }, { "s1_dur", 3.500f },
            { "s1_jitter", 0.450f }, { "s1_attack", 0.300f }, { "s1_decay", 0.350f },
            { "s1_pop", 5 }, { "s1_spread", 0.850f }, { "s1_dist", 0.350f },
            { "s1_div", 0.600f }, { "s1_level", 1.000f }, { "s0_on", 0 },
            { "s2_on", 0 }, { "weather", 0.150f }, { "habsize", 0.550f }, { "habdamping", 0.700f },
            { "habmix", 0.150f }
          },
          { } },
        { "Campfire",
          "Crackle over combustion rumble, with occasional spit",
          {
            { "s1_phony", 1 }, { "s1_model", 4 }, { "s1_elev", 0.300f }, { "s1_on", 1 }, { "s1_k0", 0.450f },
            { "s1_k1", 0.700f }, { "s1_k2", 0.400f }, { "s1_k3", 0.350f },
            { "s1_k4", 0.300f }, { "s1_k5", 0.500f }, { "s1_k6", 0.350f },
            { "s1_k7", 0.550f }, { "s1_rate", 0.700f }, { "s1_dur", 2.000f },
            { "s1_jitter", 0.500f }, { "s1_attack", 0.150f }, { "s1_decay", 0.250f },
            { "s1_pop", 4 }, { "s1_spread", 0.250f }, { "s1_dist", 0.080f },
            { "s1_div", 0.450f }, { "s1_level", 1.000f }, { "s0_on", 0 },
            { "s2_on", 0 }, { "temperature", 8.000f }, { "timeofday", 0.050f },
            { "weather", 0.150f }, { "habsize", 0.350f }, { "habdamping", 0.750f }, { "habmix", 0.110f }
          },
          { } },
        { "Steel Bars Ambient",
          "07_steel_bars: free-free bar modes 1:2.756:5.404, pentatonic, long tails",
          {
            { "s2_phony", 2 }, { "s2_model", 3 }, { "s2_elev", 1.200f }, { "s2_on", 1 }, { "s2_k0", 0.200f },
            { "s2_k1", 0.350f }, { "s2_k2", 0.400f }, { "s2_k3", 0.550f },
            { "s2_k4", 0.300f }, { "s2_k5", 0.620f }, { "s2_k6", 0.400f },
            { "s2_k7", 0.400f }, { "s2_rate", 0.450f }, { "s2_dur", 3.600f },
            { "s2_phrase", 1.000f }, { "s2_jitter", 0.550f }, { "s2_attack", 0.005f },
            { "s2_decay", 0.550f }, { "s2_pop", 6 }, { "s2_spread", 0.700f },
            { "s2_dist", 0.280f }, { "s2_div", 0.600f }, { "s2_level", 1.000f },
            { "s0_on", 0 }, { "s1_on", 0 }, { "humidity", 0.400f },
            { "weather", 0.150f }, { "habsize", 0.950f }, { "habdamping", 0.400f }, { "habmix", 0.420f }
          },
          { } },
        { "Songbird Gestures",
          "08_gesture_phase: the (pressure, tension) loop. Sweep Gesture Phase",
          {
            { "s0_phony", 0 }, { "s0_model", 4 }, { "s0_elev", 6.000f }, { "s0_on", 1 }, { "s0_k0", 0.480f },
            { "s0_k1", 0.420f }, { "s0_k2", 0.250f }, { "s0_k3", 0.300f },
            { "s0_k4", 0.200f }, { "s0_k5", 0.250f }, { "s0_k6", 0.000f },
            { "s0_k7", 0.150f }, { "s0_rate", 1.600f }, { "s0_dur", 0.340f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.450f }, { "s0_attack", 0.060f },
            { "s0_decay", 0.200f }, { "s0_pop", 3 }, { "s0_spread", 0.400f },
            { "s0_dist", 0.200f }, { "s0_div", 0.250f }, { "s0_sync", 0.000f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 14.000f }, { "timeofday", 0.450f }, { "weather", 0.150f }, { "habsize", 0.400f },
            { "habdamping", 0.700f }, { "habmix", 0.140f }
          },
          { } },
        { "Biphonation",
          "09_biphonation: two syringeal sources interacting nonlinearly",
          {
            { "s0_phony", 0 }, { "s0_model", 4 }, { "s0_elev", 6.000f }, { "s0_on", 1 }, { "s0_k0", 0.520f },
            { "s0_k1", 0.330f }, { "s0_k2", 0.170f }, { "s0_k3", 0.280f },
            { "s0_k4", 0.250f }, { "s0_k5", 0.260f }, { "s0_k6", 0.700f },
            { "s0_k7", 0.150f }, { "s0_rate", 1.650f }, { "s0_dur", 0.300f },
            { "s0_jitter", 0.400f }, { "s0_attack", 0.060f }, { "s0_decay", 0.200f },
            { "s0_pop", 2 }, { "s0_spread", 0.300f }, { "s0_dist", 0.180f },
            { "s0_div", 0.200f }, { "s0_level", 1.000f }, { "s1_on", 0 },
            { "s2_on", 0 }, { "timeofday", 0.450f }, { "weather", 0.150f }, { "habsize", 0.400f },
            { "habdamping", 0.700f }, { "habmix", 0.140f }
          },
          { } },
        { "Tawny Owl",
          "15_owl_v2: F0 478 Hz, 6 Hz tremulant at 18%, 2nd harmonic 34 dB down",
          {
            { "s0_phony", 0 }, { "s0_model", 5 }, { "s0_elev", 8.000f }, { "s0_on", 1 }, { "s0_k0", 0.369f },
            { "s0_k1", 0.455f }, { "s0_k2", 0.720f }, { "s0_k3", 0.100f },
            { "s0_k4", 0.367f }, { "s0_k5", 0.550f }, { "s0_k6", 0.500f },
            { "s0_k7", 0.100f }, { "s0_rate", 0.182f }, { "s0_dur", 1.380f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.300f }, { "s0_attack", 0.550f },
            { "s0_decay", 0.300f }, { "s0_pop", 2 }, { "s0_spread", 0.300f },
            { "s0_dist", 0.500f }, { "s0_div", 0.120f }, { "s0_sync", 0.000f },
            { "s0_level", 1.000f }, { "s1_phony", 1 }, { "s1_model", 3 }, { "s1_elev", 10.000f }, { "s1_on", 1 },
            { "s1_k0", 0.090f }, { "s1_k1", 0.850f }, { "s1_k2", 0.095f },
            { "s1_k5", 0.550f }, { "s1_level", 0.180f }, { "s1_pop", 2 },
            { "s1_dur", 3.800f }, { "s1_rate", 0.260f }, { "s1_attack", 0.300f },
            { "s1_decay", 0.350f }, { "s2_on", 0 }, { "temperature", 9.000f },
            { "humidity", 0.820f }, { "timeofday", 0.050f }, { "weather", 0.650f }, { "habsize", 0.900f },
            { "habdamping", 0.600f }, { "habmix", 0.300f }
          },
          { } },
        { "Frog Pond",
          "Helmholtz vocal sacs, strongly coupled. Watch the synchrony waves",
          {
            { "s0_phony", 0 }, { "s0_model", 3 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.280f },
            { "s0_k1", 0.350f }, { "s0_k2", 0.450f }, { "s0_k3", 0.500f },
            { "s0_k4", 0.400f }, { "s0_k5", 0.550f }, { "s0_rate", 1.600f },
            { "s0_dur", 0.350f }, { "s0_jitter", 0.280f }, { "s0_attack", 0.060f },
            { "s0_decay", 0.320f }, { "s0_pop", 22 }, { "s0_spread", 0.650f },
            { "s0_dist", 0.250f }, { "s0_div", 0.450f }, { "s0_sync", 0.550f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 20.000f }, { "humidity", 0.900f }, { "timeofday", 0.080f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.700f }, { "habmix", 0.200f }
          },
          { { "s0_sync", 0.2f }, { "s0_rate", 0.7f }, { "s0_pop", 8 } } },
        { "Distant Motorway",
          "The ground-reflection comb sweep, not the Doppler",
          {
            { "s2_phony", 2 }, { "s2_model", 0 }, { "s2_elev", 0.350f }, { "s2_on", 1 }, { "s2_k0", 0.400f },
            { "s2_k1", 0.550f }, { "s2_k2", 0.350f }, { "s2_k3", 0.500f },
            { "s2_k4", 0.450f }, { "s2_k5", 0.200f }, { "s2_k6", 0.620f },
            { "s2_k7", 0.400f }, { "s2_rate", 0.500f }, { "s2_dur", 2.400f },
            { "s2_jitter", 0.650f }, { "s2_attack", 0.250f }, { "s2_decay", 0.300f },
            { "s2_pop", 5 }, { "s2_spread", 0.800f }, { "s2_dist", 0.600f },
            { "s2_div", 0.550f }, { "s2_level", 1.000f }, { "s0_on", 0 },
            { "s1_on", 0 }, { "humidity", 0.550f }, { "weather", 0.150f }, { "habsize", 0.700f },
            { "habdamping", 0.720f }, { "habmix", 0.160f }
          },
          { } },
        { "Machine Room",
          "Strict periodicity, the thing nature almost never does",
          {
            { "s2_phony", 2 }, { "s2_model", 5 }, { "s2_elev", 1.000f }, { "s2_on", 1 }, { "s2_k0", 0.180f },
            { "s2_k1", 0.400f }, { "s2_k2", 0.400f }, { "s2_k3", 0.450f },
            { "s2_k4", 0.550f }, { "s2_k5", 0.220f }, { "s2_k6", 0.300f },
            { "s2_k7", 0.400f }, { "s2_rate", 0.600f }, { "s2_dur", 2.000f },
            { "s2_jitter", 0.050f }, { "s2_attack", 0.120f }, { "s2_decay", 0.180f },
            { "s2_pop", 3 }, { "s2_spread", 0.400f }, { "s2_dist", 0.300f },
            { "s2_div", 0.300f }, { "s2_level", 1.000f }, { "s0_on", 0 },
            { "s1_on", 0 }, { "weather", 0.150f }, { "habsize", 0.720f }, { "habdamping", 0.350f },
            { "habmix", 0.260f }
          },
          { } },
        { "Night Scene",
          "11_night_scene_full: crickets, an owl, and wind in the canopy",
          {
            { "s0_phony", 0 }, { "s0_model", 0 }, { "s0_elev", 0.060f }, { "s0_on", 1 }, { "s0_k0", 0.433f },
            { "s0_k1", 0.500f }, { "s0_k2", 0.080f }, { "s0_k3", 0.650f },
            { "s0_k4", 0.500f }, { "s0_k5", 0.333f }, { "s0_rate", 2.200f },
            { "s0_dur", 0.150f }, { "s0_jitter", 0.220f }, { "s0_attack", 0.030f },
            { "s0_decay", 0.280f }, { "s0_pop", 32 }, { "s0_spread", 0.850f },
            { "s0_sync", 0.380f }, { "s0_div", 0.450f }, { "s0_dist", 0.320f },
            { "s0_level", 0.900f }, { "s1_phony", 1 }, { "s1_model", 3 }, { "s1_elev", 10.000f }, { "s1_on", 1 },
            { "s1_k0", 0.120f }, { "s1_k1", 0.900f }, { "s1_k2", 0.090f },
            { "s1_k5", 0.600f }, { "s1_level", 0.220f }, { "s1_pop", 4 },
            { "s1_dur", 3.600f }, { "s1_rate", 0.280f }, { "s1_attack", 0.300f },
            { "s1_decay", 0.350f }, { "s2_on", 0 }, { "temperature", 21.000f },
            { "humidity", 0.700f }, { "timeofday", 0.050f }, { "weather", 0.650f }, { "habsize", 0.550f },
            { "habdamping", 0.700f }, { "habmix", 0.200f }
          },
          { } },
        { "Maritime Plate Shear",
          "17_maritime_plate_shear: 118 Hz steel, stick-slip friction",
          {
            { "s2_phony", 2 }, { "s2_model", 1 }, { "s2_elev", 1.500f }, { "s2_on", 1 },
            { "s2_k0", 0.242f }, { "s2_k1", 0.583f }, { "s2_k2", 0.220f },
            { "s2_k3", 0.650f }, { "s2_k4", 0.450f }, { "s2_k5", 0.700f },
            { "s2_k6", 0.800f }, { "s2_k7", 0.550f }, { "s2_rate", 0.400f },
            { "s2_dur", 2.500f }, { "s2_jitter", 0.600f }, { "s2_attack", 0.200f },
            { "s2_decay", 0.300f }, { "s2_pop", 4 }, { "s2_spread", 0.700f },
            { "s2_dist", 0.300f }, { "s2_div", 0.650f }, { "s2_level", 1.000f },
            { "s0_on", 0 }, { "s1_on", 0 }, { "weather", 0.150f },
            { "humidity", 0.800f }, { "habsize", 0.800f },
            { "habdamping", 0.550f }, { "habmix", 0.240f }
          },
          { } },

        { "Maritime Plate Impact",
          "18_maritime_plate_impact: struck plate, ragged per-mode decay",
          {
            { "s2_phony", 2 }, { "s2_model", 2 }, { "s2_elev", 1.500f }, { "s2_on", 1 },
            { "s2_k0", 0.265f }, { "s2_k1", 0.333f }, { "s2_k2", 0.700f },
            { "s2_k3", 0.300f }, { "s2_k4", 0.450f }, { "s2_k5", 0.680f },
            { "s2_k6", 0.780f }, { "s2_k7", 0.600f }, { "s2_rate", 0.350f },
            { "s2_dur", 3.000f }, { "s2_jitter", 0.550f }, { "s2_attack", 0.004f },
            { "s2_decay", 0.500f }, { "s2_pop", 3 }, { "s2_spread", 0.600f },
            { "s2_dist", 0.300f }, { "s2_div", 0.600f }, { "s2_level", 1.000f },
            { "s0_on", 0 }, { "s1_on", 0 }, { "weather", 0.150f },
            { "humidity", 0.750f }, { "habsize", 0.900f },
            { "habdamping", 0.450f }, { "habmix", 0.340f }
          },
          { } },

        { "Dawn Chorus",
          "All three phonies with the niche allocator on",
          {
            { "s0_phony", 0 }, { "s0_model", 4 }, { "s0_elev", 6.000f }, { "s0_on", 1 }, { "s0_k0", 0.450f },
            { "s0_k1", 0.550f }, { "s0_k2", 0.620f }, { "s0_k3", 0.300f },
            { "s0_k4", 0.300f }, { "s0_k5", 0.500f }, { "s0_k6", 0.350f },
            { "s0_k7", 0.200f }, { "s0_rate", 1.100f }, { "s0_dur", 0.320f },
            { "s0_jitter", 0.550f }, { "s0_attack", 0.100f }, { "s0_decay", 0.280f },
            { "s0_pop", 12 }, { "s0_spread", 0.800f }, { "s0_dist", 0.350f },
            { "s0_div", 0.600f }, { "s0_sync", 0.100f }, { "s0_level", 1.000f },
            { "s1_phony", 1 }, { "s1_model", 3 }, { "s1_elev", 10.000f }, { "s1_on", 1 }, { "s1_k0", 0.120f },
            { "s1_k1", 0.700f }, { "s1_k2", 0.250f }, { "s1_k5", 0.600f },
            { "s1_level", 0.240f }, { "s1_pop", 3 }, { "s1_dur", 3.500f },
            { "s1_rate", 0.300f }, { "s1_attack", 0.300f }, { "s1_decay", 0.350f },
            { "s2_phony", 2 }, { "s2_model", 0 }, { "s2_elev", 0.350f }, { "s2_on", 1 }, { "s2_k1", 0.200f },
            { "s2_k6", 0.850f }, { "s2_level", 0.140f }, { "s2_pop", 2 },
            { "s2_dur", 2.500f }, { "s2_rate", 0.300f }, { "s2_attack", 0.250f },
            { "s2_decay", 0.300f }, { "temperature", 11.000f }, { "humidity", 0.700f },
            { "timeofday", 0.350f }, { "niche", 0.750f }, { "weather", 0.650f }, { "habsize", 0.600f },
            { "habdamping", 0.660f }, { "habmix", 0.200f }
          },
          { { "s0_level", 0.12f }, { "s0_rate", 0.3f }, { "s0_pop", 3 }, { "s2_level", 0.3f }, { "niche", 0.2f }, { "temperature", 6.0f } } },
        { "Full Habitat",
          "Everything at once, the reference scene",
          {
            { "s0_phony", 0 }, { "s0_model", 0 }, { "s0_elev", 0.060f }, { "s0_on", 1 }, { "s0_k0", 0.433f },
            { "s0_k1", 0.500f }, { "s0_k2", 0.080f }, { "s0_k3", 0.650f },
            { "s0_k4", 0.500f }, { "s0_k5", 0.333f }, { "s0_rate", 2.200f },
            { "s0_dur", 0.150f }, { "s0_jitter", 0.220f }, { "s0_attack", 0.030f },
            { "s0_decay", 0.280f }, { "s0_pop", 22 }, { "s0_spread", 0.800f },
            { "s0_sync", 0.340f }, { "s0_div", 0.450f }, { "s0_dist", 0.300f },
            { "s0_level", 0.850f }, { "s1_phony", 1 }, { "s1_model", 3 }, { "s1_elev", 10.000f }, { "s1_on", 1 },
            { "s1_k0", 0.160f }, { "s1_k1", 0.800f }, { "s1_k2", 0.090f },
            { "s1_k5", 0.600f }, { "s1_level", 0.260f }, { "s1_pop", 4 },
            { "s1_dur", 3.500f }, { "s1_rate", 0.300f }, { "s1_attack", 0.300f },
            { "s1_decay", 0.350f }, { "s2_phony", 2 }, { "s2_model", 0 }, { "s2_elev", 0.350f }, { "s2_on", 1 },
            { "s2_k1", 0.250f }, { "s2_k6", 0.900f }, { "s2_level", 0.120f },
            { "s2_pop", 2 }, { "s2_dur", 2.600f }, { "s2_rate", 0.300f },
            { "s2_attack", 0.250f }, { "s2_decay", 0.300f }, { "temperature", 21.000f },
            { "humidity", 0.650f }, { "timeofday", 0.550f }, { "niche", 0.600f },
            { "weather", 0.650f }, { "habsize", 0.580f }, { "habdamping", 0.660f }, { "habmix", 0.190f }
          },
          { { "s0_rate", 1.3f }, { "s0_sync", 0.48f }, { "temperature", 13.0f }, { "s1_level", 0.15f }, { "s2_level", 0.06f }, { "habmix", 0.26f } } },
        { "Spring Peeper",
          "Pseudacris crucifer: a near-pure 2.9 kHz whistle with a slight rise",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.855f },
            { "s0_k1", 0.060f }, { "s0_k2", 0.444f }, { "s0_k3", 0.838f },
            { "s0_k4", 0.000f }, { "s0_k5", 0.180f }, { "s0_k6", 0.800f },
            { "s0_k7", 0.550f }, { "s0_rate", 1.400f }, { "s0_dur", 0.160f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.300f }, { "s0_attack", 0.180f },
            { "s0_decay", 0.450f }, { "s0_pop", 18 }, { "s0_spread", 0.750f },
            { "s0_dist", 0.280f }, { "s0_div", 0.350f }, { "s0_sync", 0.300f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 14.000f }, { "humidity", 0.900f }, { "timeofday", 0.100f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        { "Boreal Chorus Frog",
          "Pseudacris maculata: ~15 arytenoid pulses per second, a running comb",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.133f },
            { "s0_k1", 0.322f }, { "s0_k2", 0.395f }, { "s0_k3", 0.734f },
            { "s0_k4", 0.050f }, { "s0_k5", 0.550f }, { "s0_k6", 0.300f },
            { "s0_k7", 0.583f }, { "s0_rate", 0.700f }, { "s0_dur", 0.850f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.300f }, { "s0_attack", 0.020f },
            { "s0_decay", 0.100f }, { "s0_pop", 16 }, { "s0_spread", 0.700f },
            { "s0_dist", 0.250f }, { "s0_div", 0.400f }, { "s0_sync", 0.500f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 12.000f }, { "humidity", 0.900f }, { "timeofday", 0.120f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        { "Tungara Whine",
          "Engystomops pustulosus: fold rate sweeping steeply downward",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.248f },
            { "s0_k1", 0.060f }, { "s0_k2", 0.105f }, { "s0_k3", 0.719f },
            { "s0_k4", 0.050f }, { "s0_k5", 0.450f }, { "s0_k6", 0.450f },
            { "s0_k7", 0.042f }, { "s0_rate", 0.550f }, { "s0_dur", 0.350f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.300f }, { "s0_attack", 0.050f },
            { "s0_decay", 0.550f }, { "s0_pop", 9 }, { "s0_spread", 0.700f },
            { "s0_dist", 0.300f }, { "s0_div", 0.300f }, { "s0_sync", 0.200f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 27.000f }, { "humidity", 0.900f }, { "timeofday", 0.050f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        { "Tungara Chuck",
          "The fibrous mass loaded: dense, brief, harmonically packed",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.045f },
            { "s0_k1", 0.060f }, { "s0_k2", 0.363f }, { "s0_k3", 0.806f },
            { "s0_k4", 0.550f }, { "s0_k5", 0.950f }, { "s0_k6", 0.150f },
            { "s0_k7", 0.375f }, { "s0_rate", 0.900f }, { "s0_dur", 0.100f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.300f }, { "s0_attack", 0.030f },
            { "s0_decay", 0.500f }, { "s0_pop", 6 }, { "s0_spread", 0.600f },
            { "s0_dist", 0.280f }, { "s0_div", 0.300f }, { "s0_sync", 0.100f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 27.000f }, { "humidity", 0.900f }, { "timeofday", 0.050f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        { "Hyla microcephala",
          "Small head, dominant up at 6.1 kHz, 110 pulses per second",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.436f },
            { "s0_k1", 0.884f }, { "s0_k2", 0.955f }, { "s0_k3", 0.825f },
            { "s0_k4", 0.000f }, { "s0_k5", 0.500f }, { "s0_k6", 0.500f },
            { "s0_k7", 0.500f }, { "s0_rate", 2.200f }, { "s0_dur", 0.095f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.300f }, { "s0_attack", 0.060f },
            { "s0_decay", 0.350f }, { "s0_pop", 14 }, { "s0_spread", 0.800f },
            { "s0_dist", 0.300f }, { "s0_div", 0.450f }, { "s0_sync", 0.250f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 26.000f }, { "humidity", 0.900f }, { "timeofday", 0.080f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        //----------------------------------------------------------------
        { "Pond Frog (sustained)",
          "Fitted to reference: 2266 Hz near-pure carrier, 70 Hz full-depth AM",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.662f },
            { "s0_k1", 0.705f }, { "s0_k2", 0.341f }, { "s0_k3", 0.000f },
            { "s0_k4", 0.100f }, { "s0_k5", 0.380f }, { "s0_k6", 0.500f },
            { "s0_k7", 0.500f }, { "s0_rate", 0.850f }, { "s0_dur", 0.760f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.250f }, { "s0_attack", 0.050f },
            { "s0_decay", 0.280f }, { "s0_pop", 6 }, { "s0_spread", 0.650f },
            { "s0_dist", 0.250f }, { "s0_div", 0.300f }, { "s0_sync", 0.300f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 19.000f }, { "humidity", 0.900f }, { "timeofday", 0.080f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        { "Pond Frog (peeps)",
          "Fitted to reference: 38 ms peeps at 2533 Hz, ~1.6 calls per second",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.743f },
            { "s0_k1", 0.418f }, { "s0_k2", 0.384f }, { "s0_k3", 0.020f },
            { "s0_k4", 0.020f }, { "s0_k5", 0.300f }, { "s0_k6", 0.500f },
            { "s0_k7", 0.500f }, { "s0_rate", 1.590f }, { "s0_dur", 0.038f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.250f }, { "s0_attack", 0.120f },
            { "s0_decay", 0.550f }, { "s0_pop", 12 }, { "s0_spread", 0.700f },
            { "s0_dist", 0.220f }, { "s0_div", 0.350f }, { "s0_sync", 0.350f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 17.000f }, { "humidity", 0.900f }, { "timeofday", 0.080f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        { "Pond Frog Chorus",
          "Fitted to reference: continuous chorus, 2053 Hz, 51 Hz modulation",
          {
            { "s0_phony", 0 }, { "s0_model", 6 }, { "s0_elev", 0.080f }, { "s0_on", 1 }, { "s0_k0", 0.598f },
            { "s0_k1", 0.601f }, { "s0_k2", 0.307f }, { "s0_k3", 0.030f },
            { "s0_k4", 0.050f }, { "s0_k5", 0.350f }, { "s0_k6", 0.500f },
            { "s0_k7", 0.500f }, { "s0_rate", 0.620f }, { "s0_dur", 1.600f },
            { "s0_phrase", 1.000f }, { "s0_jitter", 0.250f }, { "s0_attack", 0.060f },
            { "s0_decay", 0.200f }, { "s0_pop", 20 }, { "s0_spread", 0.800f },
            { "s0_dist", 0.300f }, { "s0_div", 0.400f }, { "s0_sync", 0.450f },
            { "s0_level", 1.000f }, { "s1_on", 0 }, { "s2_on", 0 },
            { "temperature", 21.000f }, { "humidity", 0.900f }, { "timeofday", 0.080f },
            { "weather", 0.150f }, { "habsize", 0.500f }, { "habdamping", 0.720f }, { "habmix", 0.200f }
          },
          { } },
        { "Great Horned Owl",
          "Fitted to a recording: F0 340 Hz, 2nd harmonic -20 dB, 5.8 Hz tremulant",
          {
            { "s0_phony", 0 }, { "s0_model", 5 }, { "s0_elev", 8.000f }, { "s0_on", 1 }, { "s0_k0", 0.171f },
            { "s0_k1", 0.436f }, { "s0_k2", 0.400f }, { "s0_k3", 0.050f },
            { "s0_k4", 0.700f }, { "s0_k5", 0.560f }, { "s0_k6", 0.520f },
            { "s0_k7", 0.080f }, { "s0_rate", 0.900f }, { "s0_dur", 0.320f },
            { "s0_phrase", 1.000f }, { "s0_rhythm", 0.000f }, { "s0_swing", 0.000f },
            { "s0_jitter", 0.550f }, { "s0_attack", 0.050f }, { "s0_decay", 0.280f },
            { "s0_pop", 2 }, { "s0_spread", 0.350f }, { "s0_dist", 0.450f },
            { "s0_div", 0.150f }, { "s0_sync", 0.000f }, { "s0_level", 1.000f },
            { "s1_on", 0 }, { "s2_on", 0 }, { "temperature", 6.000f },
            { "humidity", 0.780f }, { "timeofday", 0.040f }, { "weather", 0.150f }, { "habsize", 0.880f },
            { "habdamping", 0.620f }, { "habmix", 0.300f }
          },
          { } },

        { "Japanese Cicada (field)",
          "Fitted to a recording: bands at 4188 and 15542 Hz, 366 Hz tymbal muscle",
          {
            { "s0_phony", 0 }, { "s0_model", 2 }, { "s0_elev", 4.000f }, { "s0_on", 1 }, { "s0_k0", 0.215f },
            { "s0_k1", 0.200f }, { "s0_k2", 0.600f }, { "s0_k3", 0.480f },
            { "s0_k4", 0.170f }, { "s0_k5", 0.550f }, { "s0_k6", 0.580f },
            { "s0_k7", 0.100f }, { "s0_rate", 0.500f }, { "s0_dur", 1.600f },
            { "s0_phrase", 10.500f }, { "s0_rhythm", 0.000f }, { "s0_jitter", 0.350f },
            { "s0_attack", 0.050f }, { "s0_decay", 0.120f }, { "s0_pop", 14 },
            { "s0_spread", 0.850f }, { "s0_dist", 0.250f }, { "s0_div", 0.500f },
            { "s0_sync", 0.040f }, { "s0_level", 1.000f }, { "s1_on", 0 },
            { "s2_on", 0 }, { "temperature", 30.000f }, { "humidity", 0.800f },
            { "timeofday", 0.850f }, { "weather", 0.150f }, { "habsize", 0.520f }, { "habdamping", 0.680f },
            { "habmix", 0.180f }
          },
          { } },

        { "Rain on a Tin Roof",
          "Each drop a small impulse into a thin inharmonic panel",
          {
            { "s1_phony", 1 }, { "s1_model", 7 }, { "s1_elev", 3.000f },
            { "s1_on", 1 }, { "s1_k0", 0.300f }, { "s1_k1", 0.500f },
            { "s1_k2", 0.450f }, { "s1_k3", 0.600f }, { "s1_k4", 0.350f },
            { "s1_k5", 0.300f }, { "s1_k6", 0.350f }, { "s1_k7", 0.850f },
            { "s1_rate", 0.500f }, { "s1_dur", 2.000f }, { "s1_attack", 0.100f },
            { "s1_decay", 0.200f }, { "s1_pop", 6 }, { "s1_spread", 0.450f },
            { "s1_dist", 0.100f }, { "s1_div", 0.500f }, { "s1_level", 1.000f },
            { "s0_on", 0 }, { "s2_on", 0 }, { "weather", 0.150f },
            { "humidity", 0.950f }, { "temperature", 12.000f },
            { "habsize", 0.380f }, { "habdamping", 0.700f }, { "habmix", 0.180f }
          },
          { } },

        { "Rain on a Tent",
          "A taut membrane instead of metal: near-harmonic modes, fast decay",
          {
            { "s1_phony", 1 }, { "s1_model", 7 }, { "s1_elev", 1.800f },
            { "s1_on", 1 }, { "s1_k0", 0.300f }, { "s1_k1", 0.500f },
            { "s1_k2", 0.150f }, { "s1_k3", 0.550f }, { "s1_k4", 0.100f },
            { "s1_k5", 0.340f }, { "s1_k6", 0.450f }, { "s1_k7", 0.050f },
            { "s1_rate", 0.500f }, { "s1_dur", 2.000f }, { "s1_attack", 0.100f },
            { "s1_decay", 0.200f }, { "s1_pop", 6 }, { "s1_spread", 0.300f },
            { "s1_dist", 0.060f }, { "s1_div", 0.450f }, { "s1_level", 1.000f },
            { "s0_on", 0 }, { "s2_on", 0 }, { "weather", 0.150f },
            { "humidity", 0.950f }, { "temperature", 11.000f },
            { "habsize", 0.220f }, { "habdamping", 0.820f }, { "habmix", 0.120f }
          },
          { } },

    };

    // Sorted alphabetically ONCE, on first call. The preset dropdown indexes
    // straight into this vector (loadByIndex, getAllNames, the NEXT/PREV
    // buttons all share the same index space), so sorting the display names
    // alone would silently desync loading from selection. Sorting the vector
    // itself keeps index order and alphabetical order identical everywhere.
    static const std::vector<FactoryPreset> sorted = []
        {
            std::vector<FactoryPreset> v = presets;
            std::sort(v.begin(), v.end(),
                [](const FactoryPreset& a, const FactoryPreset& b)
                {
                    return juce::String(a.name).compareIgnoreCase(juce::String(b.name)) < 0;
                });
            return v;
        }();
    return sorted;
}

//==============================================================================
// PresetManager
//==============================================================================
PresetManager::PresetManager(SoundscapeEcologyAudioProcessor& p) : processor(p)
{
    refreshUserPresets();
}

juce::File PresetManager::getPresetDirectory()
{
    // Documents rather than AppData: users expect to find, back up and share
    // their presets without hunting through hidden folders.
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("SoundscapeEcologyVST");
    if (!dir.exists()) dir.createDirectory();
    return dir;
}

int PresetManager::getNumFactory() const
{
    return (int)soundeco::getFactoryPresets().size();
}

void PresetManager::refreshUserPresets()
{
    userPresets.clear();
    auto dir = getPresetDirectory();
    // recurse so the Factory subfolder is picked up as well as loose user files
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, true, "*.xml"))
        if (!f.getParentDirectory().getFileName().equalsIgnoreCase("Factory"))
            userPresets.add(f.getFileNameWithoutExtension());
    userPresets.sort(true);
}

juce::StringArray PresetManager::getAllNames() const
{
    juce::StringArray names;
    for (const auto& p : soundeco::getFactoryPresets())
        names.add(p.name);
    names.addArray(userPresets);
    return names;
}

juce::String PresetManager::getCurrentName() const
{
    auto names = getAllNames();
    return juce::isPositiveAndBelow(currentIndex, names.size()) ? names[currentIndex]
        : juce::String("Init");
}

void PresetManager::resetToDefaults()
{
    for (auto* param : processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            rp->setValueNotifyingHost(rp->getDefaultValue());
}

void PresetManager::applyValues(const std::vector<soundeco::PresetValue>& values)
{
    for (const auto& v : values)
    {
        if (auto* rp = processor.apvts.getParameter(v.id))
            rp->setValueNotifyingHost(rp->getNormalisableRange().convertTo0to1(v.value));
    }
}

void PresetManager::captureState(std::map<juce::String, float>& dest) const
{
    dest.clear();
    for (auto* param : processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            dest[rp->getParameterID()] = rp->getNormalisableRange()
            .convertFrom0to1(rp->getValue());
}

void PresetManager::computeMorphKeys()
{
    morphKeys.clear();
    for (const auto& kv : dayState)
    {
        auto it = nightState.find(kv.first);
        if (it == nightState.end()) continue;
        if (kv.first == soundeco::pid::timeOfDay) continue;   // never morph the morph control
        if (std::abs(it->second - kv.second) > 1.0e-5f)
            morphKeys.add(kv.first);
    }
}

void PresetManager::loadByIndex(int index)
{
    auto names = getAllNames();
    if (!juce::isPositiveAndBelow(index, names.size())) return;
    currentIndex = index;

    const int numFactory = getNumFactory();

    if (index < numFactory)
    {
        const auto& preset = soundeco::getFactoryPresets()[(size_t)index];
        resetToDefaults();
        applyValues(preset.day);
        captureState(dayState);

        if (!preset.night.empty())
        {
            // capture the night variant by applying it on top, reading, then
            // restoring the day state
            applyValues(preset.night);
            captureState(nightState);
            for (const auto& kv : dayState)
                if (auto* rp = processor.apvts.getParameter(kv.first))
                    rp->setValueNotifyingHost(rp->getNormalisableRange().convertTo0to1(kv.second));
        }
        else
        {
            nightState = dayState;
        }
    }
    else
    {
        auto file = getPresetDirectory().getChildFile(names[index] + ".xml");
        if (auto xml = juce::XmlDocument::parse(file))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            auto curves = tree.getChildWithName("CURVES");
            if (curves.isValid())
            {
                for (int s = 0; s < soundeco::kNumSlots; ++s)
                {
                    auto slot = curves.getChildWithName("SLOT" + juce::String(s));
                    if (!slot.isValid()) continue;
                    auto parse = [](const juce::String& str,
                        std::array<float, soundeco::kDrawPoints>& dest)
                        {
                            auto tokens = juce::StringArray::fromTokens(str, " ", "");
                            for (int i = 0; i < juce::jmin(soundeco::kDrawPoints, tokens.size()); ++i)
                                dest[(size_t)i] = tokens[i].getFloatValue();
                        };
                    parse(slot.getProperty("pitch").toString(), processor.pitchCurves[(size_t)s]);
                    parse(slot.getProperty("amp").toString(), processor.ampCurves[(size_t)s]);
                }
                tree.removeChild(curves, nullptr);
            }
            processor.apvts.replaceState(tree);
            captureState(dayState);
            nightState = dayState;
        }
    }

    computeMorphKeys();
    lastMorph = -1.0f;
    processor.requestRetrigger();
    if (processor.onPresetChanged) processor.onPresetChanged();
}

void PresetManager::loadNext(int direction)
{
    auto names = getAllNames();
    if (names.isEmpty()) return;
    int next = (currentIndex + direction) % names.size();
    if (next < 0) next += names.size();
    loadByIndex(next);
}

void PresetManager::applyMorph(float t)
{
    if (morphKeys.isEmpty()) return;
    t = juce::jlimit(0.0f, 1.0f, t);
    if (std::abs(t - lastMorph) < 0.002f) return;
    lastMorph = t;

    // Time of Day 1 = day, 0 = night
    const float blend = 1.0f - t;

    for (const auto& key : morphKeys)
    {
        auto d = dayState.find(key);
        auto n = nightState.find(key);
        if (d == dayState.end() || n == nightState.end()) continue;
        const float value = d->second + (n->second - d->second) * blend;
        if (auto* rp = processor.apvts.getParameter(key))
            rp->setValueNotifyingHost(rp->getNormalisableRange().convertTo0to1(value));
    }
}

void PresetManager::saveUserPreset(const juce::String& name)
{
    if (name.isEmpty()) return;
    auto state = processor.apvts.copyState();

    juce::ValueTree curves("CURVES");
    for (int s = 0; s < soundeco::kNumSlots; ++s)
    {
        juce::ValueTree slot("SLOT" + juce::String(s));
        juce::String p, a;
        for (int i = 0; i < soundeco::kDrawPoints; ++i)
        {
            p += juce::String(processor.pitchCurves[(size_t)s][(size_t)i], 4) + " ";
            a += juce::String(processor.ampCurves[(size_t)s][(size_t)i], 4) + " ";
        }
        slot.setProperty("pitch", p.trim(), nullptr);
        slot.setProperty("amp", a.trim(), nullptr);
        curves.addChild(slot, -1, nullptr);
    }
    state.addChild(curves, -1, nullptr);

    if (auto xml = state.createXml())
        xml->writeTo(getPresetDirectory().getChildFile(name + ".xml"));

    refreshUserPresets();
    auto names = getAllNames();
    currentIndex = names.indexOf(name);
    if (processor.onPresetChanged) processor.onPresetChanged();
}

void PresetManager::installFactoryPresetsToDisk()
{
    auto dir = getPresetDirectory().getChildFile("Factory");
    if (!dir.exists()) dir.createDirectory();

    // capture whatever state we are in, so installing does not disturb it
    std::map<juce::String, float> restore;
    captureState(restore);

    const auto& bank = soundeco::getFactoryPresets();
    for (const auto& preset : bank)
    {
        auto file = dir.getChildFile(juce::String(preset.name) + ".xml");
        if (file.existsAsFile()) continue;          // never overwrite user edits

        resetToDefaults();
        applyValues(preset.day);

        auto state = processor.apvts.copyState();
        state.setProperty("description", preset.description, nullptr);

        if (!preset.night.empty())
        {
            // store the night variant as a child so the Time of Day morph
            // survives the round trip to disk
            applyValues(preset.night);
            juce::ValueTree night("NIGHT");
            std::map<juce::String, float> nightVals;
            captureState(nightVals);
            for (const auto& kv : nightVals)
                night.setProperty(kv.first, kv.second, nullptr);
            state.addChild(night, -1, nullptr);
        }

        if (auto xml = state.createXml())
            xml->writeTo(file);
    }

    // restore
    for (const auto& kv : restore)
        if (auto* rp = processor.apvts.getParameter(kv.first))
            rp->setValueNotifyingHost(rp->getNormalisableRange().convertTo0to1(kv.second));

    refreshUserPresets();
}

void PresetManager::loadFromFile(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.hasType(processor.apvts.state.getType())) return;

    // drawn contours
    auto curves = tree.getChildWithName("CURVES");
    if (curves.isValid())
    {
        for (int s = 0; s < soundeco::kNumSlots; ++s)
        {
            auto slot = curves.getChildWithName("SLOT" + juce::String(s));
            if (!slot.isValid()) continue;
            auto parse = [](const juce::String& str,
                std::array<float, soundeco::kDrawPoints>& dest)
                {
                    auto tokens = juce::StringArray::fromTokens(str, " ", "");
                    for (int i = 0; i < juce::jmin(soundeco::kDrawPoints, tokens.size()); ++i)
                        dest[(size_t)i] = tokens[i].getFloatValue();
                };
            parse(slot.getProperty("pitch").toString(), processor.pitchCurves[(size_t)s]);
            parse(slot.getProperty("amp").toString(), processor.ampCurves[(size_t)s]);
        }
        tree.removeChild(curves, nullptr);
    }

    // optional night variant for the Time of Day morph
    auto night = tree.getChildWithName("NIGHT");
    std::map<juce::String, float> loadedNight;
    if (night.isValid())
    {
        for (int i = 0; i < night.getNumProperties(); ++i)
        {
            auto name = night.getPropertyName(i);
            loadedNight[name.toString()] = (float)night.getProperty(name);
        }
        tree.removeChild(night, nullptr);
    }

    processor.apvts.replaceState(tree);

    captureState(dayState);
    nightState = loadedNight.empty() ? dayState : loadedNight;
    computeMorphKeys();
    lastMorph = -1.0f;

    // reflect in the browser if this file lives in the preset folder
    refreshUserPresets();
    auto names = getAllNames();
    const int idx = names.indexOf(file.getFileNameWithoutExtension());
    if (idx >= 0) currentIndex = idx;

    if (processor.onPresetChanged) processor.onPresetChanged();
}

void PresetManager::deleteUserPreset(const juce::String& name)
{
    getPresetDirectory().getChildFile(name + ".xml").deleteFile();
    refreshUserPresets();
    currentIndex = juce::jlimit(0, juce::jmax(0, getAllNames().size() - 1), currentIndex);
    if (processor.onPresetChanged) processor.onPresetChanged();
}

//==============================================================================
SoundscapeEcologyAudioProcessor::SoundscapeEcologyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "SOUNDSCAPE_ECOLOGY", createParameterLayout())
{
    for (int s = 0; s < kNumSlots; ++s)
        resetCurves(s);

    presets = std::make_unique<PresetManager>(*this);

    // Write the factory bank to disk on first run so the user gets a real,
    // manageable folder of .xml files rather than presets locked in the binary.
    presets->installFactoryPresetsToDisk();

    // Load a preset at startup. Without this the plugin boots on raw parameter
    // defaults, which sound nothing like any of the tuned presets.
    presets->loadByIndex(0);
}

SoundscapeEcologyAudioProcessor::~SoundscapeEcologyAudioProcessor() = default;

void SoundscapeEcologyAudioProcessor::resetCurves(int slot)
{
    for (int i = 0; i < kDrawPoints; ++i)
    {
        pitchCurves[(size_t)slot][(size_t)i] = 0.5f;                      // flat pitch
        const float u = (float)i / (float)(kDrawPoints - 1);
        ampCurves[(size_t)slot][(size_t)i] = std::sin(juce::MathConstants<float>::pi * u); // arch
    }
}

void SoundscapeEcologyAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    habitat.prepare(sampleRate, samplesPerBlock);
    weather.prepare(sampleRate);
    habitat.reset();

    slotBuffer.setSize(2, samplesPerBlock);
    // 8x headroom so a host changing block size mid-stream cannot force an
    // allocation on the audio thread
    gateBuffer.assign((size_t)juce::jmax(8192, samplesPerBlock * 8), 0.0f);
    noteVelocity.fill(0);
    heldNotes = 0;
    gateEnv = gateTarget = 0.0f;

    for (int s = 0; s < kNumSlots; ++s)
    {
        field[(size_t)s].resize(kMaxIndividuals);
        for (int i = 0; i < kMaxIndividuals; ++i)
        {
            field[(size_t)s][(size_t)i].prepare(sampleRate, 140.0f);
            configureIndividual(s, field[(size_t)s][(size_t)i], i);
        }
        lastModel[s] = -1;
        lastPopulation[s] = -1;
    }

    outputGain.reset(sampleRate, 0.02);
    widthAmount.reset(sampleRate, 0.02);
}

void SoundscapeEcologyAudioProcessor::releaseResources()
{
    habitat.reset();
}

bool SoundscapeEcologyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

//==============================================================================
void SoundscapeEcologyAudioProcessor::readParameters()
{
    auto getF = [this](const juce::String& id) -> float
        {
            if (auto* p = apvts.getRawParameterValue(id)) return p->load();
            return 0.0f;
        };

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& sp = slotParams[s];
        sp.phony = juce::jlimit(0, kNumPhony - 1, (int)getF(pid::phony(s)));
        int mCount = 0;
        modelsForClass(sp.phony, mCount);
        // classes have different model counts, so clamp when the class changes
        sp.model = juce::jlimit(0, mCount - 1, (int)getF(pid::model(s)));
        sp.enabled = getF(pid::enabled(s)) > 0.5f;
        sp.level = getF(pid::level(s));
        for (int k = 0; k < kNumModelKnobs; ++k)
            sp.knobs[k] = getF(pid::knob(s, k));

        sp.rate = getF(pid::rate(s));
        sp.phrase = getF(pid::phrase(s));
        sp.jitter = getF(pid::jitter(s));
        sp.swing = getF(pid::swing(s));
        sp.rhythm = getF(pid::rhythm(s));
        sp.attack = getF(pid::attack(s));
        sp.decay = getF(pid::decay(s));
        sp.duration = getF(pid::duration(s));

        sp.population = juce::jlimit(1, kMaxIndividuals, (int)getF(pid::population(s)));
        sp.spread = getF(pid::spread(s));
        sp.distance = getF(pid::distance(s));
        sp.diversity = getF(pid::diversity(s));
        sp.synchrony = getF(pid::synchrony(s));
        sp.parallax = getF(pid::parallax(s));
        sp.elevation = getF(pid::elevation(s));

        sp.drawPitch = getF(pid::drawPitchOn(s)) > 0.5f;
        sp.drawAmp = getF(pid::drawAmpOn(s)) > 0.5f;
        sp.drawDepth = getF(pid::drawDepth(s));
        sp.pitchCurve = pitchCurves[(size_t)s].data();
        sp.ampCurve = ampCurves[(size_t)s].data();
    }

    globalTemperature = getF(pid::temperature);
    globalHumidity = getF(pid::humidity);
    nicheAmount = getF(pid::niche);

    // Time of Day morph. Parameter writes must not happen on the audio thread,
    // so the value is handed to the message thread via AsyncUpdater.
    const float tod = getF(pid::timeOfDay);
    if (std::abs(tod - lastSeenTimeOfDay) > 0.002f)
    {
        lastSeenTimeOfDay = tod;
        if (presets != nullptr && presets->hasMorph())
        {
            pendingMorph.store(tod);
            triggerAsyncUpdate();
        }
    }

    weatherCoupling = getF(pid::weather);

    // How much rain is falling right now, taken from whichever slot is
    // actually running a rain model. Rain silences insects in the real world,
    // and this is the cheapest way to make the weather feel causal rather
    // than layered on top.
    weather.rain = 0.0f;
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto& sp = slotParams[s];
        if (!sp.enabled || sp.phony != 1) continue;
        if (sp.model == 0 || sp.model == 5)      // Rain, Rainfall
            weather.rain = juce::jmax(weather.rain, juce::jmin(1.0f, sp.level));
    }

    // The canopy moves in a gust, so the ambience brightens and stirs.
    const float damp = juce::jlimit(0.0f, 1.0f,
        getF(pid::habDamping) - weatherCoupling * weather.gust * 0.22f);
    habitat.setParams(getF(pid::habSize), damp, getF(pid::habMix));
    outputGain.setTargetValue(getF(pid::output));
    widthAmount.setTargetValue(getF(pid::width));
}

//==============================================================================
// Acoustic niche allocator.
//
// Krause's acoustic niche hypothesis: species in a healthy habitat partition
// the spectrum so their calls do not mask each other, which is why a dawn
// chorus reads as coherent rather than muddy. Here each slot is nudged away
// from the others' carrier regions in proportion to the NICHE control.
//==============================================================================
void SoundscapeEcologyAudioProcessor::applyNiche()
{
    if (nicheAmount <= 0.001f)
    {
        for (int s = 0; s < kNumSlots; ++s) nicheShift[s] = 1.0f;
        return;
    }

    // crude but effective: order the slots by nominal carrier and push apart
    float nominal[kNumSlots];
    for (int s = 0; s < kNumSlots; ++s)
        nominal[s] = 200.0f + 6000.0f * slotParams[s].knobs[0];

    for (int s = 0; s < kNumSlots; ++s)
    {
        float push = 0.0f;
        for (int o = 0; o < kNumSlots; ++o)
        {
            if (o == s || !slotParams[o].enabled) continue;
            const float ratio = nominal[s] / juce::jmax(20.0f, nominal[o]);
            const float octaves = std::log2(juce::jmax(0.01f, ratio));
            if (std::abs(octaves) < 1.0f)          // overlapping territory
                push += (octaves >= 0.0f ? 1.0f : -1.0f) * (1.0f - std::abs(octaves));
        }
        nicheShift[s] = std::pow(2.0f, juce::jlimit(-1.0f, 1.0f, push) * 0.75f * nicheAmount);
    }
}

//==============================================================================
void SoundscapeEcologyAudioProcessor::configureIndividual(int slot, FieldIndividual& ind, int index)
{
    auto& sp = slotParams[slot];

    // per-individual variation
    const float d = sp.diversity;
    ind.carrierScale = 1.0f + d * 0.18f * (rng.nextFloat() * 2.0f - 1.0f);
    ind.qScale = 1.0f + d * 0.45f * (rng.nextFloat() * 2.0f - 1.0f);
    ind.rateScale = 1.0f + d * 0.22f * (rng.nextFloat() * 2.0f - 1.0f);

    ind.phase = rng.nextFloat() * kTwoPi;
    ind.omega = kTwoPi * juce::jmax(0.02f, sp.rate * ind.rateScale);

    ind.active = false;
    ind.ribIndex = 0;
    ind.bank.clear();
}

void SoundscapeEcologyAudioProcessor::updateField(int slot)
{
    auto& sp = slotParams[slot];
    auto& f = field[(size_t)slot];

    // distance range from the DISTANCE and SPREAD controls
    const float nearD = 1.2f + 18.0f * sp.distance;
    const float farD = nearD + 4.0f + 110.0f * sp.spread;

    for (int i = 0; i < sp.population; ++i)
    {
        auto& ind = f[(size_t)i];
        // deterministic-ish placement so it does not jump when parameters move
        const float u = (sp.population > 1) ? (float)i / (float)(sp.population - 1) : 0.5f;
        const float jitterU = std::fmod(std::sin((float)i * 12.9898f) * 43758.5453f, 1.0f);
        const float dist = nearD + (farD - nearD) * std::pow(juce::jlimit(0.0f, 1.0f,
            0.5f * (u + std::abs(jitterU))), 1.7f);
        const float az = std::sin((float)i * 7.7f) * 0.95f;
        // individuals vary in height as well as in position
        const float elevJitter = 1.0f + sp.diversity * 0.55f
            * std::sin((float)i * 3.301f + 1.7f);
        ind.place(dist, az, sp.elevation * elevJitter,
            globalHumidity, sp.parallax, currentSampleRate);
        ind.omega = kTwoPi * juce::jmax(0.02f, sp.rate * ind.rateScale);
    }
}

//==============================================================================
// Event triggering — the articulation layer.
//
// Length, attack and decay come from the articulation controls rather than
// being baked into each model, so timing can be dialled independently of
// timbre.
//==============================================================================
void SoundscapeEcologyAudioProcessor::triggerEvent(int slot, FieldIndividual& ind)
{
    auto& sp = slotParams[slot];

    const float durJitter = 1.0f + sp.jitter * 0.4f * (rng.nextFloat() * 2.0f - 1.0f);
    ind.eventLength = juce::jmax(32, (int)(currentSampleRate * sp.duration * durJitter));
    ind.eventSample = 0;
    ind.eventAmp = 0.55f + 0.45f * rng.nextFloat();
    ind.active = true;
    ind.ribIndex = 0;
    ind.oscPhase = 0.0f;
    ind.oscPhase2 = 0.0f;
    ind.subPhase = 0.0f;

    // (Re)build the resonant body for this event. Cheap enough to do per event,
    // and it lets DIVERSITY act on the body as well as the rhythm.
    int count = 0;
    auto* models = modelsForClass(sp.phony, count);
    const auto& md = models[juce::jlimit(0, count - 1, sp.model)];
    const juce::String name(md.name);

    ind.bank.clear();
    const float k0 = sp.knobs[0], k1 = sp.knobs[1];

    if (name == "Cricket" || name == "Katydid")
    {
        // Field crickets sit in 2-8 kHz. Tooth jitter smears the drive
        // (measured: spectral flatness rises ~74x from even to irregular
        // teeth); the resonator Q is the body and moves separately.
        const float carrier = (2000.0f + 6000.0f * k0) * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        const float q = (name == "Cricket" ? 20.0f + 80.0f * k1 : 6.0f + 26.0f * k1) * ind.qScale;
        ind.bank.add(carrier, q, 1.00f, currentSampleRate);
        ind.bank.add(carrier * 2.02f, q * 0.45f, 0.10f, currentSampleRate);
        ind.bank.add(carrier * 0.51f, 14.0f, 0.045f, currentSampleRate);
    }
    else if (name == "Cicada")
    {
        // Tymbal ribs buckle in sequence, each click exciting the tymbal
        // (primary resonator) and then the abdominal air sac, which acts as a
        // Helmholtz resonator with the tympana as its neck (Bennet-Clark &
        // Young 1992). Measured from reference: carrier ~4.2 kHz, muscle rate
        // ~368 Hz. Successive pulses maintain coherent resonance, which is why
        // Q sits far higher than a single struck tymbal would suggest.
        const float carrier = (1500.0f + 12500.0f * k0) * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        const float q = (30.0f + 110.0f * k1) * ind.qScale;
        const float bright = sp.knobs[7];
        ind.bank.add(carrier, q, 1.00f, currentSampleRate);
        ind.bank.add(carrier * 1.63f, q * 0.70f, 0.45f * (0.4f + bright), currentSampleRate);
        ind.bank.add(carrier * 2.31f, q * 0.55f, 0.28f * (0.3f + bright), currentSampleRate);
        // second spectral band — Hyalessa maculaticollis is reported with two
        // distinct peaks at 4.7 and 15 kHz, and the supplied reference shows
        // exactly that two-band leap
        const float second = sp.knobs[6];
        if (second > 0.02f)
            // Measured from a field recording of a Japanese cicada: the two
            // bands sit at 4188 Hz and 15542 Hz, a ratio of 3.71 — not the
            // 5.3 that was hardcoded here, which pushed the upper band into
            // the 16 kHz clamp and flattened it against the ceiling.
            // The reference puts 32% of the total energy in the upper band; the old
            // gain range topped out near 21%, so it could never be reached.
            ind.bank.add(juce::jmin(carrier * 3.71f, 17000.0f), q * 1.6f,
                second * 1.7f, currentSampleRate);
        // abdominal air sac
        ind.aux[0].set(carrier * (0.55f + 0.5f * sp.knobs[5]), 5.0f, currentSampleRate);
    }
    else if (name == "Anuran")
    {
        // The vocal sac is NOT a cavity resonator. Rand & Dudley (1993)
        // replaced the vocal-tract air with heliox, in which sound travels
        // ~75% faster, and found no consistent shift in the call spectrum —
        // a cavity resonance would have moved. The sac and head instead act
        // as a broadband RADIATOR that impedance-matches the sound to air and
        // emphasises one harmonic of the fold rate. Smaller heads emphasise
        // higher ones: Hyla microcephala peaks at 6.1 kHz, H. ebraccata at
        // 3.2 kHz.
        const float dominant = juce::jlimit(120.0f, 9000.0f, 150.0f + 6200.0f * sp.knobs[2])
            * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        // The emphasis has to be genuinely selective or the fold rate stays
        // on top and every species collapses to its fundamental. A gentle
        // shelf is not enough: Hyla microcephala's dominant sits four
        // harmonics above F0.
        // Reference recordings show the carrier is nearly a PURE TONE: in a
        // sustained call the 2nd harmonic sits 53 dB down and the 3rd 47 dB
        // down. A moderate Q cannot get near that, so the radiator has to be
        // able to go genuinely narrow. The curve is shaped so most of the
        // knob's travel stays in the useful broad range.
        const float r3 = 1.0f - sp.knobs[3];
        const float sel = 1.6f + 78.0f * r3 * r3 * r3;
        // The two auxiliary modes give a broad radiator its body, but one of
        // them sits essentially ON the second harmonic — so they have to fade
        // out as the radiator narrows, or they set a -26 dB floor and the
        // near-pure calls stay impossible.
        const float broad = sp.knobs[3];
        ind.bank.add(dominant, sel, 1.00f, currentSampleRate);
        ind.bank.add(dominant * 1.97f, sel * 0.55f, 0.16f * broad, currentSampleRate);
        ind.bank.add(dominant * 0.5f, sel * 0.45f, 0.08f * broad, currentSampleRate);
    }
    else if (name == "Frog")
    {
        // A frog is a pulse train through TWO resonances, not a harmonic
        // stack: the inflated vocal sac acting as a Helmholtz resonator, plus
        // a higher mouth/tympanic formant. Stacking harmonics of the larynx
        // produced a buzzy organ tone rather than a croak, because a croak
        // gets its identity from formants sitting at FIXED frequencies while
        // the pulse rate moves underneath them.
        const float larynx = (60.0f + 900.0f * k0) * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        const float inflate = 0.35f + 1.3f * (1.0f - sp.knobs[1]);
        const float sacF = juce::jlimit(90.0f, 3200.0f, larynx * inflate * 1.6f);
        ind.bank.add(sacF, 3.5f + 9.0f * k1, 1.00f, currentSampleRate, false);
        ind.bank.add(sacF * 2.35f, 4.0f + 10.0f * k1, 0.55f, currentSampleRate, false);
        ind.bank.add(sacF * 4.10f, 5.0f, 0.22f, currentSampleRate, false);
        ind.bank.add(larynx, 7.0f, 0.30f, currentSampleRate, false);
    }
    else if (name == "Metal Bar")
    {
        // A free-free steel bar. Its transverse modes are STRONGLY inharmonic:
        //   1 : 2.756 : 5.404 : 8.933 : 13.344 : 18.638
        // That inharmonicity is exactly why struck metal reads as metal and
        // not as a string. This is a different object from the plates above,
        // whose modes go as (m/a)^2 + (n/b)^2 and are far denser.
        static const float barRatios[] = { 1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.638f };
        const float f0 = (80.0f + 900.0f * k0) * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        const float hard = sp.knobs[1];
        const float decayC = 0.5f + 11.0f * sp.knobs[5];
        const float split = 0.0004f + 0.010f * sp.knobs[4];
        for (int m = 0; m < 6; ++m)
        {
            const float f = f0 * barRatios[m] * (1.0f + 0.0016f * (rng.nextFloat() - 0.5f));
            if (f > 17000.0f) break;
            // higher modes die faster, and harder strikes excite them more
            const float t60 = decayC / (1.0f + 1.15f * (float)m);
            const float g = std::pow(0.30f + 0.70f * hard, (float)m);
            // Impulsively excited: struck once per event, never driven
            // coherently at each mode's own rate, so no Q-buildup compensation.
            ind.bank.add(f, qFromT60(f, t60), g, currentSampleRate, false);
            // the two polarisations of a real bar, giving slow beating
            if (sp.knobs[3] > 0.02f)
                ind.bank.add(f * (1.0f + split), qFromT60(f, t60 * 0.95f),
                    g * 0.6f * sp.knobs[3], currentSampleRate, false);
        }
    }
    else if (name == "Plate Shear" || name == "Plate Impact" || name == "Bell")
    {
        // Measured from reference plate: ratios 1.000 1.113 1.182 1.334 1.500
        // 1.666 1.771 1.993 2.222 — dense and inharmonic, i.e. a PLATE, not
        // the free-free bar series. Per-mode T60 spans 0.03 s to 11.5 s in the
        // same object, and modes come in split pairs 0.3-0.7% apart, which is
        // what produces the slow beating.
        // Down to 14 Hz. A ship's hull plate is metres across and its
        // fundamental is felt more than heard; the old 40 Hz floor made every
        // "large" plate sound like a small panel.
        const float f0 = (14.0f + 430.0f * k0) * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        const float aspect = 1.1f + 2.4f * sp.knobs[1];
        const float split = 0.001f + 0.012f * sp.knobs[4];
        const float decayC = 0.08f + 22.0f * sp.knobs[5];   // up to ~22 s for huge steel
        const float decaySpread = sp.knobs[6];
        const float bright = sp.knobs[7];

        float base = -1.0f;
        for (int m = 1; m <= 8; ++m)
        {
            for (int nn = 1; nn <= 8; ++nn)
            {
                const float v = (float)(m * m) + aspect * aspect * (float)(nn * nn);
                if (base < 0.0f) base = v;
                const float f = f0 * v / base;
                if (f > 16000.0f || ind.bank.numModes >= kMaxModes - 2) continue;

                // per-mode decay, log-uniform, tilted so high modes die sooner
                const float tilt = std::pow(f / f0, -0.45f);
                const float r = rng.nextFloat();
                const float t60 = juce::jlimit(0.02f, 14.0f,
                    std::exp(std::log(0.05f) + r * decaySpread * 5.5f) * decayC * tilt);
                const float g = (1.0f / (1.0f + std::pow(f / f0, 1.75f - bright)))
                    * (0.35f + 0.65f * rng.nextFloat());
                // Impulsive: a stick-slip release or an impact hit each
                // mode once per event, not repeatedly at its own rate.
                ind.bank.add(f, qFromT60(f, t60), g, currentSampleRate, false);
                if (rng.nextFloat() < 0.55f && ind.bank.numModes < kMaxModes - 1)
                    ind.bank.add(f * (1.0f + split * (0.4f + 1.2f * rng.nextFloat())),
                        qFromT60(f, t60 * 0.9f), g * 0.72f, currentSampleRate, false);
            }
        }
    }
    else if (name == "Rain Surface")
    {
        // A thin struck panel. INHARMONIC morphs the mode set from a harmonic
        // series (a taut membrane, a tent) toward plate bending modes
        // (m^2 + gamma*n^2), which is what makes a metal roof sound like metal
        // rather than like a drum.
        const float f0 = (90.0f + 1500.0f * sp.knobs[2])
            * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        const float inh = sp.knobs[7];
        const float t60 = 0.02f + 1.4f * sp.knobs[4] * sp.knobs[4];
        int added = 0;
        for (int m = 1; m <= 5 && added < 20; ++m)
        {
            for (int n = 1; n <= 4 && added < 20; ++n)
            {
                const float harmonic = (float)(m * n);
                const float plate = ((float)(m * m) + 2.3f * (float)(n * n)) / 3.3f;
                const float ratio = harmonic + (plate - harmonic) * inh;
                const float f = f0 * ratio;
                if (f > 15000.0f) continue;
                const float decay = t60 / (1.0f + 0.7f * (float)(m + n - 2));
                const float g = 1.0f / (1.0f + 0.55f * (float)(m + n - 2));
                // Each drop is a single impact, not a coherent drive.
                ind.bank.add(f, qFromT60(f, decay), g, currentSampleRate, false);
                ++added;
            }
        }
    }
    else if (name == "Rainfall" || name == "Ocean Surf")
    {
        // no resonator bank: the grains are synthesised directly
    }
    else if (name == "Rain")
    {
        // only the impact click needs a body now; the bubbles are grains
        const float f = juce::jlimit(200.0f, 9000.0f, 1200.0f + 5000.0f * sp.knobs[3]);
        ind.bank.add(f, 3.0f, 0.8f, currentSampleRate);
    }
    else if (false)
    {
        // Minnaert: a bubble of radius r rings at f = 3.26 / r (r in metres),
        // so a 1 mm bubble sings near 3.3 kHz. Rain is an impact click plus an
        // entrained bubble ping, which is why it sounds silvery not static.
        // Moving water is almost entirely bubble entrainment; the "rushing"
        // quality is thousands of these, not filtered noise.
        const float rMM = 0.3f + 4.5f * k0;
        const float f = juce::jlimit(60.0f, 16000.0f, 3260.0f / rMM);
        const float spreadOct = 0.4f + 2.6f * sp.knobs[1];
        for (int i = 0; i < 4; ++i)
        {
            const float oct = ((float)i / 3.0f - 0.5f) * spreadOct;
            ind.bank.add(f * std::pow(2.0f, oct) * ind.carrierScale,
                12.0f + 60.0f * k1, 1.0f / (1.0f + 0.6f * (float)i),
                currentSampleRate);
        }
    }
    else if (name == "Fire")
    {
        // Fire is two unrelated things summed: sharp crackle transients from
        // moisture pockets bursting in the wood (short, high, resonant) and a
        // low combustion rumble from unsteady heat release (broadband, under
        // ~100 Hz). The ratio of the two IS the character — a campfire is
        // crackle-dominant, a furnace is rumble-dominant.
        const float crackF = (600.0f + 5200.0f * k0) * ind.carrierScale;
        const float spreadOct = 0.5f + 2.5f * sp.knobs[1];
        for (int i = 0; i < 5; ++i)
        {
            const float oct = ((float)i / 4.0f - 0.5f) * spreadOct;
            ind.bank.add(crackF * std::pow(2.0f, oct),
                8.0f + 40.0f * k1, 1.0f / (1.0f + 0.7f * (float)i),
                currentSampleRate);
        }
        ind.aux[0].set(juce::jlimit(40.0f, 400.0f, 60.0f + 240.0f * sp.knobs[3]),
            2.4f, currentSampleRate);
    }
    else if (name == "Rumble")
    {
        // A distant strike built entirely from struck modal-bank resonances:
        // three low "thump" bodies plus ONE shared noise burst duplicated
        // through five band-pass copies at different centre
        // frequencies/decays — struck once per event, so they join the bank
        // exactly like Bell/Plate/Metal Bar's modes do (coherent = false).
        const float registerMul = std::pow(2.0f, (sp.knobs[1] - 0.5f) * 2.0f) * ind.carrierScale;

        // crack texture: centres ~98/212/392/689/1091 Hz, weights
        // 1.00/0.75/0.55/0.38/0.24. CRACK SPREAD stretches/compresses that
        // ladder in octaves around the first band; at its default the
        // layout above comes back out.
        static const float crackRatio[5] = { 1.0f, 2.16f, 4.00f, 7.03f, 11.13f };
        static const float crackWeight[5] = { 1.00f, 0.75f, 0.55f, 0.38f, 0.24f };
        const float crackSpread = 0.4f + 1.2f * sp.knobs[4];
        const float crackT60Base = 0.02f + 0.4f * sp.knobs[5];
        for (int i = 0; i < 5; ++i)
        {
            const float f = juce::jmin(16000.0f, 98.0f * registerMul
                * std::pow(crackRatio[i], crackSpread));
            // small per-band decay stretch, drawn once per event, so bands
            // do not all cut off in lock-step (a random stretch in the
            // 0.85-1.25 range).
            const float stretch = 0.85f + 0.4f * rng.nextFloat();
            ind.bank.add(f, qFromT60(f, crackT60Base * stretch), crackWeight[i],
                currentSampleRate, false);
        }

        // the three low thumps for weight at the strike: 55/78/44 Hz at
        // 1.00/0.55/0.65, T60 taken from each thump's (duration * 0.35).
        static const float thumpFreq[3] = { 55.0f, 78.0f, 44.0f };
        static const float thumpGain[3] = { 1.00f, 0.55f, 0.65f };
        static const float thumpT60[3] = { 0.098f, 0.070f, 0.1225f };
        const float punchFreqMul = 0.75f + 0.5f * sp.knobs[3];
        const float punchGain = 0.4f + 2.2f * sp.knobs[3];
        for (int i = 0; i < 3; ++i)
        {
            const float f = thumpFreq[i] * punchFreqMul * registerMul;
            ind.bank.add(f, qFromT60(f, thumpT60[i]), thumpGain[i] * punchGain,
                currentSampleRate, false);
        }

        ind.thunderStruck = false;
        ind.thunderBurstSamples = 0;
        ind.thunderThumpSamples[0] = ind.thunderThumpSamples[1] = ind.thunderThumpSamples[2] = -1;
        ind.thunderReburstActive = false;
        ind.thunderReburstSamplesLeft = 0;
        ind.thunderTremPhase = 0.0f;
    }
    else if (name == "Thunder")
    {
        // A close strike built from independent Butterworth-filtered noise
        // layers rather than the modal bank. Nothing here touches ind.bank
        // or ind.aux — nothing is added to the modal bank for this model at
        // all, deliberately, since the whole point is a separate, cleaner
        // texture. The two short excitation buffers this event needs (the
        // shared core burst for the crack, the shared re-arrival burst for
        // the tail swells) are rendered once here, per triggered event, and
        // everything downstream in generateSample reads from these buffers
        // through independent Butterworth bandpasses.
        const float registerMul = std::pow(2.0f, (sp.knobs[1] - 0.5f) * 2.0f) * ind.carrierScale;

        // ---- pre-rumble band (40-300 Hz quiet murmur ahead of the strike) --
        ind.tv5PreBand.set(40.0f * registerMul, 300.0f * registerMul, currentSampleRate);
        ind.tv5PreWalk = 0.0f;
        ind.tv5TremPhase = 0.0f;

        // ---- crack: 5 Butterworth copies of ONE shared core burst ---------
        // Centres/weights:
        //   60-160 / 140-320 / 280-550 / 500-950 / 850-1400 Hz
        //   weights 1.00 / 0.75 / 0.55 / 0.38 / 0.24
        // CRACK SPREAD stretches/compresses that ladder in octaves around the
        // first band; at its default setting the layout above comes back
        // out.
        static const float crackCenterRatio[5] = { 1.00f, 2.09f, 3.77f, 6.59f, 10.23f };
        static const float crackRelWidth[5] = { 0.909f, 0.783f, 0.651f, 0.621f, 0.489f };
        static const float crackDelaySec[5] = { 0.000f, 0.004f, 0.009f, 0.014f, 0.020f };
        const float crackSpread = 0.5f + 1.0f * sp.knobs[4];
        const float crackCenter0 = 110.0f * registerMul;
        for (int i = 0; i < FieldIndividual::kTv5CrackBands; ++i)
        {
            const float centre = crackCenter0 * std::pow(crackCenterRatio[i], crackSpread);
            const float half = centre * crackRelWidth[i] * 0.5f;
            ind.tv5CrackBand[i].set(centre - half, centre + half, currentSampleRate);
            // per-band decay stretch, drawn once per event (a random
            // stretch in the 0.85-1.25 range).
            ind.tv5CrackStretch[i] = 0.85f + 0.4f * rng.nextFloat();
        }
        (void)crackDelaySec; // consumed sample-accurately in generateSample

        // ---- one shared ~0.22s core burst: onset ramp then exp decay -----
        {
            const float durS = 0.22f;
            const int n = (int)(currentSampleRate * durS);
            ind.tv5Core.assign((size_t)juce::jmax(1, n), 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const float t = (float)i / (float)currentSampleRate;
                const float env = (t < durS * 0.03f)
                    ? t / (durS * 0.03f)
                    : std::exp(-(t - durS * 0.03f) / (durS * 0.45f));
                ind.tv5Core[(size_t)i] = (rng.nextFloat() * 2.0f - 1.0f) * env;
            }
        }
        // ---- one shared ~0.35s re-arrival burst, same shape, longer ------
        {
            const float durS = 0.35f;
            const int n = (int)(currentSampleRate * durS);
            ind.tv5Rebursts.assign((size_t)juce::jmax(1, n), 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const float t = (float)i / (float)currentSampleRate;
                const float env = (t < durS * 0.03f)
                    ? t / (durS * 0.03f)
                    : std::exp(-(t - durS * 0.03f) / (durS * 0.45f));
                ind.tv5Rebursts[(size_t)i] = (rng.nextFloat() * 2.0f - 1.0f) * env;
            }
        }

        // ---- 5 independently-modulated tail bands -------------------------
        // Reference bands/decay time-constants (render_tail):
        //   35-70/5.5s, 60-130/3.8s, 110-220/2.6s, 200-400/1.8s, 350-700/1.2s
        static const float tailLo[5] = { 35.0f, 60.0f, 110.0f, 200.0f, 350.0f };
        static const float tailHi[5] = { 70.0f, 130.0f, 220.0f, 400.0f, 700.0f };
        for (int i = 0; i < FieldIndividual::kTv5TailBands; ++i)
        {
            ind.tv5TailBand[i].set(tailLo[i] * registerMul, tailHi[i] * registerMul, currentSampleRate);
            ind.tv5TailWalk[i] = 0.0f;
        }

        ind.tv5Struck = false;
        ind.tv5CrackPos = -1;
        ind.tv5ThumpElapsed[0] = ind.tv5ThumpElapsed[1] = ind.tv5ThumpElapsed[2] = -1;
        for (auto& rv : ind.tv5ReburstVoice) { rv.active = false; rv.pos = 0; rv.amp = 0.0f; rv.etSecs = 0.0f; rv.band.reset(); }
    }
    else if (name == "Machinery")
    {
        // A rotating machine: periodic impacts into a metal housing, plus
        // load-dependent broadband noise. Same modal idea as the plates but
        // sparser and far more regular, which is what makes machinery read as
        // man-made — nature is almost never this periodic.
        const float f0 = (60.0f + 900.0f * k0) * ind.carrierScale * nicheShift[slot] * midiPitchRatio;
        for (int m = 1; m <= 7; ++m)
        {
            const float f = f0 * (float)m * (1.0f + 0.012f * (float)(m * m));
            if (f > 14000.0f) break;
            // Struck once per revolution, not driven continuously at the
            // mode's own frequency.
            ind.bank.add(f, 14.0f + 70.0f * sp.knobs[4],
                1.0f / (1.0f + 0.55f * (float)m), currentSampleRate, false);
        }
        ind.aux[0].set(juce::jlimit(60.0f, 6000.0f, 200.0f + 3000.0f * sp.knobs[2]),
            1.8f, currentSampleRate);
    }

    // Rainfall and Ocean Surf are CONTINUOUS textures, not discrete calls.
    // Wrapping them in 2.5 s events with attack and decay made them pulse,
    // which is most of why they did not sound like the reference. Give them
    // an effectively endless event so the articulation layer never gates them.
    if (name == "Rainfall" || name == "Ocean Surf")
    {
        ind.eventLength = 0x40000000;
        ind.eventSample = 0;
        ind.eventAmp = 1.0f;
    }

    // amplitude envelope increment for the articulation layer
    // Distant Road sweeps its own ground comb as the car passes, so the
    // Field must not add a second static one on top.
    ind.ownGroundComb = (name == "Distant Road");

    ind.env = 0.0f;
    ind.envInc = 1.0f / (float)ind.eventLength;
}

//==============================================================================
// Per-sample generation for one individual.
//==============================================================================
float SoundscapeEcologyAudioProcessor::generateSample(int slot, FieldIndividual& ind, float t01)
{
    auto& sp = slotParams[slot];
    int count = 0;
    auto* models = modelsForClass(sp.phony, count);
    const auto& md = models[juce::jlimit(0, count - 1, sp.model)];
    const juce::String name(md.name);
    const float sr = (float)currentSampleRate;

    // ---- articulation envelope -------------------------------------------
    const float atk = juce::jmax(0.002f, sp.attack);
    const float dec = juce::jmax(0.002f, sp.decay);
    float env;
    if (t01 < atk)              env = t01 / atk;
    else if (t01 > 1.0f - dec)  env = juce::jmax(0.0f, (1.0f - t01) / dec);
    else                        env = 1.0f;
    env = env * env * (3.0f - 2.0f * env);   // smoothstep, avoids clicky corners

    if (name == "Rainfall" || name == "Ocean Surf")
        env = 1.0f;                          // continuous, never gated

    // ---- species rhythm ---------------------------------------------------
    // Divide the event into PHRASE syllables and shape them with RHYTHM.
    // At rhythm 0 this is a shallow tremolo; at rhythm 1 each syllable is a
    // sharp exponential decay. This is the layer that was missing entirely,
    // and it is what makes one cicada sound like a different species from
    // another whose spectrum is nearly identical.
    // Position within the current SYLLABLE. Pitch gestures follow this rather
    // than the whole event: in a five-note owl phrase each hoot carries its own
    // arch, so an arch stretched across the phrase reads as a slow drift and
    // the individual notes come out flat.
    float gestureT = t01;

    if (sp.phrase > 1.005f)
    {
        // HOLD: sustain first, then break into syllables. Minmin-zemi is a
        // long held "miiin" followed by "min min min min", a two-stage shape
        // that a uniform syllable division cannot express.
        const float hold = juce::jlimit(0.0f, 0.9f, sp.swing);
        const float after = (t01 <= hold) ? 0.0f
            : (t01 - hold) / juce::jmax(0.01f, 1.0f - hold);
        const float sylPos = (t01 <= hold) ? 0.0f
            : std::fmod(after * sp.phrase, 1.0f);
        gestureT = sylPos;
        const float depth = 0.30f + 0.68f * sp.rhythm;
        // Continuous blend from smooth tremolo to struck syllable. An earlier
        // version switched branches at 0.5, which put a discontinuity right
        // in the middle of the useful range.
        const float tremolo = 0.5f + 0.5f * std::cos(kTwoPi * sylPos);
        const float struck = std::exp(-sylPos * 6.0f);
        const float shaped = tremolo + (struck - tremolo) * sp.rhythm;
        env *= (1.0f - depth) + depth * shaped;
    }

    // optional drawn amplitude curve
    if (sp.drawAmp && sp.ampCurve != nullptr)
    {
        const float idx = t01 * (kDrawPoints - 1);
        const int i0 = juce::jlimit(0, kDrawPoints - 1, (int)idx);
        const int i1 = juce::jmin(kDrawPoints - 1, i0 + 1);
        const float fr = idx - (float)i0;
        const float drawn = sp.ampCurve[i0] * (1.0f - fr) + sp.ampCurve[i1] * fr;
        // Full replacement of the envelope at depth 1, not a 2x nudge.
        env = env * ((1.0f - sp.drawDepth) + sp.drawDepth * drawn * 2.2f);
    }

    // optional drawn pitch curve -> a multiplier around 1.0
    float pitchMul = 1.0f;
    if (sp.drawPitch && sp.pitchCurve != nullptr)
    {
        const float idx = t01 * (kDrawPoints - 1);
        const int i0 = juce::jlimit(0, kDrawPoints - 1, (int)idx);
        const int i1 = juce::jmin(kDrawPoints - 1, i0 + 1);
        const float fr = idx - (float)i0;
        const float drawn = sp.pitchCurve[i0] * (1.0f - fr) + sp.pitchCurve[i1] * fr;
        // +/- 3 octaves at full depth. The old +/- 1 octave was inaudible on
        // broadband models where the carrier is only part of the picture.
        pitchMul = std::pow(2.0f, (drawn - 0.5f) * 12.0f * sp.drawDepth);
    }

    float out = 0.0f;

    switch (md.engine)
    {
        // =================================================================
    case Engine::StochasticModal:
    {
        float exc = 0.0f;

        if (name == "Cricket" || name == "Katydid")
        {
            // scraper dragging across a file of teeth: an impulse train
            // whose regularity decides the tonality
            const float carrier = (2000.0f + 6000.0f * sp.knobs[0]) * ind.carrierScale
                * nicheShift[slot] * midiPitchRatio * pitchMul;
            const float detune = 0.75f + 0.5f * sp.knobs[4];
            const float taper = sp.knobs[3];
            const float glide = 1.0f + (1.0f - taper) * 0.22f * (t01 - 0.5f);
            const float rate = carrier * detune * glide;

            ind.oscPhase += rate / sr;
            if (ind.oscPhase >= 1.0f)
            {
                ind.oscPhase -= 1.0f;
                const float jit = sp.knobs[2];
                exc = (1.0f - jit * 0.9f * rng.nextFloat()) * (0.8f + 0.4f * rng.nextFloat());
                // pulse-train grouping within the chirp
                const float pulses = 2.0f + 6.0f * sp.knobs[5];
                const float pulsePhase = std::fmod(t01 * pulses, 1.0f);
                exc *= (pulsePhase < 0.62f) ? std::sin(juce::MathConstants<float>::pi * pulsePhase / 0.62f) : 0.0f;
            }
        }
        else if (name == "Cicada")
        {
            // one muscle contraction = N ribs buckling in sequence,
            // posterior to anterior, each successive rib weaker and lower
            const float baseRate = 150.0f + 450.0f * sp.knobs[3];
            // The tymbal muscle rate SAGS across a phrase and resets:
            // measured 371 -> 355 -> 323 Hz, about -13%, and the sag
            // accelerates toward the end as sac pressure falls rather
            // than falling linearly.
            const float sag = t01 * t01 * (3.0f - 2.0f * t01);
            const float drift = 1.0f + (sp.knobs[4] - 0.5f) * 0.4f * sag;
            const float muscle = baseRate * drift * ind.rateScale;
            const int   nRibs = 2 + (int)(sp.knobs[2] * 5.0f);

            ind.subPhase += muscle / sr;
            if (ind.subPhase >= 1.0f) { ind.subPhase -= 1.0f; ind.ribIndex = 0; }

            const float ribSpan = 0.55f;
            const float ribPos = ind.subPhase / ribSpan;
            const int   want = (int)(ribPos * (float)nRibs);
            if (ind.subPhase < ribSpan && want > ind.ribIndex - 1 && want < nRibs)
            {
                if (want >= ind.ribIndex)
                {
                    exc = std::pow(0.86f, (float)ind.ribIndex) * (0.9f + 0.2f * rng.nextFloat());
                    ++ind.ribIndex;
                }
            }
        }
        else if (name == "Anuran")
        {
            // 1. VOCAL FOLDS — their opening/closing rate IS the call
            //    fundamental (Walkowiak 2007).
            const float f0 = (80.0f + 3300.0f * sp.knobs[0])   // peepers reach 2.9 kHz
                * ind.carrierScale * nicheShift[slot]
                * midiPitchRatio * pitchMul;
            const float sweep = 1.0f + (sp.knobs[7] - 0.5f) * 1.2f * t01;
            const float fInst = juce::jmax(30.0f, f0 * sweep);

            // 2. FIBROUS MASS loads the folds, detuning a second
            //    oscillator. Coupled, the pair locks; loaded heavily it
            //    beats and then breaks into subharmonics — the tungara
            //    "chuck" lives here.
            const float massRatio = sp.knobs[4];
            ind.foldPhase += fInst / sr;
            if (ind.foldPhase >= 1.0f) ind.foldPhase -= 1.0f;
            ind.massPhase += fInst * (1.0f - 0.5f * massRatio) / sr;
            if (ind.massPhase >= 1.0f) ind.massPhase -= 1.0f;

            // glottal pulse: open for only part of the cycle, which is
            // what makes the source harmonically rich rather than a sine
            const float oq = juce::jlimit(0.03f, 0.9f,
                0.06f + 0.5f * (1.0f - sp.knobs[6]));
            const float p1 = ind.foldPhase, p2 = ind.massPhase;
            const float g1 = ((p1 < oq) ? 0.5f - 0.5f * std::cos(MathPi * p1 / oq) : 0.0f) - 0.25f;
            const float g2 = ((p2 < oq) ? 0.5f - 0.5f * std::cos(MathPi * p2 / oq) : 0.0f) - 0.25f;
            float x = 0.65f * g1 + 0.35f * g2 + 0.5f * g1 * g2;

            const float drive = 1.0f + 6.0f * sp.knobs[5];
            x = std::tanh(drive * x) / std::tanh(drive);

            // 3. ARYTENOID CARTILAGES open and close rhythmically,
            //    modulating AMPLITUDE on a clock entirely separate from
            //    the fundamental. This pulse rate is the main species
            //    signature — a boreal chorus frog runs about 15/sec.
            const float pulseRate = 0.5f + 140.0f * sp.knobs[1] * sp.knobs[1];
            ind.arytenoidPhase += pulseRate * (1.0f - 0.25f * t01) / sr;
            if (ind.arytenoidPhase >= 1.0f) ind.arytenoidPhase -= 1.0f;
            // Measured AM depth in the reference is 1.00 — the envelope
            // goes fully to zero between pulses — and the shape is a fast
            // attack into an exponential decay, not a sinusoid. A 0.1
            // floor left the pulses smeared together.
            const float smooth = 0.5f - 0.5f * std::cos(kTwoPi * ind.arytenoidPhase);
            const float struck = std::exp(-ind.arytenoidPhase * 9.0f)
                * juce::jmin(1.0f, ind.arytenoidPhase * 26.0f);
            const float shape = juce::jlimit(0.0f, 1.0f, sp.knobs[1] * 2.2f);
            exc = x * (0.02f + 0.98f * (smooth + (struck - smooth) * shape));
        }
        else if (name == "Frog")
        {
            // Pulse rate falls across the croak as the animal runs out of
            // air, which is most of what makes it read as an animal rather
            // than a buzzer.
            const float base = 12.0f + 190.0f * sp.knobs[2];
            const float rate = base * (1.0f - 0.35f * t01) * ind.rateScale;
            ind.oscPhase += rate / sr;
            if (ind.oscPhase >= 1.0f)
            {
                ind.oscPhase -= 1.0f;
                // alternate pulse strength gives the characteristic
                // creaking double-pulse of many species
                ind.ribIndex ^= 1;
                exc = (ind.ribIndex ? 1.0f : 0.55f) * (0.75f + 0.25f * rng.nextFloat());
                exc *= (1.0f - sp.knobs[4] * 0.7f * rng.nextFloat());
            }
        }
        else if (name == "Plate Shear")
        {
            // Slow the drive right down at the bottom of its range: a
            // gigantic plate creeps rather than chatters.
            const float driveRate = (0.05f + 7.0f * sp.knobs[2] * sp.knobs[2]) * 300.0f / sr;
            exc = ind.friction.process(driveRate, sp.knobs[3]);
        }
        else if (name == "Plate Impact" || name == "Bell")
        {
            if (ind.eventSample < (int)(sr * (0.0006f + 0.004f * (1.0f - sp.knobs[2]))))
                exc = (rng.nextFloat() * 2.0f - 1.0f)
                * std::exp(-(float)ind.eventSample / (sr * 0.0012f));
        }
        else if (name == "Rain Surface")
        {
            // Drops arrive as sharp impulses into the panel. The entrained
            // bubble is still there but much quieter than on soil, because
            // a hard surface splashes rather than absorbing.
            const float density = 130.0f * (0.15f + 3.4f * sp.knobs[5]);
            if (rng.nextFloat() < density / sr)
            {
                const float amp = std::pow(rng.nextFloat(), 1.8f);
                exc = amp * (0.3f + 1.7f * sp.knobs[3]) * (rng.nextBool() ? 1.0f : -1.0f);

                // a little water alongside the ring
                const float rMM = 0.3f + 2.2f * sp.knobs[0];
                ind.drops.spawn(juce::jlimit(200.0f, 16000.0f, 3260.0f / rMM)
                    * ind.carrierScale,
                    0.5f, 0.35f, 0.0f, amp * 0.5f,
                    currentSampleRate, rng);
            }
            out += ind.drops.process(currentSampleRate, rng, ind.sideOut) * 0.4f;

            // the wash of countless drops on the far side of the surface
            ind.bedLP.setCutoff(2400.0f, currentSampleRate);
            out += ind.bedLP.lp(rng.nextFloat() * 2.0f - 1.0f)
                * 0.28f * sp.knobs[6];
        }
        else if (name == "Rainfall")
        {
            // Exact port. Reference: 2600 drops per 20 s = 130 drops/sec,
            // radius log-uniform 0.35-2.6 mm, chirp U(0.3,0.9),
            // amplitude U(0,1)^2.2, drops x0.75 + bed x0.30, then a
            // canopy lowpass.
            const float density = 130.0f * (0.15f + 3.4f * sp.knobs[5]);
            if (rng.nextFloat() < density / sr)
            {
                const float lo = std::log(0.35f), hi = std::log(2.6f);
                const float spreadK = 0.25f + 1.5f * sp.knobs[1];
                const float mid = lo + (hi - lo) * juce::jlimit(0.0f, 1.0f, sp.knobs[0]);
                const float rMM = std::exp(mid + (hi - lo) * spreadK * 0.5f
                    * (rng.nextFloat() * 2.0f - 1.0f));
                const float f = juce::jlimit(60.0f, 16000.0f,
                    3260.0f / juce::jmax(0.05f, rMM));
                // Chirp (knob 7) was declared in the model's knob list but
                // the value was hardcoded, so the control did nothing.
                // Centre the random spread on the knob instead.
                const float chirp = juce::jlimit(0.0f, 1.4f,
                    (0.1f + 1.1f * sp.knobs[7]) * (0.75f + 0.5f * rng.nextFloat()));
                const float amp = std::pow(rng.nextFloat(), 2.2f);
                ind.drops.spawn(f * ind.carrierScale, chirp,
                    (0.25f + 0.5f * rng.nextFloat()) * (0.3f + 1.4f * sp.knobs[2]),
                    0.9f * (0.2f + 1.6f * sp.knobs[3]),
                    amp, currentSampleRate, rng);
            }
            {
                float side = 0.0f;
                out += ind.drops.process(currentSampleRate, rng, side) * 0.75f;
                ind.sideOut += side * 0.75f;
            }

            // The bed uses INDEPENDENT noise per channel, as the reference
            // does. Two uncorrelated noise sources are most of why the
            // Python rain feels wide.
            ind.bedLP.setCutoff(1900.0f, currentSampleRate);
            ind.bedLP2.setCutoff(1900.0f, currentSampleRate);
            const float bedM = ind.bedLP.lp(rng.nextFloat() * 2.0f - 1.0f);
            const float bedS = ind.bedLP2.lp(rng.nextFloat() * 2.0f - 1.0f);
            const float bedG = 0.30f * (0.2f + 2.4f * sp.knobs[6]);
            out += bedM * bedG;
            ind.sideOut += bedS * bedG * 0.66f;

            // canopy absorbs the highs
            ind.canopyLP.setCutoff(16000.0f - 11000.0f * sp.knobs[4], currentSampleRate);
            out = ind.canopyLP.lp(out);
        }
        else if (name == "Ocean Surf")
        {
            // Exact port. Two swell periods (8.3 s and 13.7 s, phase 1.1)
            // beating against each other, raised to 1.8. Bubbles are
            // rejection-sampled against swell^1.4 so they cluster where
            // the wave is breaking: ~1083 candidates/sec, ~178 accepted.
            // Roar is lowpassed noise scaled by swell^2, and it is
            // multiplied by 2.2 against the bubbles' 1.0 — the roar
            // carries most of the level.
            const float rateScale = 0.35f + 1.9f * sp.knobs[2];
            ind.swellPhaseA += rateScale / (8.3f * sr);
            ind.swellPhaseB += rateScale / (13.7f * sr);
            if (ind.swellPhaseA >= 1.0f) ind.swellPhaseA -= 1.0f;
            if (ind.swellPhaseB >= 1.0f) ind.swellPhaseB -= 1.0f;

            float swell = (0.55f + 0.45f * std::sin(kTwoPi * ind.swellPhaseA))
                * (0.65f + 0.35f * std::sin(kTwoPi * ind.swellPhaseB + 1.1f));
            // BREAK (knob 6) was declared in the knob list but never read.
            // It belongs on the swell exponent: a low value gives a soft,
            // rolling swell, a high one a sharply peaked crest that
            // collapses quickly -- literally how abruptly the wave breaks.
            // The reference port's fixed 1.8 sits mid-range.
            const float breakSharp = 0.9f + 2.6f * sp.knobs[6];
            swell = std::pow(juce::jmax(0.0f, swell), breakSharp);
            // Swell Depth flattens the gating toward a constant wash
            const float sw = (1.0f - sp.knobs[3]) + sp.knobs[3] * swell;

            const float candidates = 1083.0f * (0.15f + 3.0f * sp.knobs[5]);
            if (rng.nextFloat() < candidates / sr
                && rng.nextFloat() <= std::pow(sw, 1.4f))
            {
                const float lo = std::log(0.5f), hi = std::log(5.0f);
                const float spreadK = 0.25f + 1.5f * sp.knobs[1];
                const float mid = lo + (hi - lo) * juce::jlimit(0.0f, 1.0f, sp.knobs[0]);
                const float rMM = std::exp(mid + (hi - lo) * spreadK * 0.5f
                    * (rng.nextFloat() * 2.0f - 1.0f));
                const float f = juce::jlimit(60.0f, 16000.0f,
                    3260.0f / juce::jmax(0.05f, rMM));
                // Same as Rainfall: the Chirp knob was inert here too.
                const float chirp = juce::jlimit(0.0f, 1.4f,
                    (0.1f + 1.1f * sp.knobs[7]) * (0.8f + 0.4f * rng.nextFloat()));
                const float amp = rng.nextFloat() * rng.nextFloat() * 0.5f;
                ind.drops.spawn(f * ind.carrierScale, chirp, 1.0f, 0.0f,
                    amp, currentSampleRate, rng);
            }
            {
                float side = 0.0f;
                out += ind.drops.process(currentSampleRate, rng, side);
                ind.sideOut += side;
            }

            // Broadband roar, with independent noise per channel. The
            // reference generates a full stereo noise pair here, and that
            // decorrelation is what gives the break its width.
            const float nzM = rng.nextFloat() * 2.0f - 1.0f;
            const float nzS = rng.nextFloat() * 2.0f - 1.0f;
            ind.bedLP.setCutoff(800.0f, currentSampleRate);
            ind.bedLP2.setCutoff(800.0f, currentSampleRate);
            ind.canopyLP.setCutoff(5000.0f, currentSampleRate);
            float roarM = ind.bedLP.lp(nzM) * sw * sw;
            roarM += ind.canopyLP.lp(roarM) * 0.25f;
            const float roarS = ind.bedLP2.lp(nzS) * sw * sw;
            const float rg = 2.2f * (0.2f + 1.6f * sp.knobs[4]);
            out += roarM * rg;
            ind.sideOut += roarS * rg * 0.88f;
        }
        else if (name == "Rain" || name == "Surf" || name == "Stream")
        {
            // Bubble entrainment as GRAINS, not filtered impulses.
            // Minnaert: f = 3260 / r_mm. Each grain is a damped sinusoid
            // sweeping upward as the bubble collapses.
            const float rMM = 0.3f + 4.5f * sp.knobs[0];
            const float fBase = juce::jlimit(60.0f, 16000.0f, 3260.0f / rMM);
            const float spreadOct = 0.4f + 2.6f * sp.knobs[1];
            const float chirp = 0.15f + 1.1f * sp.knobs[7];

            float density = 40.0f + 5200.0f * sp.knobs[5];
            if (name == "Surf")
            {
                const float swellRate = 0.03f + 0.35f * sp.knobs[2];
                ind.subPhase += swellRate / sr;
                if (ind.subPhase >= 1.0f) ind.subPhase -= 1.0f;
                const float swell = 0.5f - 0.5f * std::cos(kTwoPi * ind.subPhase);
                density *= (1.0f - sp.knobs[3]) + sp.knobs[3] * std::pow(swell, 1.8f);
            }
            else if (name == "Stream")
                density *= 0.4f + 1.8f * sp.knobs[2];

            if (rng.nextFloat() < density / sr)
            {
                const float oct = (rng.nextFloat() - 0.5f) * spreadOct;
                ind.bubbles.spawn(fBase * std::pow(2.0f, oct) * ind.carrierScale,
                    chirp, 0.25f + 0.75f * rng.nextFloat() * rng.nextFloat(),
                    1.0f, currentSampleRate);
            }
            out += ind.bubbles.process(currentSampleRate);

            // the sharp impact of a drop striking a surface
            if (name == "Rain" && rng.nextFloat() < density * 0.35f / sr)
                exc = sp.knobs[3] * 1.2f * (rng.nextFloat() * 2.0f - 1.0f);
        }
        else if (false)
        {
            // Poisson bubble entrainment. Rain clusters into an impact
            // click plus a bubble ping; surf gates the density with a slow
            // swell so bubbles cluster where the wave is breaking; a
            // stream is continuous with a narrow size distribution.
            float density = 20.0f + 3000.0f * sp.knobs[5];
            if (name == "Surf")
            {
                const float swellRate = 0.03f + 0.35f * sp.knobs[2];
                const float swellDepth = sp.knobs[3];
                ind.subPhase += swellRate / sr;
                if (ind.subPhase >= 1.0f) ind.subPhase -= 1.0f;
                const float swell = 0.5f - 0.5f * std::cos(kTwoPi * ind.subPhase);
                density *= (1.0f - swellDepth) + swellDepth * std::pow(swell, 1.8f);
            }
            else if (name == "Stream")
            {
                // flow speed scales entrainment rate directly
                density *= 0.4f + 1.8f * sp.knobs[2];
            }

            if (rng.nextFloat() < density / sr)
                exc = (0.3f + 0.7f * rng.nextFloat()) * (rng.nextBool() ? 1.0f : -1.0f);

            // impact click: the sharp part of a drop landing
            if (rng.nextFloat() < density * 0.35f / sr)
                exc += sp.knobs[3] * 0.9f * (rng.nextFloat() * 2.0f - 1.0f);
        }
        else if (name == "Fire")
        {
            // crackle: sparse sharp bursts, size-distributed
            const float crackRate = 3.0f + 260.0f * sp.knobs[2];
            if (rng.nextFloat() < crackRate / sr)
                exc = std::pow(rng.nextFloat(), 2.2f) * (rng.nextBool() ? 1.0f : -1.0f) * 1.4f;

            // spit: rare, much louder pops
            if (rng.nextFloat() < crackRate * 0.02f * sp.knobs[7] / sr)
                exc += (rng.nextBool() ? 1.0f : -1.0f) * (1.5f + 2.0f * rng.nextFloat());
        }
        else if (name == "Machinery")
        {
            // Rotating machine: a strictly periodic impact train, plus
            // irregular secondary clatter from slack in the mechanism.
            // The regularity is the point — nature almost never repeats
            // this exactly, so periodicity alone reads as man-made.
            const float revRate = 1.0f + 60.0f * sp.knobs[5];
            ind.oscPhase += revRate / sr;
            if (ind.oscPhase >= 1.0f)
            {
                ind.oscPhase -= 1.0f;
                exc = 0.85f + 0.15f * rng.nextFloat();
            }
            // clatter: loose parts striking off the main cycle
            if (rng.nextFloat() < revRate * 3.0f * sp.knobs[3] / sr)
                exc += (rng.nextBool() ? 1.0f : -1.0f) * 0.4f * rng.nextFloat();
            // load-dependent broadband
            exc += (rng.nextFloat() * 2.0f - 1.0f) * sp.knobs[7] * 0.05f;
        }
        else if (name == "Rumble")
        {
            // Three stages: a quiet murmur that closes in on the strike,
            // the strike itself (thumps + one shared noise burst read
            // through the bank's crack modes), and a long modulated
            // rumbling tail with a handful of re-arrival swells. STRIKE
            // POSITION places the strike within the event.
            const float strikePos = 0.08f + 0.62f * sp.knobs[0];
            const float eventSecs = (float)ind.eventLength / sr;
            const float registerMul = std::pow(2.0f, (sp.knobs[1] - 0.5f) * 2.0f) * ind.carrierScale;

            if (t01 < strikePos)
            {
                // ---- pre-rumble: quiet murmur, tremolo ramps in --------
                const float noise = rng.nextFloat() * 2.0f - 1.0f;
                ind.thunderPreHP.setCutoff(40.0f * registerMul, currentSampleRate);
                ind.thunderPreLP.setCutoff(300.0f * registerMul, currentSampleRate);
                const float low = ind.thunderPreLP.lp(ind.thunderPreHP.hp(noise));
                // a causal smoothed random walk: a leaky integrator,
                // soft-clipped to [0,1].
                ind.thunderTailWalk[0] += (rng.nextFloat() * 2.0f - 1.0f - ind.thunderTailWalk[0]) * 0.002f;
                const float walk = 0.5f + 0.5f * std::tanh(ind.thunderTailWalk[0] * 2.0f);
                float env2 = 0.15f + 0.55f * walk;

                const float timeToStrike = juce::jmax(0.0f, (strikePos - t01) * eventSecs);
                const float approach = sp.knobs[2];
                const float rate = juce::jlimit(1.5f, 7.0f, 7.0f - timeToStrike * 1.1f);
                const float depth = std::pow(juce::jlimit(0.0f, 1.0f,
                    1.0f - timeToStrike / juce::jmax(1.0e-3f, strikePos * eventSecs)), 1.5f)
                    * approach;
                ind.thunderTremPhase += rate / sr;
                if (ind.thunderTremPhase >= 1.0f) ind.thunderTremPhase -= 1.0f;
                const float tremolo = 1.0f + depth * 0.6f * std::sin(kTwoPi * ind.thunderTremPhase);
                out += low * env2 * tremolo;
            }
            else
            {
                // ---- the strike lands once, then the tail runs --------
                if (!ind.thunderStruck)
                {
                    ind.thunderStruck = true;
                    ind.thunderBurstSamples = (int)(0.22f * sr);
                    ind.thunderThumpSamples[0] = 0;
                    ind.thunderThumpSamples[1] = (int)(0.015f * sr);
                    ind.thunderThumpSamples[2] = (int)(0.035f * sr);
                }

                // the shared core burst: a soft-onset, decaying noise
                // window feeding every crack-texture mode in the bank at
                // once, so they read as one textured crack rather than
                // independent events.
                if (ind.thunderBurstSamples > 0)
                {
                    const int total = (int)(0.22f * sr);
                    const int elapsedS = total - ind.thunderBurstSamples;
                    const float durS = 0.22f;
                    const float elapsed = (float)elapsedS / sr;
                    const float benv = (elapsed < durS * 0.03f)
                        ? elapsed / (durS * 0.03f)
                        : std::exp(-(elapsed - durS * 0.03f) / (durS * 0.45f));
                    exc += (rng.nextFloat() * 2.0f - 1.0f) * benv * 0.9f;
                    --ind.thunderBurstSamples;
                }
                // the three thumps, each fired as a single impulse at its
                // own tiny delay — the bank's own Q/T60 supplies the ring.
                for (int i = 0; i < 3; ++i)
                {
                    if (ind.thunderThumpSamples[i] < 0) continue;
                    if (ind.thunderThumpSamples[i] == 0)
                        exc += (i == 0 ? 1.0f : (i == 1 ? 0.7f : 0.8f));
                    --ind.thunderThumpSamples[i];
                }

                // ---- tail: 5 independently-modulated low bands ---------
                static const float tailRatio[5] = { 1.0f, 1.80f, 3.16f, 5.77f, 10.10f };
                static const float tailWeight[5] = { 1.00f, 0.85f, 0.65f, 0.45f, 0.30f };
                static const float tailDecayTc[5] = { 5.5f, 3.8f, 2.6f, 1.8f, 1.2f };
                static const float tailSmooth[5] = { 0.6f, 0.45f, 0.35f, 0.25f, 0.15f };
                const float tailDecayMul = 0.25f + 2.5f * sp.knobs[6];
                const float tSinceStrike = juce::jmax(0.0f, (t01 - strikePos) * eventSecs);

                for (int i = 0; i < 5; ++i)
                {
                    const float f = 49.0f * registerMul * tailRatio[i];
                    auto& band = (i < 4) ? ind.aux[i] : ind.thunderTailBand4;
                    // All 5 tail bands run roughly a 2:1 high/low ratio,
                    // i.e. Q = centre/bandwidth ~ 1.4.
                    band.set(f, 1.4f, currentSampleRate);

                    auto& walk = ind.thunderTailWalk[i];
                    walk += (rng.nextFloat() * 2.0f - 1.0f - walk) * (tailSmooth[i] * 0.02f);
                    const float w = 0.35f + 0.65f * (0.5f + 0.5f * std::tanh(walk * 2.0f));

                    const float decayEnv = std::exp(-tSinceStrike / (tailDecayTc[i] * tailDecayMul));
                    const float noise = rng.nextFloat() * 2.0f - 1.0f;
                    out += band.process(noise) * decayEnv * w * tailWeight[i];
                }

                // ---- re-arrival swells: rolling echoes of the strike ---
                const float rearrival = sp.knobs[7];
                if (!ind.thunderReburstActive)
                {
                    const float rate = 0.1f + 1.0f * rearrival;
                    if (rng.nextFloat() < rate / sr)
                    {
                        const float lo = 60.0f + 240.0f * rng.nextFloat();
                        const float hi = lo + 150.0f + 500.0f * rng.nextFloat();
                        const float cf = 0.5f * (lo + hi) * registerMul;
                        ind.thunderReburstBand.set(cf, juce::jmax(0.6f, cf / juce::jmax(1.0f, hi - lo)),
                            currentSampleRate);
                        ind.thunderReburstAmp = (0.08f + 0.14f * rng.nextFloat())
                            * std::exp(-tSinceStrike / 4.0f)
                            * (0.3f + 1.4f * rearrival);
                        ind.thunderReburstSamplesLeft = (int)(0.35f * sr);
                        ind.thunderReburstActive = true;
                    }
                }
                if (ind.thunderReburstActive)
                {
                    const int total = (int)(0.35f * sr);
                    const int elapsedS = total - ind.thunderReburstSamplesLeft;
                    const float durS = 0.35f;
                    const float elapsed = (float)elapsedS / sr;
                    const float benv = (elapsed < durS * 0.03f)
                        ? elapsed / (durS * 0.03f)
                        : std::exp(-(elapsed - durS * 0.03f) / (durS * 0.45f));
                    const float noise = rng.nextFloat() * 2.0f - 1.0f;
                    out += ind.thunderReburstBand.process(noise) * benv * ind.thunderReburstAmp;
                    if (--ind.thunderReburstSamplesLeft <= 0) ind.thunderReburstActive = false;
                }
            }
        }
        else // fallback event bed
        {
            const float density = 20.0f + 3000.0f * sp.knobs[5];
            if (rng.nextFloat() < density / sr)
                exc = (0.3f + 0.7f * rng.nextFloat()) * (rng.nextBool() ? 1.0f : -1.0f);
        }

        // ACCUMULATE: several models write directly to `out` above (the
        // rain/surf grains), and an assignment here silently discarded
        // all of it.
        out += ind.bank.process(exc);

        // ---- broadband bed --------------------------------------------
        // Water is bubbles PLUS a broadband layer, and in the reference
        // renders that layer carries most of the level: surf is roughly
        // bubbles x1 + roar x2.2. Modelling only the bubbles gives a
        // pattering sound with no weight behind it. Rain's equivalent is
        // the merged bed of thousands of distant drops.
        if (name == "Surf" || name == "Rain" || name == "Stream")
        {
            const float amount = (name == "Rain") ? sp.knobs[6] : sp.knobs[4];
            if (amount > 0.005f)
            {
                const float nz = rng.nextFloat() * 2.0f - 1.0f;
                // low roar plus a lighter hiss on top
                ind.tilt.setCutoff((name == "Surf") ? 800.0f : 1900.0f, currentSampleRate);
                ind.tilt2.setCutoff(5000.0f, currentSampleRate);
                float bed = ind.tilt.lp(nz) * 3.2f + ind.tilt2.lp(nz) * 0.8f;

                // surf's roar follows the swell squared, so it surges as
                // each wave breaks rather than sitting flat
                if (name == "Surf")
                {
                    const float swell = 0.5f - 0.5f * std::cos(kTwoPi * ind.subPhase);
                    bed *= (1.0f - sp.knobs[3]) + sp.knobs[3] * swell * swell;
                }
                out += bed * amount * 2.2f;
            }
        }

        // secondary resonator (cicada air sac)
        if (name == "Cicada")
        {
            // The abdominal air sac is a Helmholtz resonator with the
            // tympana as its neck, and it is what gives a cicada its
            // characteristic BELL. A fixed low-Q mix left it barely
            // audible; the Bell control now drives both the sac Q and how
            // much of the signal passes through it.
            const float bell = sp.knobs[7];
            ind.aux[1].set(juce::jlimit(100.0f, 9000.0f,
                (1500.0f + 12500.0f * sp.knobs[0]) * (0.55f + 0.5f * sp.knobs[5])),
                2.5f + 16.0f * bell, currentSampleRate);
            const float sac = ind.aux[1].process(out);
            out = out * (0.85f - 0.35f * bell) + sac * (0.4f + 1.5f * bell);
        }

        if (name == "Plate Shear" || name == "Plate Impact")
        {
            // SUB RUMBLE: the whole structure flexing under the local
            // event. Huge steel radiates a body mode an octave or two
            // below the plate fundamental, and it is what makes a ship
            // sound enormous rather than merely loud.
            const float amount = sp.knobs[7];
            if (amount > 0.005f)
            {
                const float subF = juce::jlimit(12.0f, 160.0f,
                    (14.0f + 430.0f * sp.knobs[0]) * 0.42f);
                ind.aux[3].set(subF, 3.5f, currentSampleRate);
                out += ind.aux[3].process(exc) * amount * 3.4f;
            }
        }

        if (name == "Fire")
        {
            // combustion rumble: turbulent, unsteady heat release. Two
            // cascaded one-poles give the right slow, formless motion
            // under the crackle.
            const float n = rng.nextFloat() * 2.0f - 1.0f;
            ind.tilt.setCutoff(18.0f + 60.0f * sp.knobs[4], currentSampleRate);
            ind.tilt2.setCutoff(30.0f + 120.0f * sp.knobs[4], currentSampleRate);
            ind.rumble = ind.tilt.lp(n);
            ind.rumble2 = ind.tilt2.lp(ind.rumble);
            out += ind.aux[0].process(ind.rumble2 * 12.0f) * sp.knobs[4] * 2.2f;
            // gas hiss
            out += n * sp.knobs[6] * 0.06f;
        }
        else if (name == "Machinery")
        {
            out += ind.aux[0].process(exc) * 0.4f;
        }

        break;
    }

    // =================================================================
    case Engine::NonlinearVocal:
    {
        // A closed loop drawn in the (air-sac pressure, labial tension)
        // plane. Mindlin's group synthesise canary syllables exactly this
        // way: the PHASE OFFSET between the two axes turns a syllable from
        // an upsweep into a flat tone into a downsweep.
        const bool isOwl = (name == "Owl");

        const float phaseOffset = sp.knobs[2] * kTwoPi;
        const float u = t01 * kTwoPi;
        const float beta = 0.5f - 0.5f * std::cos(u);                 // pressure
        float       kappa = 0.5f - 0.5f * std::cos(u + phaseOffset);   // tension

        // natural wander
        const float wander = sp.knobs[3] * 0.25f;
        kappa = juce::jlimit(0.0f, 1.2f, kappa + wander * (rng.nextFloat() * 2.0f - 1.0f) * 0.05f);

        float freq;
        if (isOwl)
        {
            // Measured from the owl reference: F0 ~478 Hz, tremulant at
            // 6.00 Hz and +/-15%, 2nd harmonic 34 dB below the fundamental
            // (essentially a pure sine), slow ~750 ms swell rather than a
            // sharp onset. The tremulant fades IN across the note.
            const float f0 = 220.0f + 700.0f * sp.knobs[0];
            const float tremRate = 1.0f + 11.0f * sp.knobs[1];
            const float tremDepth = 0.25f * sp.knobs[2];
            const float tremOnset = sp.knobs[3];
            const float tremEnv = juce::jlimit(0.0f, 1.0f,
                (t01 - tremOnset) / juce::jmax(0.05f, 1.0f - tremOnset));
            ind.subPhase += tremRate / sr;
            if (ind.subPhase >= 1.0f) ind.subPhase -= 1.0f;
            const float trem = std::sin(kTwoPi * ind.subPhase) * tremDepth * tremEnv;

            // ---- PITCH GESTURE -------------------------------------
            // Measured from a great horned owl: every note follows the
            // same arch. It starts near 305 Hz, RISES 13-15% within the
            // first quarter, holds, then falls back to about +8% by the
            // end. A static fundamental with vibrato on top cannot
            // produce that, and its absence is why the notes sat wrong
            // however well the spectrum matched. The note is a
            // trajectory, not a pitch.
            const float riseAmt = 0.35f * sp.knobs[5];          // up to +35%
            const float peakPos = juce::jlimit(0.03f, 0.75f, 0.05f + 0.7f * sp.knobs[6]);
            // Measured across three notes: the arch peaks at t = 0.45-0.54
            // (MID-note, not early) and returns essentially to baseline by
            // the end. An earlier reading put the peak at the first quarter
            // and the tremulant at 15%; separating the slow trend from the
            // fast residual showed the trend WAS most of that 15%, and the
            // real tremulant is only about 3-6%.
            const float sustain = 0.06f;
            float arch;
            if (gestureT < peakPos)
            {
                const float u = gestureT / peakPos;
                arch = std::sin(u * MathPi * 0.5f);             // smooth rise
            }
            else
            {
                const float u = (gestureT - peakPos) / juce::jmax(0.02f, 1.0f - peakPos);
                arch = 1.0f - (1.0f - sustain) * (u * u * (3.0f - 2.0f * u));
            }

            freq = f0 * (1.0f + riseAmt * arch + trem)
                * ind.carrierScale * nicheShift[slot] * midiPitchRatio * pitchMul;
        }
        else
        {
            const float lo = 300.0f + 3000.0f * sp.knobs[0];
            const float hi = lo + 200.0f + 4000.0f * sp.knobs[1];
            freq = (lo + (hi - lo) * kappa) * ind.carrierScale * nicheShift[slot] * midiPitchRatio * pitchMul;
        }

        ind.oscPhase += freq / sr;
        if (ind.oscPhase >= 1.0f) ind.oscPhase -= 1.0f;
        const float ph = kTwoPi * ind.oscPhase;

        float x;
        if (isOwl)
        {
            // near-sinusoidal, with the harmonics set explicitly in dB
            const float h2 = std::pow(10.0f, (-45.0f + 30.0f * sp.knobs[4]) / 20.0f);
            const float h3 = h2 * 0.05f;
            x = std::sin(ph) + h2 * std::sin(2.0f * ph) + h3 * std::sin(3.0f * ph);
        }
        else
        {
            // labia as a nonlinear oscillator: harmonic content grows with
            // air-sac pressure
            const float drive = 1.0f + 3.2f * beta;
            x = std::tanh(drive * std::sin(ph)) / std::tanh(drive);

            // biphonation: birds have two independent sound sources, one
            // per bronchus. Their nonlinear interaction is where the eerie
            // doubled quality of real birdsong comes from.
            const float bi = sp.knobs[6];
            if (bi > 0.01f)
            {
                ind.oscPhase2 += freq * 1.0134f / sr;
                if (ind.oscPhase2 >= 1.0f) ind.oscPhase2 -= 1.0f;
                const float x2 = std::tanh(drive * std::sin(kTwoPi * ind.oscPhase2)) / std::tanh(drive);
                x = (1.0f - bi) * x + bi * (0.5f * (x + x2) + 0.5f * x * x2);
            }
        }

        out = x * (isOwl ? 1.0f : juce::jmax(0.0f, beta));

        // trachea: a delay line with an inverting end reflection.
        // Approximated here by a resonant formant (the OEC).
        const float oec = juce::jlimit(200.0f, 8000.0f, freq * (1.0f + sp.knobs[4] * 2.0f));
        ind.aux[0].set(oec, 1.4f, currentSampleRate);
        out = 0.55f * out + 0.75f * ind.aux[0].process(out);

        out += sp.knobs[7] * 0.03f * (rng.nextFloat() * 2.0f - 1.0f) * env;
        break;
    }

    // =================================================================
    case Engine::TurbulentAeolian:
    {
        // Vortex shedding past an obstacle gives an aeolian tone at
        // f = St * U / d, with a Strouhal number near 0.2. So each
        // obstacle diameter is a narrowband peak that tracks wind speed
        // over a bed of broadband turbulence.
        const float noise = rng.nextFloat() * 2.0f - 1.0f;
        const float speed = 0.5f + 24.0f * sp.knobs[0];
        // Gusting: the model's own slow cycle, blended toward the SHARED
        // weather gust. Coupled, every wind source rises and falls
        // together — a single moving air mass rather than several
        // independent noise generators that happen to be playing.
        const float ownGust = std::sin(kTwoPi * t01 * 0.7f);
        const float shared = weather.gust * 2.0f - 1.0f;
        const float g = ownGust + (shared - ownGust) * weatherCoupling;
        const float gust = 1.0f + sp.knobs[1] * 0.85f * g;
        const float U = juce::jmax(0.3f, speed * gust);

        // Real wind passes MANY obstacle sizes at once — grass, twigs,
        // branches, gaps — so it has a whole family of aeolian tones, not
        // one. A single diameter put every peak below 100 Hz and left the
        // 0.5-2 kHz band, where most of wind's character lives, empty.
        const float dia = 0.0012f + 0.09f * sp.knobs[2];
        const float spreadOct = 0.5f + 4.5f * sp.knobs[3];
        const float envU = std::pow(U / speed, 2.2f);

        float aeo = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            const float oct = ((float)k / 3.0f - 0.5f) * spreadOct;
            const float dk = dia * std::pow(2.0f, oct);
            const float fk = juce::jlimit(25.0f, sr * 0.4f, 0.2f * U / dk)
                * ind.carrierScale * nicheShift[slot] * midiPitchRatio * pitchMul;
            ind.aux[k].set(fk, 3.0f + 14.0f * sp.knobs[4], currentSampleRate);
            aeo += ind.aux[k].process(noise) * (0.30f / (1.0f + 40.0f * dk));
        }

        // Turbulence bed in TWO layers: a strong low body plus a lighter
        // wideband hiss. One lowpass could not give both the weight and
        // the air at the same time.
        const float bedGain = 0.4f + 0.6f * sp.knobs[5];
        ind.tilt.setCutoff(150.0f + 1200.0f * sp.knobs[7], currentSampleRate);
        ind.tilt2.setCutoff(2400.0f, currentSampleRate);
        const float bed = (ind.tilt.lp(noise) * 0.85f
            + ind.tilt2.lp(noise) * 0.16f) * bedGain;
        const float hiss = noise * sp.knobs[6] * 0.15f;

        out = bed + hiss + aeo * envU;

        // Wind is DIFFUSE — you stand inside it. The reference renders it
        // from two independent noise streams, giving zero interchannel
        // correlation, where one mono source panned by the Field came out
        // almost perfectly correlated.
        if (name == "Wind")
        {
            const float nzS = rng.nextFloat() * 2.0f - 1.0f;
            ind.canopyLP.setCutoff(150.0f + 1200.0f * sp.knobs[7], currentSampleRate);
            ind.bedLP2.setCutoff(2400.0f, currentSampleRate);
            ind.sideOut += ((ind.canopyLP.lp(nzS) * 0.85f
                + ind.bedLP2.lp(nzS) * 0.16f) * bedGain
                + nzS * sp.knobs[6] * 0.15f) * 1.30f;
        }

        // Distant Road.
        //
        // A passing car is dominated by broadband tyre-road noise, not
        // engine. Doppler at 30 m/s is only about a 9% shift and is barely
        // audible; what actually sells the pass is the MOVING COMB FILTER
        // formed by interference between the direct path and the ground
        // reflection. As the car approaches, the path difference grows and
        // the comb notches sweep downward. Get that right and it is
        // unmistakable; get only the Doppler and it sounds like a cartoon.
        if (name == "Distant Road")
        {
            // tyre-road noise peaks around 800-1200 Hz
            ind.aux[2].set(juce::jlimit(200.0f, 4000.0f,
                500.0f + 1800.0f * sp.knobs[2]), 1.1f, currentSampleRate);
            float tyre = ind.aux[2].process(noise) * 1.4f + ind.tilt.lp(noise) * 0.5f;

            // engine harmonics, weak and low
            const float engineF = 40.0f + 120.0f * sp.knobs[5];
            ind.oscPhase += engineF / sr;
            if (ind.oscPhase >= 1.0f) ind.oscPhase -= 1.0f;
            tyre += sp.knobs[5] * 0.18f
                * (std::sin(kTwoPi * ind.oscPhase)
                    + 0.5f * std::sin(2.0f * kTwoPi * ind.oscPhase));

            // geometry: source and receiver heights, horizontal distance
            // sweeping past over the event
            const float hs = 0.35f;                                  // tyre contact patch
            const float hr = 1.6f;                                   // ears
            const float closest = 4.0f + 90.0f * sp.knobs[6];
            const float travel = closest * 6.0f;
            const float x = (t01 - 0.5f) * 2.0f * travel;            // -travel..+travel
            const float dHoriz = std::sqrt(x * x + closest * closest);

            const float direct = std::sqrt(dHoriz * dHoriz + (hs - hr) * (hs - hr));
            const float reflected = std::sqrt(dHoriz * dHoriz + (hs + hr) * (hs + hr));
            const float pathDiff = juce::jmax(0.0f, reflected - direct);
            const float delaySamp = pathDiff / kSpeedOfSound * sr;

            ind.combWriteSample(tyre);
            const float refl = ind.combRead(delaySamp);

            // ground reflection is partially absorbed, more so at HF
            const float absorb = 0.35f + 0.55f * (1.0f - sp.knobs[4]);
            ind.tilt2.setCutoff(1200.0f + 5000.0f * sp.knobs[4], currentSampleRate);
            out = tyre - ind.tilt2.lp(refl) * absorb;

            // 1/distance and approach brightness
            out *= 1.0f / (1.0f + dHoriz / juce::jmax(2.0f, closest));
            out *= 0.4f + 1.6f * sp.knobs[1];   // traffic density
        }
        break;
    }

    // =================================================================
    case Engine::PortedBurst:
    {
        // Deliberately does not touch ind.bank / ind.aux / Resonator /
        // ModalBank anywhere in this case — every filter here is a
        // ButterBandpass built fresh for this engine (see
        // PluginProcessor.h). STRIKE POSITION places the strike within
        // the event.
        const float strikePos = 0.08f + 0.62f * sp.knobs[0];
        const float eventSecs = (float)ind.eventLength / sr;
        const float registerMul = std::pow(2.0f, (sp.knobs[1] - 0.5f) * 2.0f) * ind.carrierScale;

        if (t01 < strikePos)
        {
            // ---- pre-rumble: quiet murmur, tremolo ramps in ----------
            const float noise = rng.nextFloat() * 2.0f - 1.0f;
            const float low = ind.tv5PreBand.process(noise);

            // A causal smoothed random walk (a leaky integrator,
            // soft-clipped to [0,1] — same trick used by the Rumble
            // model above).
            ind.tv5PreWalk += (rng.nextFloat() * 2.0f - 1.0f - ind.tv5PreWalk) * 0.002f;
            const float walk = 0.5f + 0.5f * std::tanh(ind.tv5PreWalk * 2.0f);
            float env2 = 0.15f + 0.55f * walk;

            const float timeToStrike = juce::jmax(0.0f, (strikePos - t01) * eventSecs);
            const float approach = sp.knobs[2];
            const float rate = juce::jlimit(1.5f, 7.0f, 7.0f - timeToStrike * 1.1f);
            const float depth = std::pow(juce::jlimit(0.0f, 1.0f,
                1.0f - timeToStrike / juce::jmax(1.0e-3f, strikePos * eventSecs)), 1.5f)
                * approach;
            ind.tv5TremPhase += rate / sr;
            if (ind.tv5TremPhase >= 1.0f) ind.tv5TremPhase -= 1.0f;
            const float tremolo = 1.0f + depth * 0.6f * std::sin(kTwoPi * ind.tv5TremPhase);
            out += low * env2 * tremolo;
        }
        else
        {
            if (!ind.tv5Struck)
            {
                ind.tv5Struck = true;
                ind.tv5CrackPos = 0;
                static const float thumpDelaySec[FieldIndividual::kTv5Thumps] = { 0.000f, 0.015f, 0.035f };
                for (int i = 0; i < FieldIndividual::kTv5Thumps; ++i)
                    ind.tv5ThumpElapsed[i] = -(int)(thumpDelaySec[i] * sr);   // negative = countdown to onset
            }

            // ---- the crack: 5 Butterworth copies of ONE core burst,
            // each with its own tiny onset delay and its own decay
            // stretch, exactly mirroring layered_crack_texture(). ------
            static const float crackWeight[5] = { 1.00f, 0.75f, 0.55f, 0.38f, 0.24f };
            static const float crackDelaySec[5] = { 0.000f, 0.004f, 0.009f, 0.014f, 0.020f };
            const float crackDecayBase = 0.03f + 0.30f * sp.knobs[5];   // ~0.09s at default
            if (ind.tv5CrackPos >= 0)
            {
                for (int i = 0; i < FieldIndividual::kTv5CrackBands; ++i)
                {
                    const int delaySamples = (int)(crackDelaySec[i] * sr);
                    const int corePos = ind.tv5CrackPos - delaySamples;
                    if (corePos < 0 || corePos >= (int)ind.tv5Core.size()) continue;
                    const float core = ind.tv5Core[(size_t)corePos];
                    const float filtered = ind.tv5CrackBand[i].process(core);
                    const float t = (float)corePos / sr;
                    const float decayEnv = std::exp(-t / (crackDecayBase * ind.tv5CrackStretch[i]));
                    out += filtered * decayEnv * crackWeight[i] * 0.9f;
                }
                if (++ind.tv5CrackPos > (int)ind.tv5Core.size() + (int)(0.03f * sr))
                    ind.tv5CrackPos = -1;   // all copies fully decayed; stop touching the buffer
            }

            // ---- the three low thumps: exact per-sample evaluation of
            // make_thump(), not fed through a resonator ---------------
            static const float thumpFreq[FieldIndividual::kTv5Thumps] = { 55.0f, 78.0f, 44.0f };
            static const float thumpGain[FieldIndividual::kTv5Thumps] = { 1.00f, 0.55f, 0.65f };
            static const float thumpDur[FieldIndividual::kTv5Thumps] = { 0.28f, 0.20f, 0.35f };
            const float punchFreqMul = 0.75f + 0.5f * sp.knobs[3];
            const float punchGain = 0.4f + 2.2f * sp.knobs[3];
            for (int i = 0; i < FieldIndividual::kTv5Thumps; ++i)
            {
                if (ind.tv5ThumpElapsed[i] < 0) { ++ind.tv5ThumpElapsed[i]; continue; }
                const float durS = thumpDur[i];
                const int   n = (int)(durS * sr);
                if (ind.tv5ThumpElapsed[i] > n) continue;
                const float t = (float)ind.tv5ThumpElapsed[i] / sr;
                const float f = thumpFreq[i] * punchFreqMul * registerMul;
                const float body = std::sin(kTwoPi * f * t) * std::exp(-t / (durS * 0.35f))
                    + (rng.nextFloat() * 2.0f - 1.0f) * 0.15f * std::exp(-t / (durS * 0.15f));
                out += body * thumpGain[i] * punchGain;
                ++ind.tv5ThumpElapsed[i];
            }

            // ---- tail: 5 independently-modulated low bands -----------
            static const float tailWeight[5] = { 1.00f, 0.85f, 0.65f, 0.45f, 0.30f };
            static const float tailDecayTc[5] = { 5.5f, 3.8f, 2.6f, 1.8f, 1.2f };
            static const float tailSmooth[5] = { 0.6f, 0.45f, 0.35f, 0.25f, 0.15f };
            const float tailDecayMul = 0.25f + 2.5f * sp.knobs[6];
            const float tSinceStrike = juce::jmax(0.0f, (t01 - strikePos) * eventSecs);
            for (int i = 0; i < FieldIndividual::kTv5TailBands; ++i)
            {
                auto& walk = ind.tv5TailWalk[i];
                walk += (rng.nextFloat() * 2.0f - 1.0f - walk) * (tailSmooth[i] * 0.02f);
                const float w = 0.35f + 0.65f * (0.5f + 0.5f * std::tanh(walk * 2.0f));
                const float decayEnv = std::exp(-tSinceStrike / (tailDecayTc[i] * tailDecayMul));
                const float noise = rng.nextFloat() * 2.0f - 1.0f;
                out += ind.tv5TailBand[i].process(noise) * decayEnv * w * tailWeight[i];
            }

            // ---- re-arrival swells: rolling echoes of the strike,
            // each a filtered copy of the SAME shared reburst burst ----
            const float rearrival = sp.knobs[7];
            const float reburstRate = 0.1f + 1.0f * rearrival;
            for (auto& rv : ind.tv5ReburstVoice)
            {
                if (!rv.active && rng.nextFloat() < reburstRate / sr)
                {
                    const float lo = 60.0f + 240.0f * rng.nextFloat();
                    const float hi = lo + 150.0f + 500.0f * rng.nextFloat();
                    rv.band.set(lo * registerMul, hi * registerMul, currentSampleRate);
                    rv.amp = (0.08f + 0.14f * rng.nextFloat())
                        * std::exp(-tSinceStrike / 4.0f)
                        * (0.3f + 1.4f * rearrival);
                    rv.pos = 0;
                    rv.etSecs = tSinceStrike;
                    rv.active = true;
                }
                if (rv.active)
                {
                    if (rv.pos >= (int)ind.tv5Rebursts.size()) { rv.active = false; continue; }
                    out += rv.band.process(ind.tv5Rebursts[(size_t)rv.pos]) * rv.amp;
                    ++rv.pos;
                }
            }
        }
        break;
    }
    }

    return out * env * ind.eventAmp * modelLoudnessGain(sp.phony, sp.model);
}

//==============================================================================
void SoundscapeEcologyAudioProcessor::renderSlot(int slot, juce::AudioBuffer<float>& out, int numSamples)
{
    auto& sp = slotParams[slot];
    auto& f = field[(size_t)slot];
    if (!sp.enabled) { slotActivity[slot].store(0.0f); return; }

    // (re)configure when model or population changes
    // A class change swaps the whole model list, so it must reconfigure just
    // as a model change does. Packing both into one key keeps the check cheap.
    const int modelKey = sp.phony * 100 + sp.model;
    if (lastModel[slot] != modelKey || lastPopulation[slot] != sp.population)
    {
        for (int i = 0; i < kMaxIndividuals; ++i)
            configureIndividual(slot, f[(size_t)i], i);
        lastModel[slot] = modelKey;
        lastPopulation[slot] = sp.population;
    }
    updateField(slot);

    auto* L = out.getWritePointer(0);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer(1) : L;

    const float sr = (float)currentSampleRate;
    const float coupling = sp.synchrony * 6.0f;
    const float popNorm = 1.0f / std::sqrt((float)juce::jmax(1, sp.population));

    // BIOPHONY responds to the weather: animals reduce calling during gusts and
    // rain, because the signal would be masked and calling costs energy. This
    // slows the chorus clock rather than dropping events at random, which is
    // what actually happens — they wait for a lull.
    float weatherRate = 1.0f, weatherLevel = 1.0f;
    if (weatherCoupling > 0.001f)
    {
        if (sp.phony == 0)
        {
            const float q = weather.quietening() * weatherCoupling;
            weatherRate = 1.0f - 0.75f * q;
            weatherLevel = 1.0f - 0.55f * q;
        }
        else if (sp.phony == 1 && sp.model == 3)      // Wind
        {
            // every wind slot gusts TOGETHER, which is the coherence that
            // makes separate layers read as one moving air mass
            weatherLevel = 1.0f + weatherCoupling * (weather.gust * 1.6f - 0.5f);
            weatherLevel = juce::jmax(0.15f, weatherLevel);
        }
    }

    float activity = 0.0f;

    // ---- Kuramoto mean field ---------------------------------------------
    // Weakly-coupled oscillators. At zero coupling the population is a wash;
    // at full coupling it pulses in unison; the interesting region is around
    // 0.3, where waves of synchrony form and dissolve. Combined with the
    // spatial spread this reads as sound arriving late from far away.
    float mx = 0.0f, my = 0.0f;
    for (int i = 0; i < sp.population; ++i)
    {
        mx += std::cos(f[(size_t)i].phase);
        my += std::sin(f[(size_t)i].phase);
    }
    mx /= (float)sp.population; my /= (float)sp.population;
    const float order = std::sqrt(mx * mx + my * my);
    const float psi = std::atan2(my, mx);
    chorusOrder[slot].store(order);

    const float dt = 1.0f / sr;

    for (int i = 0; i < sp.population; ++i)
    {
        auto& ind = f[(size_t)i];

        for (int s = 0; s < numSamples; ++s)
        {
            // advance the phase oscillator; a wrap is a call event
            const float prev = ind.phase;
            ind.phase += dt * (ind.omega + coupling * order * std::sin(psi - ind.phase));
            if (ind.phase >= kTwoPi)
            {
                ind.phase -= kTwoPi;
                if (!ind.active) triggerEvent(slot, ind);
            }
            else if (prev > ind.phase) { /* wrapped backwards, ignore */ }

            float dry = 0.0f;
            ind.sideOut = 0.0f;
            if (ind.active)
            {
                const float t01 = (float)ind.eventSample / (float)ind.eventLength;
                dry = generateSample(slot, ind, t01);
                if (++ind.eventSample >= ind.eventLength) ind.active = false;
            }

            // Individuals SUM, so a population of 28 was 28x louder than a
            // solo. Normalise by sqrt(population): incoherent sources add in
            // power, not amplitude, so this keeps perceived level constant as
            // the population grows.
            dry *= sp.level * popNorm * weatherLevel;
            ind.sideOut *= sp.level * popNorm * weatherLevel;
            activity = juce::jmax(activity, std::abs(dry));
            ind.spatialiseStereo(dry, ind.sideOut, L[s], R[s]);
        }
    }

    slotActivity[slot].store(juce::jlimit(0.0f, 1.0f, activity));
}

//==============================================================================
void SoundscapeEcologyAudioProcessor::handleNoteOn(int note, float velocity)
{
    note = juce::jlimit(0, 127, note);
    if (noteVelocity[(size_t)note] == 0) ++heldNotes;
    noteVelocity[(size_t)note] = juce::jlimit(1, 127, (int)(velocity * 127.0f));
    lastNoteNumber = note;
    gateTarget = 1.0f;
}

void SoundscapeEcologyAudioProcessor::handleNoteOff(int note)
{
    note = juce::jlimit(0, 127, note);
    if (noteVelocity[(size_t)note] != 0)
    {
        noteVelocity[(size_t)note] = 0;
        heldNotes = juce::jmax(0, heldNotes - 1);
    }
    if (heldNotes == 0) gateTarget = 0.0f;
}

//==============================================================================
// Build a sample-accurate gate envelope for this block.
//
// Rather than allocating a voice per note, MIDI opens and closes one global
// gate. That suits a soundscape: you are turning a place on, not playing a
// pitch. Attack and release are in seconds and can be very long, so a scene
// can fade in over several bars.
//==============================================================================
void SoundscapeEcologyAudioProcessor::buildGateEnvelope(juce::MidiBuffer& midi, int numSamples)
{
    const auto mode = (MidiMode)(int)apvts.getRawParameterValue(pid::midiMode)->load();

    // Never allocate here: this runs on the audio thread. prepareToPlay
    // reserves generously; if a host somehow exceeds that, clamp rather than
    // grow the buffer.
    numSamples = juce::jmin(numSamples, (int)gateBuffer.size());
    if (numSamples <= 0) return;

    if (mode == MidiMode::FreeRun)
    {
        // legacy behaviour: always on
        std::fill(gateBuffer.begin(), gateBuffer.begin() + numSamples, 1.0f);
        gateEnv = 1.0f;
        velocityGain = 1.0f;
        midiPitchRatio = 1.0f;
        return;
    }

    const float atkSec = apvts.getRawParameterValue(pid::midiAttack)->load();
    const float relSec = apvts.getRawParameterValue(pid::midiRelease)->load();
    const float velSens = apvts.getRawParameterValue(pid::midiVelSens)->load();
    const float pitchAmt = apvts.getRawParameterValue(pid::midiPitch)->load();
    const int   root = (int)apvts.getRawParameterValue(pid::midiRoot)->load();

    // per-sample coefficients for an exponential-ish approach
    const float atkInc = 1.0f / juce::jmax(1.0f, (float)(atkSec * currentSampleRate));
    const float relInc = 1.0f / juce::jmax(1.0f, (float)(relSec * currentSampleRate));

    int sample = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int pos = juce::jlimit(0, numSamples, meta.samplePosition);

        // fill up to this event
        for (; sample < pos; ++sample)
        {
            gateEnv += (gateTarget > gateEnv) ? atkInc : -relInc;
            gateEnv = juce::jlimit(0.0f, 1.0f, gateEnv);
            gateBuffer[(size_t)sample] = gateEnv * gateEnv * (3.0f - 2.0f * gateEnv);
        }

        if (msg.isNoteOn())
        {
            const bool wasSilent = (heldNotes == 0);
            handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            if (wasSilent) retriggerPending = true;
        }
        else if (msg.isNoteOff())    handleNoteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            noteVelocity.fill(0); heldNotes = 0; gateTarget = 0.0f;
        }
    }

    for (; sample < numSamples; ++sample)
    {
        gateEnv += (gateTarget > gateEnv) ? atkInc : -relInc;
        gateEnv = juce::jlimit(0.0f, 1.0f, gateEnv);
        gateBuffer[(size_t)sample] = gateEnv * gateEnv * (3.0f - 2.0f * gateEnv);
    }

    // velocity of the most recent held note
    const int vel = juce::jlimit(0, 127, noteVelocity[(size_t)lastNoteNumber]);
    const float velNorm = (vel > 0) ? (float)vel / 127.0f : velocityGain;
    velocityGain = (1.0f - velSens) + velSens * velNorm;

    // transposition, only in Gated + Pitch
    midiPitchRatio = (mode == MidiMode::GatedPitch)
        ? std::pow(2.0f, (float)(lastNoteNumber - root) / 12.0f * pitchAmt)
        : 1.0f;
}

//==============================================================================
// Stagger the chorus phases so the first sounds arrive promptly after a
// note-on. Several presets run at 0.06-0.3 events per second per individual,
// which meant pressing a key could produce up to 17 seconds of silence.
//==============================================================================
void SoundscapeEcologyAudioProcessor::retriggerField(int slot)
{
    auto& sp = slotParams[slot];
    auto& f = field[(size_t)slot];
    const int pop = juce::jlimit(1, kMaxIndividuals, sp.population);

    for (int i = 0; i < pop; ++i)
    {
        auto& ind = f[(size_t)i];
        // Spread the population across the first cycle, biased early, so the
        // scene establishes at once rather than trickling in. Individual 0
        // fires immediately.
        const float u = (pop > 1) ? (float)i / (float)pop : 0.0f;
        ind.phase = kTwoPi * (1.0f - std::pow(u, 0.65f) * 0.92f);
        if (i == 0) ind.phase = kTwoPi;      // wraps on the next sample
        ind.active = false;
    }
}

//==============================================================================
void SoundscapeEcologyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    buffer.clear();
    readParameters();
    applyNiche();
    weather.update(numSamples, currentSampleRate);
    buildGateEnvelope(midiMessages, numSamples);

    if (retriggerPending)
    {
        for (int s = 0; s < kNumSlots; ++s) retriggerField(s);
        retriggerPending = false;
    }

    if (slotBuffer.getNumSamples() < numSamples)
        slotBuffer.setSize(2, numSamples, false, false, true);

    // When the gate is fully shut and nothing is held, skip generation
    // entirely — but still run the habitat so its tail decays naturally
    // rather than being cut off.
    const bool gateOpen = (gateEnv > 1.0e-4f) || (gateTarget > 0.0f);

    if (gateOpen)
    {
        for (int s = 0; s < kNumSlots; ++s)
        {
            slotBuffer.clear(0, 0, numSamples);
            slotBuffer.clear(1, 0, numSamples);
            renderSlot(s, slotBuffer, numSamples);
            for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
                buffer.addFrom(ch, 0, slotBuffer, ch, 0, numSamples);
        }

        // apply the gate BEFORE the habitat, so reverb rings on after release
        for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
        {
            auto* d = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s)
                d[s] *= gateBuffer[(size_t)s] * velocityGain;
        }
    }

    habitat.process(buffer);

    // width + output
    //
    // Master loudness calibration: even after the per-model fixes above, a
    // typical populated scene (Cricket Field at Dusk) measured -28.5 LUFS
    // integrated -- reasonable for a strictly ecological level, but quiet
    // next to normal listening levels and quieter than requested. This is a
    // FIXED multiplier, not user-facing: the Output knob's 0..2x range still
    // behaves as an intuitive trim on top of it, its default of 1.0 just now
    // means something louder than before. tanh() below still soft-limits
    // gracefully, so the loudest presets (Metal Bar, populated scenes) gain
    // a little gentle saturation rather than hard clipping.
    constexpr float kMasterLoudness = 4.2f;   // ~+12.5 dB

    if (buffer.getNumChannels() > 1)
    {
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getWritePointer(1);
        for (int s = 0; s < numSamples; ++s)
        {
            const float w = widthAmount.getNextValue();
            const float mid = 0.5f * (L[s] + R[s]);
            const float side = 0.5f * (L[s] - R[s]) * w;
            const float g = outputGain.getNextValue() * kMasterLoudness;
            L[s] = std::tanh((mid + side) * g);
            R[s] = std::tanh((mid - side) * g);
        }
    }
    else
    {
        auto* L = buffer.getWritePointer(0);
        for (int s = 0; s < numSamples; ++s)
            L[s] = std::tanh(L[s] * outputGain.getNextValue() * kMasterLoudness);
    }
}

// Subtle per-slot randomisation.
//
// Deliberately gentle: every parameter is nudged relative to where it already
// sits, by a fraction of its own range, rather than being redrawn uniformly.
// Scattering values across full range produces noise, not a soundscape --
// most random parameter sets in a physical model are not a plausible animal.
// Model, class, enable, level and population are excluded because those are
// structural choices; randomising them would change WHAT the scene is rather
// than shading how it sounds.
void SoundscapeEcologyAudioProcessor::randomiseSlot(int slot, float amount)
{
    auto nudge = [this, amount](const juce::String& id)
        {
            auto* p = apvts.getParameter(id);
            if (p == nullptr) return;
            const auto range = p->getNormalisableRange();
            const float cur01 = p->getValue();                      // already 0..1
            const float delta = (rng.nextFloat() * 2.0f - 1.0f) * amount;
            const float next01 = juce::jlimit(0.0f, 1.0f, cur01 + delta);
            p->beginChangeGesture();
            p->setValueNotifyingHost(next01);
            p->endChangeGesture();
            juce::ignoreUnused(range);
        };

    // the eight physics knobs: the actual voice of the model
    for (int k = 0; k < kNumModelKnobs; ++k)
        nudge(pid::knob(slot, k));

    // articulation: timing and shape
    nudge(pid::rate(slot));
    nudge(pid::phrase(slot));
    nudge(pid::rhythm(slot));
    nudge(pid::jitter(slot));
    nudge(pid::swing(slot));
    nudge(pid::attack(slot));
    nudge(pid::decay(slot));
    nudge(pid::duration(slot));

    // the field: placement, but not how many there are
    nudge(pid::spread(slot));
    nudge(pid::distance(slot));
    nudge(pid::diversity(slot));
    nudge(pid::synchrony(slot));
    nudge(pid::parallax(slot));
    nudge(pid::elevation(slot));

    // restage so the change is audible straight away rather than on the next
    // slow chorus cycle
    requestRetrigger();
}

void SoundscapeEcologyAudioProcessor::handleAsyncUpdate()
{
    const float t = pendingMorph.exchange(-1.0f);
    if (t >= 0.0f && presets != nullptr)
        presets->applyMorph(t);
}

//==============================================================================
juce::AudioProcessorEditor* SoundscapeEcologyAudioProcessor::createEditor()
{
    return new SoundscapeEcologyAudioProcessorEditor(*this);
}

void SoundscapeEcologyAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // store the draw canvases alongside the parameters
    juce::ValueTree curves("CURVES");
    for (int s = 0; s < kNumSlots; ++s)
    {
        juce::ValueTree slot("SLOT" + juce::String(s));
        juce::String p, a;
        for (int i = 0; i < kDrawPoints; ++i)
        {
            p += juce::String(pitchCurves[(size_t)s][(size_t)i], 4) + " ";
            a += juce::String(ampCurves[(size_t)s][(size_t)i], 4) + " ";
        }
        slot.setProperty("pitch", p.trim(), nullptr);
        slot.setProperty("amp", a.trim(), nullptr);
        curves.addChild(slot, -1, nullptr);
    }
    state.addChild(curves, -1, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SoundscapeEcologyAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml(*xml);
    auto curves = tree.getChildWithName("CURVES");
    if (curves.isValid())
    {
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto slot = curves.getChildWithName("SLOT" + juce::String(s));
            if (!slot.isValid()) continue;
            auto parse = [](const juce::String& str, std::array<float, kDrawPoints>& dest)
                {
                    auto tokens = juce::StringArray::fromTokens(str, " ", "");
                    for (int i = 0; i < juce::jmin(kDrawPoints, tokens.size()); ++i)
                        dest[(size_t)i] = tokens[i].getFloatValue();
                };
            parse(slot.getProperty("pitch").toString(), pitchCurves[(size_t)s]);
            parse(slot.getProperty("amp").toString(), ampCurves[(size_t)s]);
        }
        tree.removeChild(curves, nullptr);
    }
    apvts.replaceState(tree);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoundscapeEcologyAudioProcessor();
}