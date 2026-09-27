# 83ChorusVerb

A four-stage stereo effects plugin — **Chorus → Delay → Phaser → Reverb** —
lifted unchanged from VirtualDX7's global FX section and repackaged as its
own standalone insert effect. The (19)83 comes from the release year of the DX7.

## Signal chain

The order is, matching the synth:

```
IN → Chorus → Delay → Phaser → Reverb → OUT
```

Each stage can be switched off independently.

## The shared piece every effect is built on: `FxBase`

Every effect (`Chorus`, `Delay`, `Phaser`, `Reverb`) inherits from a small
base class that handles two things so the individual DSP classes don't have
to:

- **Click-free on/off.** Flipping a hard bypass boolean mid-block causes an
  audible click. `FxBase` instead ramps a `juce::SmoothedValue<float>`
  (`enableGain`) between 0 and 1 over ~20 ms whenever the switch is toggled,
  and crossfades the wet signal in/out through that ramp. `isActive()`
  reports `true` until the ramp has *fully* settled at zero, which is what
  lets an idle effect skip its own `processInternal()` entirely — an OFF
  chorus costs nothing.
- **Dry/Wet.** The `Mix` knob is its own `SmoothedValue`, ramped over ~50 ms,
  so dragging it doesn't zipper.

Both ramps multiply together in `mix()`, which every effect calls once per
sample to blend its wet output back over the untouched dry signal.

## Chorus

A single modulated delay line per channel. An LFO sweeps the read position
around a short base delay (`Delay` knob, 1–20 ms); `Depth` sets how far it
sweeps, `Rate` how fast. `Spread` offsets the left LFO phase from the right
by up to 180°, which is what gives the effect stereo width — at 0% both
channels wobble in lockstep (mono-compatible chorus); at 100% they're in
antiphase (as wide as it gets).

## Delay

Two independent delay lines (`Time L`, `Time R`), each with its own
one-pole high-pass filter *inside* the feedback loop (`HPF`). Filtering
inside the loop rather than on the output means each repeat loses a little
low end compared to the one before it — real analog echo units behave this
way, and it stops the feedback path from building up an undifferentiated
low-frequency wash after a few repeats. Delay time changes are smoothed
rather than jumped to, avoiding a pitch-bend/zipper artifact when turning 
the knob.

## Phaser

2–12 first-order allpass stages in series per channel (`Stages`), swept by
an LFO around a `Center` frequency by up to ±2 octaves at full `Depth`.
`Feedback` taps the output of the last stage back into the input of the
first, which sharpens the notches into the classic "jet flanging" character
as it increases.

## Reverb

This is an 8-line Feedback Delay Network (FDN) and the specific numbers chosen
here are doing real work. A few things stack together to produce the lush result:

### 1. Eight lines, mutually incommensurate delay times

```cpp
static constexpr double kLineMs[8] =
{ 29.7, 37.1, 41.1, 43.7, 53.3, 61.9, 71.3, 79.7 };
```

These eight delay times share no small common factors. If they did (say,
10, 20, 30, 40 ms), their echoes would reinforce each other at regular
intervals and you'd hear a distinct, metallic pitch — a comb filter you can
sing along to. Spacing them irregularly instead spreads the reflections'
constructive/destructive interference across the whole spectrum unevenly,
which is exactly what a real room's early-to-late reflections do. This is
the single biggest reason the tail sounds like *space* rather than *pitch*.

### 2. A Householder feedback matrix

Each line's output feeds into **every** line, through an 8×8 Householder reflection matrix:

```cpp
const float h = 2.0f / numLines;
for (int l = 0; l < numLines; ++l)
    lpState[l] += lpCoeff * ((lineOut[l] - h * sum) - lpState[l]);
```

`y_i = x_i − (2/N)·Σx` is a energy-preserving matrix (it doesn't add or
remove level, only redistributes it), and it fully mixes all eight lines
into each other on every pass. Two consequences: the reverb is lossless
except where damping/feedback deliberately moves energy out (so `Feedback`
cleanly controls decay time without the tail collapsing unpredictably), and
every line quickly carries a bit of every other line — which is what turns
eight discrete delay taps into one smoothly-blended wash instead of eight
audible echoes.

### 3. Input diffusion before the tank

```cpp
for (int i = 0; i < 4; ++i) {
    difL = diffusers[0][i].process(difL, 0.62f);
    difR = diffusers[1][i].process(difR, 0.62f);
}
```

Before the dry signal ever reaches the FDN, it passes through four
allpass filters in series (4.7, 3.6, 12.7, 9.3 ms, coefficient 0.62). An
allpass smears a transient in time without changing its frequency content —
so a sharp snare hit or plucked note goes in and a soft, textureless little
puff of noise comes out the other side, which is what gets injected into
the tank. That's the difference between a reverb that sounds like eight
repeats of your dry signal and one that sounds like your signal fell into
a cloud.

### 4. Per-line modulation, each line at its own phase

