# VirtualK5

**VirtualK5** is a free additive synthesizer, built as a VST3 plugin and
standalone `.exe` for Windows, with full JUCE/C++ source included for
cross-platform builds.

It is a **loose remake of the Kawai K5**, not an emulation. The K5's signal path
and its way of thinking about sound, dozens of individually enveloped harmonics
rather than a waveform pushed through a filter, are the starting point, but no
attempt is made to match the original machine's numbers, its ROM behaviour or
its front panel. Nothing from the hardware is included or reproduced. Think of
it as a fun, simple additive synth that happens to be laid out the way a K5 is.

---

## Signal path

Following the K5's chain, one stage at a time:

| Stage | Meaning | What it does here |
| --- | --- | --- |
| **DFG** | Digital Frequency Generator | Pitch: base note, detune, pitch envelope, LFO vibrato. |
| **DHG** | Digital Harmonic Generator | 63 harmonics per source. Each harmonic has a static level and is assigned to one of **4 envelope busses**, so a handful of envelopes animate the whole spectrum. A harmonic **MODE** mask (All / Odd / Even / Octave / Fifth) selects which partials sound at all. |
| **DDF** | Digital Dynamic Filter | Dynamic filter. On the real machine this is not a filter across the summed signal — it scales individual harmonics, and so does this, which is both more faithful and cheaper than a biquad per voice. |
| **DDA** | Digital Dynamic Amplifier | Amplitude envelope, plus LFO tremolo. |
| **DFT** | Digital Formant Filter | An 11-band spectral shape applied across the harmonic series. |

**Two sources** (S1 and S2) stack per note, each with its own full parameter set,
level and detune. Polyphony is **16 voices**.

Everything above the raw oscillator sum runs at a control rate of 32 samples,
with per-sample linear interpolation of harmonic amplitudes — the original
updated its harmonic levels in discrete steps too, so this is in keeping rather
than a shortcut. No heap allocation happens after the constructor, so the whole
engine is safe to drive from the audio thread.

---

## Control reference

### Per source (S1 / S2, identical sets)

**Level & tuning**
- **Level** — source level, 0–1.
- **Detune** — ±50 cents.

**Pitch envelope**
- **Attack / Decay / Sustain / Release**
- **Depth** — 0–24 semitones.

**Amp envelope (DDA)**
- **Attack / Decay / Sustain / Release**

**Dynamic filter (DDF)**
- **Cutoff** — 20 Hz to 18 kHz.
- **Resonance** — 0 to 0.99.
- **Env Amount** — bipolar, −1 to +1.
- **Slope** — 12 or 24 dB/oct.
- **Attack / Decay / Sustain / Release** for its own envelope.

**Harmonics (DHG)**
- **Harmonic Mode** — All / Odd / Even / Octave / Fifth.
- **Harmonic Tilt** — bipolar spectral tilt across the 63 partials.
- **Harm Bus 1–4**, each with **Attack / Decay / Sustain / Release**. Every
  harmonic is assigned to one of these four busses, which is how four envelopes
  animate 63 partials.

### Global

**Master**
- **Master Gain**

**LFO**
- **Shape** — Triangle / Saw / Square / Random.
- **Rate** — 0.02 to 20 Hz.
- **Delay** — 0 to 5 s before the LFO fades in.
- **→ Vibrato** — 0 to 12 semitones.
- **→ Tremolo** — 0 to 1.
- **→ Filter** — 0 to 4 octaves.

**DFT (shared 11-band formant filter)**

Eleven bands, each with **Gain**, **Freq** and **Q**. Defaults sit at 250, 400,
600, 850, 1150, 1500, 1950, 2500, 3200, 4100 and 5300 Hz — a formant-ish spread
you can pull anywhere between 80 Hz and 12 kHz. All gains start at zero, so the
DFT does nothing until you dial something in.

---

## Presets

Patches are saved with the host session as normal. On top of that there is a
user-facing preset browser writing standalone `.xml` files to:

```
<user application data>/K5Additive/Presets/
```

Five factory presets are installed there the first time the plugin runs: **Init**,
**Glass Bells**, **Warm Growl Bass**, **Airy Formant Pad** and **Metallic Pluck**.

The file format is deliberately simple and hand-editable — not a raw dump of
normalised 0–1 values:

```xml
<K5Preset name="Glass Bells">
  <PARAM id="s1_cutoff" value="9000"/>
  <PARAM id="s1_harmMode" value="3"/>   <!-- choice index -->
  ...
</K5Preset>
```

Any parameter not mentioned is left at its default, so a preset only needs to
specify what makes it distinctive.

---

## Build

A `.jucer` project is included. Open it in the Projucer (with the global JUCE
path), pick an exporter, save to generate the IDE project, and build.

> The module list includes `juce_audio_processors_headless`, which recent JUCE 8
> releases (8.0.x) require as a dependency of `juce_audio_processors`. If you are
> on an older JUCE that predates that module, remove it from the Modules list in
> the Projucer.

## Project layout

```
VirtualK5/
  Source/
    PluginProcessor.{h,cpp}   parameters, voice allocation, processor
    PluginEditor.{h,cpp}      the UI
    K5SynthVoice.h            the additive engine (DFG/DHG/DDF/DDA/DFT)
    PresetManager.h           the .xml preset browser
    FactoryPresets.h          the five bundled starter patches
  VirtualK5.jucer
```

---

## Credits & license

Written with the assistance of **Claude, the AI by Anthropic**.

AGPLv3 as any freely distributed JUCE plugin.

This project is not affiliated with or endorsed by Kawai. "K5" is used here only
descriptively, to say where the idea came from. No firmware, sample data,
wavetables or presets from the original instrument are included, reproduced or
distributed — the engine is written from scratch and the architecture above is
the only thing borrowed.
