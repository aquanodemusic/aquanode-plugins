# SpectralLock

A polyphonic filterbank resynthesizer / retuner — a "colorbass" style effect
that splits incoming audio into narrow log-spaced bands (36 bands per
octave, ~33 cent resolution), tracks each band's amplitude, and re-sings that amplitude through its own sine oscillator retuned onto a scale, a chord matrix, or the notes you're currently holding on a MIDI keyboard. The result: whatever you play in — a bassline, a chord, a vocal, a loop — comes back out locked to musical pitches, with the original transient/noise content still under your control.

There is zero added processing latency (the only "delay" is the envelope followers' own inertia).

---

## Signal path

```
input
  ├─ low split  (below Range Low)   ──── dry, muteable
  ├─ high split (above Range High)  ──── dry, muteable
  └─ mid band (Range Low..High)
        → filterbank (2 cascaded bandpass biquads per band)
        → per-band RMS envelope follower
        → per-band sine oscillator, retuned via Root/Matrix or MIDI
        → summed, panned per band
        ↓
     AMOUNT blends tonal (retuned) vs. the raw noise/transients
     MIX blends wet against the untouched dry signal
        ↓
     output
```

Only the *mid* band (between **Range Low** and **Range High**) goes through
the retuning engine. The low and high tails pass through dry, and can each be
muted independently.

---

## Knobs

| Knob | Range | Default | What it does |
|---|---|---|---|
| **Mix** | 0 – 100% | 100% | Dry/wet. |
| **Amount** | 0 – 100% | 100% | Tonal vs. noise. At 100%, the output is fully the retuned oscillator bank. Lower it and more of the original noise/transient content bleeds back in — useful for keeping percussive attack while still hearing the retune underneath. |
| **Isolate** | 0 – 100% | 0% | A per-band noise gate on the oscillator bank, plus a hidden overall gate on the whole effect. Raising it gates out quiet/transient content so only sustained, clearly-pitched material gets resynthesized — good for isolating a held note or drone out of a busier signal. |
| **Glide** | 0 – 100% | 0% | Portamento time for each band sliding to its new target pitch (roughly 0.4 ms at 0% up to 500 ms at 100%, skewed so the useful range is spread across the low end of the knob). |
| **Bend** | −100 to +100% | 0% | Bipolar pitch bend, scaled by **Bend Range**. |
| **Bend Range** *("+ / −")* | 1 – 24 semitones | 2 | How many semitones **Bend** covers at full deflection. |
| **Width** | 0 – 200% | 100% | Stereo spread of the oscillator bank. Each band has a fixed pseudo-random pan position; Width scales how far it's allowed to sit from center. |
| **Pitch** | −24 to +24 st | 0 | Global transpose applied after retuning. |
| **Tilt** | −100 to +100% | 0% | Spectral tilt pivoting around 700 Hz. Negative tilts the balance toward the low bands, positive toward the high bands. |
| **Shimmer** | 0 – 100% | 0% | Blends in a phase-locked octave-up partner tone for every band oscillator — an airy shimmer on top of the fundamental retune. |
| **Spray** | 0 – 100% | 0% | Adds a small fixed, per-band pseudo-random detune scatter (stable across sessions/reloads) for a chorused, slightly-out analog feel and modulation. |
| **Level** | 0 – 200% | 100% | Trim on the resynthesized (tonal) sum, before it's blended with the dry low/high tails and Mix. |

## Toggles

| Toggle | Default | What it does |
|---|---|---|
| **Mute Low** | Off | Removes the dry, unprocessed content *below* Range Low from the output — leaves only the retuned mid band (and the high tail, if not also muted). |
| **Mute High** | Off | Same, for the dry content *above* Range High. |
| **MIDI In** | Off | When on *and* you're holding at least one note, every band's target pitch snaps to whichever held note it's closest to (magnetized); a band more than half an octave from any held note drops out entirely. When off (or nothing is held), retuning falls back to **Root** + the **Matrix**. |
| **Freeze** | Off | Holds every band's envelope at its current level indefinitely — freezes whatever's currently sounding into a sustained drone/pad, regardless of what continues to come in. |

## Root & The Matrix

Above the 12×12 grid is a root-note strip — this sets the tonal center
everything below is relative to (only used when **MIDI In** is off, or no
notes are currently held).

The matrix itself remaps **incoming pitch class → outgoing pitch class**:
columns are the note a band would naturally be at (relative to Root),
rows are what it actually gets retuned to. Click/drag cells to build a
custom mapping by hand, or pick a **scale preset** from the dropdown above
it (Chromatic, Major, Minor, Harmonic Minor, Dorian, Phrygian, Lydian,
Mixolydian, Pentatonic Major, Pentatonic Minor, Whole Tone, or "Octaves" —
which pulls everything to the root) to auto-populate it; off-scale tones are
pulled to their nearest in-scale degree.

## Band View

The live display along the top — one stripe per active oscillator band,
coloured by pitch, brightening with that band's current amplitude and gate
state. It's a good way to see Isolate, Freeze, and Range Low/High actually
doing something in real time.

---

## Quick start

You can leave all settings at their default, you only need to pick a **scale preset**, I recommend Pentatonic Minor for a shimmering, slightly positive but deep and calmer character (if you can call a colorbass sound character that). Use the Spray for interesting modulation.