```cpp
lfoPhase[l] = (double)l / numLines;   // spread LFO phases across lines
...
const double dly = kLineMs[l] * 0.001 * sampleRate * size + lfo * modDepth;
```

Every line's delay length wobbles slightly (`Mod Depth`, at `Mod Rate`),
and — critically — each of the eight lines starts at a *different* point
in that LFO cycle (`l/8` around the circle). A static FDN of this size can
ring at fixed resonant frequencies determined by the delay lengths; slowly
and independently detuning each line prevents any single resonance from
sitting still long enough to be heard as a pitch, and it's also what gives
a long tail that shimmering, faintly chorused "alive" quality rather than
sounding like a frozen, static sample of reverb.

### 5. Frequency-dependent damping

```cpp
const float lpCoeff = frozen ? 1.0f : jlimit(0.05f, 1.0f, 1.0f - damping * 0.9f);
lpState[l] += lpCoeff * ((lineOut[l] - h * sum) - lpState[l]);
```

A one-pole lowpass sits inside each line's feedback path, so high
frequencies move out faster than low ones as the tail decays — the same
thing that happens in a real room as air and soft furnishings absorb
treble before bass. `Damping` controls how aggressively; low damping gives
a bright, glassy plate-like tail, high damping gives a damp, warm hall.
Without this, an FDN's tail decays at one uniform rate across the whole
spectrum and starts to sound synthetic very quickly.

### 6. Stereo decorrelation without hard panning

```cpp
if (l & 1) wetL += lineOut[l]; else wetR += lineOut[l];
```

The four odd-numbered lines feed the left output, the four even ones feed
the right. Because all eight lines are already different lengths, modulated
at different phases, and cross-mixed by the Householder matrix, left and
right end up carrying genuinely decorrelated signals — real stereo width —
without ever explicitly panning anything or copying L to R.

### 7. Freeze

```cpp
const float fb = frozen ? 1.0f : jlimit(0.0f, 0.98f, feedbackAmt * 0.8f);
const float lpCoeff = frozen ? 1.0f : ...;
difL = difR = 0.0f;   // no new input while frozen
```

Freeze sets feedback to exactly unity and damping's lowpass coefficient to
1 (no high-frequency loss), and stops feeding new input into the tank. The
Householder matrix being lossless is what makes this work cleanly: with
nothing transporting energy out and nothing new coming in, whatever was in the
tank the instant you hit Freeze sustains indefinitely, unchanged, as an
infinite pad/drone.

## Parameter reference

All parameters live in one `juce::AudioProcessorValueTreeState`
(`ChorusVerbAudioProcessor::apvts`), added by `FxChain::addParameters()`, so
every knob, switch and the Phaser's stage-count menu are host-automatable
out of the box.

| Section | Parameter | Range | Default |
|---|---|---|---|
| Chorus | On | on/off | off |
| | Rate | 0.1 – 10 Hz (log) | 0.5 Hz |
| | Depth | 0 – 100 % | 50 % |
| | Delay | 1 – 20 ms | 7 ms |
| | Spread | 0 – 100 % | 50 % |
| | Mix | 0 – 100 % | 50 % |
| Delay | On | on/off | off |
| | Time L | 20 – 2000 ms (log) | 250 ms |
| | Time R | 20 – 2000 ms (log) | 375 ms |
| | Feedback | 0 – 1.2 | 0.3 |
| | High-Pass | 20 – 2000 Hz (log) | 100 Hz |
| | Mix | 0 – 100 % | 35 % |
| Phaser | On | on/off | off |
| | Rate | 0.05 – 10 Hz (log) | 0.5 Hz |
| | Depth | 0 – 100 % | 50 % |
| | Center | 100 – 10000 Hz (log) | 800 Hz |
| | Feedback | 0 – 100 % | 20 % |
| | Mix | 0 – 100 % | 50 % |
| | Stages | 2 / 4 / 6 / 8 / 10 / 12 | 4 |
| Reverb | On | on/off | off |
| | Size | 0.1 – 8 (log) | 1.0 |
| | Feedback | 0 – 1.2 | 0.7 |
| | Damping | 0 – 100 % | 40 % |
| | Mod Rate | 0.01 – 8 Hz (log) | 0.25 Hz |
| | Mod Depth | 0 – 50 | 4.0 |
| | Freeze | on/off | off |
| | Mix | 0 – 100 % | 35 % |

Frequency- and time-like knobs use a log-skewed `NormalisableRange`
(`FxChain::logRange()`), so the knob spends its travel where the ear
actually resolves detail instead of linearly across a huge span.

## Provenance

The DSP here is unmodified from VirtualDX7's `FxChain.h`, which was itself
ported from the Aquanode Modular effect modules (`ChorusModule`,
`DelayModule`, `PhaserModule`, `AquatonReverbModule`). Only the surrounding
plugin (parameter IDs, the `AudioProcessor`/`AudioProcessorEditor`, and the
UI classes) is new — built to expose those same four algorithms as their
own standalone insert effect, with the Chord/Harmonizer section (a MIDI
note-doubler, not an audio effect) left out entirely.
