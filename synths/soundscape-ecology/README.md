# Soundscape Ecology VST

![soundscape ecology gui](assets/GUI.png)

*🌿 A procedural nature synthesizer. Nothing here is a sample. 🌴*

Every landscape sings somewhere, whether actively or passively. The most prominent species to create natural atmospheres in the audio world are crickets, frogs and birds, which form the **Biophony** as one of the three pillars of Soundscape Ecology - both the name of the academic field, and the name of this VST plugin. The other two besides Biophony are **Geophony**, the sound of inorganic, moving and interacting elements, and, self-centered as we are, **Anthropony**, the sound humans make and design. These are the terms used in the soundscape-ecology literature, as cited and described below.

---

# What it is

Every sound this plugin makes is generated from a physical model of the thing that made it. Crickets are synthesised by simulating a scraper dragging across a file of wing teeth. Cicadas by simulating a row of stiff ribs buckling in sequence. Waves by simulating the ringing of entrained air bubbles. Passing cars by computing the interference between the direct sound path and its reflection off the ground.

SoundscapeEcology does not to produce a perfect 1:1 replica of a real animal's calls or the sound of a storm, river or machine. The instrument explores the mathematical mechanisms and descriptions of organic and inorganic rhythms, spatial cues and interactions that make a soundscape feel alive.

Some models are based on measurements from field recordings; others are deliberately more approximate. The engine is very experimental and rough around the edges.

It basically answers:

> *"What happens when we model some of the things that make a frog sound like a frog?"*

---

# Structure: the three phonies

Soundscape ecology divides all sound in a landscape into three source classes.
The plugin has one slot for each.

| Slot | Class | Contents |
|---|---|---|
| I | **Biophony** | Cricket, Katydid, Cicada, Frog, Songbird, Owl |
| II | **Geophony** | Rain, Surf, Stream, Wind, Fire |
| III | **Anthrophony** | Distant Road, Plate Shear, Plate Impact, Bell, Machinery |

This taxonomy comes from Krause (1987), who introduced
*biophony* and *geophony*, later adding *anthrophony* with Stuart Gage; it was
formalised as a research framework by Pijanowski et al. (2011).

There are three generator slots, but the slots are deliberately **not permanently assigned to a category**. Click a slot heading to cycle:

**Biophony → Geophony → Anthrophony → Biophony**

The heading itself is the control. The model list, description and accent colour change with it.

This means a scene can be conventional with birds, rain and distant machinery mixed together, or deliberately niche: three frogs, three kinds of rain, or a full mixture of insects, birds and frogs.

---

# Architecture

```
  ┌──────────────┬──────────────┬──────────────┐
  │   SLOT I     │   SLOT II    │   SLOT III   │
  │  Biophony    │  Geophony    │ Anthrophony  │  interchangable
  ├──────────────┼──────────────┼──────────────┤
  │ Generator    │ Generator    │ Generator    │  concerned with PHYSICS
  │ Articulation │ Articulation │ Articulation │  concerned with RHYTHM
  │ The Field    │ The Field    │ The Field    │  concerned with POPULATION
  │ Contour      │ Contour      │ Contour      │  concerned with ENVELOPES
  └──────┬───────┴──────┬───────┴──────┬───────┘
         └──────────────┼──────────────┘
                        ▼
             ┌─────────────────────┐
             │   NICHE ALLOCATOR   │
             ├─────────────────────┤
             │  HABITAT (Feedback) │
             └─────────────────────┘
```

## Three engines cover everything

| Engine | Structure | Models |
|---|---|---|
| **A** | stochastic excitation → resonant body | insects, frogs, rain, surf, stream, fire, plates, bells, machinery |
| **B** | nonlinear oscillator → vocal tract | songbird, owl |
| **C** | turbulence + aeolian resonance | wind, tyre-road noise |

### 1. Physics

The model-specific controls describe the mechanism being approximated:

- wing resonance
- tymbal vibration
- vocal folds
- vocal sacs
- bubbles
- plate modes
- vortex shedding
- friction
- impacts
- machinery resonance

### 2. Articulation

These controls determine **when** the source speaks:

| Control | Function |
|---|---|
| **Rate** | Events per second |
| **Phrase** | Syllables inside an event |
| **Rhythm** | Smooth tremolo → sharply articulated syllables |
| **Jitter** | Timing irregularity |
| **Hold** | Sustain before a syllable train |
| **Attack** | Fade-in time |
| **Decay** | Fade-out time |
| **Length** | Duration of an event |
| **Level** | Slot output level |

These controls are particularly important for biological models. Species identity 
often lives as much in temporal organisation as in spectral content.

### 3. The Field

A cricket is already somewhat difficult to model. A cricket *field* is the interplay 
between many individuals, arguably a bit less difficult of a problem, but it is where
realism lives. What you hear in a real recording is, say, forty animals at forty
distances, each slightly out of sync, each low-passed by however much air sits
between it and the microphone.

The Field turns a single synthesised source into a population:

| Control | Function |
|---|---|
| **Population** | Number of individuals |
| **Spread** | Spatial separation |
| **Distance** | Distance of the nearest individual |
| **Diversity** | Individual variation |
| **Synchrony** | Coupling between individuals |
| **Parallax** | Stereo movement from spatial distance |
| **Elevation** | Height above ground in metres |

Per individual: inverse-distance gain, propagation delay, distance- and
humidity-dependent HF air absorption, and a pan width that narrows with distance
(the parallax cue that makes a field feel deep rather than merely wide).

Population is normalised by the square root of the number of individuals, preventing 
a population of 28 from simply becoming 28 times louder than a solo source.

**Synchrony** couples the population as weakly-coupled phase oscillators
(Kuramoto). At zero coupling the field is a wash; at full coupling it pulses in
unison; the interesting region is around 0.3, where waves of synchrony form and
dissolve. An observation from building this plugin is that
synchrony interacts with *spatial spread*. A tightly-coupled field that is also
spatially spread out sounds far more alive than either alone, because the
propagation delays smear the synchrony geographically — waves of sound arriving
late from far away.

### 4. Field Map

The Field Map is a top-down representation of the current population.

Each dot represents an individual. Individuals brighten and pulse when they call.

The map reflects:

- **Population**
- **Spread**
- **Distance**
- **Parallax**

Scroll to zoom, drag to pan, click twice to reset the view. Distance rings are labelled in metres.

The spatial arrangement shown by the map corresponds to the spatialisation used by the audio engine rather than being purely decorative.

### The Niche Allocator

Krause's **Acoustic Niche Hypothesis**: species in a healthy habitat partition
the frequency spectrum so their calls don't mask each other. That is *why* a
dawn chorus sounds coherent rather than muddy. The NICHE control nudges each
slot's carrier away from territory the others already occupy. At zero everything
piles up — an unhealthy habitat. At one, the mix negotiates.

---

## Interface

One screen, no tabs. Three columns, one per phony. Each column: model selector,
eight physics knobs that relabel themselves per model, the articulation section,
the Field section, and two draw canvases (pitch and amplitude, independently
switchable, double-click to reset).

The **Field map** is a top-down view with the listener at the centre, distance
rings, and one soft dot per individual that pulses when it calls and brightens
as the chorus synchronises. You can watch waves of synchrony cross the
population.

Everything is drawn — no image assets. The background gradient responds to the
synthesis: it warms with Temperature and darkens toward night with Time of Day.

---

# Draw your own gesture

Each slot provides two drawing areas:

- **Pitch / Gesture**
- **Amplitude**

Enable the corresponding drawing control and draw directly into the graph.
The pitch curve can impose a trajectory on a source instead of leaving it at a static pitch. The amplitude curve shapes the level through the event.
**Draw Depth** controls how strongly the curves affect the sound. At zero, the curves are ignored.
Clicking a curve twice resets it to its neutral shape.
This is particularly useful for sounds where movement is part of the identity. Not all sound generators support it in all constellations.
A static spectrum can contain the correct frequencies and still sound wrong if the original sound is really a trajectory through pitch and amplitude.
This is especially apparent in the owl models. The Great Horned Owl reference contains a broad pitch arch across each note, with a smaller tremulant riding on top.

---

# Biophony 🐸🦉🦗

## Cricket / Katydid

The Cricket model represents a scraper on one forewing dragging across a file of
teeth on the other; each tooth strike excites a resonant wing membrane (the *harp*
in true crickets, the *mirror* in katydids). Field crickets call in a narrow
2–8 kHz band.

Important controls include:

- **Carrier** — wing resonance.
- **Harp Q** — resonance sharpness / resonant body.
- **Mirror Q** — katydid-style secondary resonance.
- **Tooth Jitter** — regularity of tooth spacing, which sets the coherence of the
  drive. Even spacing gives a driven resonance and a near-pure whistle; irregular
  spacing smears it into a broadband rasp. Measured on the prototype: spectral
  flatness rises about **74×** across this control.
- **File Taper** — changing tooth spacing.
- **Strike Tune** — the relationship between tooth strike rate and resonance;
  tonality depends on the two matching, and this control breaks that match
  deliberately.
- **Pulses** — wing strokes within a chirp.

Regular teeth produce a cleaner, more tonal result; irregularity introduces a raspier spectrum. The carrier is intended to operate in the field-cricket region of approximately 2–8 kHz.

**Temperature** drives the insect slot rather than a plain rate knob, because of
Dolbear's law: chirp rate tracks ambient temperature closely enough to
read a thermometer off it. Calibrated on the snowy tree cricket; other species
have their own slopes, which is a feature.

**Katydid** uses the same broad stridulatory concept but emphasises lower-Q resonance, irregular tooth timing, and raspier excitation. This shows why physical mechanism alone is insufficient: excitation regularity strongly affects perceived identity.

## Cicada

The tymbal muscle contracts once per cycle, buckling a row of stiff ribs in
sequence from posterior to anterior. Each rib snapping through is a click, and
each successive rib is weaker and lower — measured in *C. saundersii* as
1050, 870 and 830 Hz for ribs 1–3. The clicks excite two resonators in series:
the tymbal itself, and the abdominal air sac, which acts as a Helmholtz
resonator with the tympana as its neck. Q sits far higher than a single struck
tymbal would suggest, because successive rib pulses *maintain* a coherent
resonance rather than each ringing independently.

Controls include:

- **Tymbal Freq**
- **Tymbal Q**
- **Ribs**
- **Muscle Rate**
- **Rate Drift**
- **Air Sac**
- **Second Band**
- **Brightness**
- **Bell**
- **Sub Rumble**

The model includes a second spectral band because the Japanese cicada reference used during development (*minmin-zemi*, *Hyalessa maculaticollis*) showed distinct low and high bands. The fitted reference values were approximately **4188 Hz** and **15542 Hz**, with a tymbal muscle rate around **366 Hz**, drifting downward through each phrase (model reproduces 374 Hz). The reference is reported with two distinct peaks at 4.7 and 15 kHz; the supplied file measured a ~2.5 kHz phase leaping to ~13.5 kHz, and the model treats this as a real alternating band structure rather than a filter sweep. The phrase can also slow as it progresses rather than maintaining a perfectly constant muscle rate.

### Cicada rhythm

**Phrase** and **Rhythm** are especially important here. Low Rhythm blends syllables toward a shallow tremolo. High Rhythm produces sharper, more separated syllables.

The factory bank therefore contains several distinct cicada characters:

- **Cicada Abura-zemi**
- **Cicada Minmin-zemi**
- **Cicada Higurashi**
- **Japanese Cicada (field)**

Their mechanisms overlap, but their temporal behaviour is intentionally different.

## Frog

The Frog model uses a resonant vocal model combined with population behaviour and synchrony.

## Songbird

Mindlin's group model the avian syrinx as labia driven by air-sac pressure and
syringeal tension. Songbird uses a loop through this simplified **pressure /
tension** space rather than simply oscillating a fixed oscillator. The phase
relationship between the two axes — the **Gesture Phase** control — determines
whether the gesture becomes an upsweep, a flatter tone or a downsweep: verified
on the prototype, phase offsets of π/4 and π/2 produce measurable downsweeps,
3π/2 an upsweep, 0 and π flat or arched syllables. So an entire syllable is a
closed loop drawn in 2D, and its character is the *phase* of that loop — which
is what the **pitch draw canvas** is for on these models.

**Biphonation** runs two sources with slightly different parameters and lets
them interact nonlinearly — birds have two independently controllable sound
sources, one per bronchus, and this is where the eerie doubled quality of real
birdsong comes from.

## Owl

The Owl model is intentionally close to a near-sinusoidal vocal source with a slow tremulant. 
It was rebuilt from a reference recording of the Great Horned Owl, one of the more measurement-driven parts of the project.
The Owl's call is rather difficult to model, thus this is one of the most crude engines.

| | reference | model |
|---|---:|---:|
| F0 | 477.8 Hz | 456.9 Hz |
| 2nd harmonic | −34.3 dB | −37.4 dB |
| 3rd harmonic | −63.8 dB | −65.5 dB |
| tremulant | 6.00 Hz, ±72.9 Hz | 5.56 Hz, ±57.6 Hz |

| Feature | Reference | Model |
|---|---:|---:|
| F0 | 340 Hz | 344 Hz |
| 2nd harmonic | −20 dB | −22 dB |
| Tremulant | 5.8 Hz at 15% | 5.8 Hz at 15% |
| Pitch arch | +12–17% | +14.0% |
| True tremulant | ±3–6% | ±4% |

The 2nd harmonic sitting well down means it is **essentially a pure sine**.
The tremulant fades *in* across the note rather than being
present from the start, with the amplitude swell peaking around 750 ms. The
pitch arch is distinct from the tremulant: the slow arch rises through the
middle of the note, while the faster tremulant sits on top of it.

## Anuran

The Anuran model goes further into the mechanics of frog vocalisation.

Its controls include:

- **Fold Rate** — vocal-fold opening rate.
- **Pulse Rate** — separate arytenoid gating rhythm.
- **Dominant** — strongly radiated harmonic.
- **Radiator** — emphasis of the dominant.
- **Fibrous Mass** — mass loading of the folds.
- **Open Quotient** — fraction of each cycle for which the folds are open.
- **FM Sweep** — changing fold rate through the call.
- **Sac Inflate** — vocal-sac inflation.
- **Low F / High F** — gesture limits.
- **Gesture Phase** — phase between pressure and tension.
- **Wander** — natural variation.
- **Beak Open / Trachea** — vocal-tract shaping.
- **Biphonation** — two sources.
- **Breath** — breath noise.

The factory bank contains several frog-oriented explorations, including **Spring Peeper**, **Boreal Chorus Frog**, **Tungara Whine**, **Tungara Chuck**, **Hyla microcephala**, and several pond-frog fits.

---

# Geophony 🌧️🌊🔥🌬️

## Rain, Surf & Stream

Water sound is almost entirely **bubble entrainment**, not filtered noise. A
bubble rings at its Minnaert frequency, `f ≈ 3.26 / r` with r in metres — so a
1 mm bubble sings near 3.3 kHz — and chirps upward as it collapses.

There are three related but distinct water models:

### Rainfall (`_exact`)

The `_exact` Rainfall model treats rain as individual drops plus their entrained bubbles — an impact click plus an entrained bubble ping, which is precisely why rain sounds silvery rather than like static. A reference implementation used approximately **130 drops per second**, with bubble radii distributed across roughly **0.35–2.6 mm** and a chirping bubble response. The result is deliberately not simply filtered noise; individual impacts and bubble resonances provide the texture. An older, legacy Rain resonator model remains available for compatibility with older presets.

### Ocean Surf

Ocean Surf gates bubble density with a slow swell so events cluster where the wave is breaking, with a broadband roar underneath that is itself modulated by the swell, so the roar surges as waves break rather than remaining static. A legacy Surf resonator model also remains for compatibility.

Rainfall and Ocean Surf are continuous textures rather than conventional short articulated events — their event envelope is effectively held open rather than repeatedly attacking and decaying.

### Stream

A stream is continuous water with a narrow bubble-size distribution, modelled as continuous bubble entrainment over gravel. Main controls include:

- bubble size,
- size spread,
- bubble/impact balance,
- water flow,
- turbulence,
- gravel.

The concept is that moving water's rushing character can emerge from many small bubble events rather than simply from filtered noise.

### Rain Surface

Rain on a surface is not simply rain with a different EQ — each drop is treated as an impulse into a resonant body. **Inharmonic** morphs the modal structure from something closer to a harmonic membrane toward inharmonic plate modes:

- lower-inharmonic: approximately `1.0 : 1.94 : 2.17` — behaves more like a membrane, tent or canvas.
- higher-inharmonic: approximately `1.0 : 1.42 : 1.79 : 2.16` — reads more like metal, a roof or a bell.

The factory bank includes **Rain on a Tin Roof** and **Rain on a Tent**.

## Wind

Vortex shedding past an obstacle produces an aeolian tone at `f = St · U / d`,
with a Strouhal number near 0.2. Each obstacle diameter is therefore a
narrowband resonance that tracks wind speed, over a bed of broadband turbulence.
Wind in pines and wind in a wire fence are the same model with a different
diameter.

Useful controls include:

- **Speed**
- **Gustiness**
- **Obstacle**
- **Aeolian Q**
- **Hiss**
- **Tilt**

The goal is a relationship between flow speed, obstacle size and tonal shedding, rather than treating wind as generic noise.

## Fire

Fire combines two unrelated things: sharp crackle transients from moisture pockets bursting in the wood, and a low combustion rumble from unsteady heat release (generated from slowly varying filtered noise, plus additional gas hiss). The ratio between them *is* the character — a campfire is crackle-dominant, a furnace rumble-dominant.

---

# Anthrophony 🚗🔩

## Distant Road

A passing car is dominated by broadband tyre-road noise, not engine. The Doppler
shift at 30 m/s is only about 9% and is barely audible. What actually sells the
pass is the **moving comb filter** formed by interference between the direct
path and the ground reflection.

The model computes true geometry from source height, receiver height and
horizontal distance, so the reflection geometry changes as the vehicle moves;
the model carries its own moving comb rather than the static ground-reflection
treatment used by ordinary field sources. For an 8 m closest approach the first
notch sweeps from 7456 Hz down to 1250 Hz and back — a symmetric 2.5-octave
sweep. Get that right and it is unmistakable; get only the Doppler and it
sounds like a cartoon.

## Plate Shear, Plate Impact & Bell

Fitted from reference recordings of struck and working metal. Three findings changed the model:

1. **Ratios 1.000, 1.113, 1.182, 1.334, 1.500, 1.666, 1.771, 1.993, 2.222** —
   dense and inharmonic. This is a **plate**, not a bar. An earlier version used
   the free-free bar series (1 : 2.756 : 5.404 : 8.933), which is far too
   sparse. Plate bending modes go roughly as `(m/a)² + (n/b)²`, producing
   exactly this kind of irregular cluster.
2. **Per-mode T60 spans 0.03 s to 11.5 s within the same object.** A smooth
   decay rule is wrong; each mode gets its own decay, drawn log-uniformly and
   tilted so high modes die sooner.
3. **Modes come in split pairs 0.3–0.7% apart** — 439.9/443.1, 733.0/735.6/739.1,
   876.7/879.3 — which is what produces the slow beating.

**Plate Shear** uses stick-slip friction driving these dense, inharmonic plate modes: a spring loads at a rate set by drive velocity, and when the load exceeds a randomly-varying static threshold it slips, releasing an impulse and dropping to the kinetic level — that relaxation cycle is why creaking metal stutters in a way no LFO reproduces. Controls include:

- **Plate F0**
- **Aspect**
- **Drive**
- **Roughness**
- **Mode Split**
- **Decay Spread**
- **Hardness**
- **Sub Rumble**

Large structures can also develop a low structural rumble below the main plate modes; the implementation explicitly adds this low structural component for both Plate Shear and Plate Impact.

**Plate Impact** uses the same general modal family but is excited by impacts rather than continuous friction. Its per-mode decay can range from very short values into the many-second range, giving large metal structures an uneven resonant tail.

**Bell** uses a plate-like modal structure with a wide spread of decay times. **Strike Pos** changes where the object is excited.

## Metal Bar

Metal Bar is a free-free bar model using the characteristic series **1 : 2.756 : 5.404 : 8.933** — more regular than the plate models above, giving a recognisable struck-metal character.

## Machinery

Machinery deliberately moves in the opposite direction from biological irregularity. It uses periodic impacts into a metal housing, with additional load-dependent broadband noise. The modal structure is comparatively sparse and regular, because strict periodicity is one of the cues that makes machinery sound human-made.

Controls include:

- **Housing**
- **Stretch**
- **Clatter**
- **Load**
- **Tone**
- **Resonance**

---

# Habitat 🌳

After the three slots are combined, they pass through **Habitat**.

Habitat is a short, diffuse feedback-delay network intended to suggest early reflections and the spatial response of an outdoor environment rather than a conventional large studio reverb.

Its controls are:

| Control | Function |
|---|---|
| **Size** | Apparent size of the surrounding space |
| **Damping** | Environmental absorption |
| **Mix** | Direct/reflected balance |
| **Width** | Final stereo width |

Small Size suggests a clearing; large Size suggests a valley.

High Damping represents absorbent environments such as dense foliage or snow, while low Damping represents harder environments such as rock and water.

Mix uses equal-power behaviour rather than a simple linear crossfade, avoiding a deliberate volume dip through the middle of the range.

---

# Weather 🌧️🍃

**Weather** is a shared ecological coupling mechanism.

As weather activity increases:

- biophonic calling slows and falls in level;
- wind sources move toward a shared gust process;
- the canopy becomes more active;
- rain can silence insects.

The idea is ecological: animals do not necessarily continue calling at exactly the same rate while a storm is masking them. They can wait for quieter moments.

One measured test found that crickets alongside rain dropped **69% in calling rate at full coupling**, while the same rain changed their rate by only about **1% at zero coupling**.

The weather process is a slow random walk rather than a perfectly periodic modulation, intended to feel more like changing weather than an LFO.

---

# Temperature, Humidity & Acoustic Niche

## Temperature

Temperature is specified in degrees Celsius. It particularly affects insect timing: the project uses the relationship between temperature and cricket chirp rate so the insect chorus speeds up or slows down rather than merely changing pitch.

## Humidity

Humidity affects perceived distance by increasing high-frequency absorption over air paths. Far-away sources therefore become duller, while nearby sources are affected much less.

## Niche

**Niche** is inspired by Krause's acoustic niche hypothesis. Increasing Niche pushes the three active slots apart in frequency so their calls occupy more distinct spectral territory rather than masking each other.

This becomes especially interesting with populated scenes: the aim is not merely to make three sounds louder, but to make them behave more like components of one ecosystem.

---

# Elevation & Ground Reflection

Every slot has an **Elevation** control in metres. This is more than a visual parameter.

Outdoors, the listener receives a direct path and a ground-reflected path. Their interference produces a comb filter whose spacing depends on the geometry, and that geometry changes substantially with source height and distance.

For example, the model's approximate first-notch behaviour includes:

| Source | 3 m | 8 m | 20 m | 50 m |
|---|---:|---:|---:|---:|
| Cricket in grass, 0.05 m | 3645 Hz | 8745 Hz | 21506 Hz | — |
| Frog at pond edge, 0.1 m | 1823 Hz | 4373 Hz | 10753 Hz | 26811 Hz |
| Bird in bush, 2.5 m | 88 Hz | 183 Hz | 433 Hz | 1074 Hz |
| Bird in tree, 8 m | 57 Hz | 76 Hz | 145 Hz | 339 Hz |

These relationships explain why elevation can make a sound feel surprisingly different without changing its source model.

**Distant Road** keeps a separate moving comb because its reflection geometry changes as the vehicle passes.

---

# Time of Day

**Time of Day** morphs between day and night versions of presets that contain a night variant. It also changes the visual atmosphere of the interface and can alter which creatures are active.

Not every preset has a separate night state. Where one exists, only the parameters that actually differ are morphed. Try moving through Time of Day slowly rather than treating it as an on/off switch.

---

# MIDI

The synthesizer is **MIDI-gated by default**.

A MIDI note does not allocate a conventional synthesizer voice. It opens a **single global gate** for the soundscape — this suits a soundscape better than allocating one synthesizer voice per note:

> you are turning a place on, rather than playing an individual animal.

## Free Run

The scene is always running.

## Gated

MIDI notes open the scene; releasing the final held note closes the gate.

## Gated + Pitch

The same global gate behaviour is used, but the MIDI note also transposes the models relative to **Root**.

## Note Attack

How quickly the whole scene fades in after a note opens the gate.

## Note Release

How long the scene takes to fade after the final note is released.

## Velocity

How strongly note velocity affects the level of the field.

## Root

The MIDI note treated as the untransposed reference. In Gated + Pitch mode, notes above and below Root shift the model carriers.

The habitat remains active after the gate closes, allowing its tail to decay naturally rather than being abruptly cut off.

---

# Global Controls

| Control | Description |
|---|---|
| **Temperature** | Ambient temperature in °C; affects biological timing |
| **Time of Day** | Day/night morph |
| **Humidity** | High-frequency air absorption over distance |
| **Niche** | Spectral separation between the three slots |
| **Weather** | Shared weather coupling |
| **Output** | Master output trim |

These descriptions are also exposed through the plugin's hover-help system.

---

# Factory Presets

The current factory bank contains **36 presets**. They are installed on first run as individual XML files under:

```text
Documents/SoundscapeEcologyVST/Factory/
```

User presets are stored in the same SoundscapeEcologyVST directory.

The factory bank ranges from individual-species studies to deliberately dense soundscapes. Examples include:

- **Cricket Field at Dusk**
- **Katydid Rasp**
- **Cicada Abura-zemi**
- **Cicada Minmin-zemi**
- **Cicada Higurashi**
- **Rain on Leaves**
- **Atlantic Surf**
- **Pine Wind**
- **Campfire**
- **Steel Bars Ambient**
- **Songbird Gestures**
- **Biphonation**
- **Tawny Owl**
- **Frog Pond**
- **Distant Motorway**
- **Machine Room**
- **Night Scene**
- **Dawn Chorus**
- **Full Habitat**
- **Spring Peeper**
- **Boreal Chorus Frog**
- **Tungara Whine**
- **Tungara Chuck**
- **Hyla microcephala**
- **Pond Frog (sustained)**
- **Pond Frog (peeps)**
- **Pond Frog Chorus**
- **Great Horned Owl**
- **Japanese Cicada (field)**
- **Rain on a Tin Roof**
- **Rain on a Tent**

The preset system stores the model parameters and, where applicable, a separate night state for Time of Day morphing.

Presets are sorted alphabetically by the actual preset vector, keeping browsing order and loading order aligned.

---

# Realism & Limitations

This project should not be treated as a field-recording replacement.
A physical model can be wrong in many ways while still being useful.
A frog is not merely a resonant oscillator.
A bird is not merely an F0 fundamental frequency and a harmonic ratio.
A forest is not merely a reverb.
The useful part is the attempt to model relationships.

The field-recording fits show both what the approach can get close to and where it remains approximate.
For example, the Great Horned Owl fit is close in F0, harmonic level and tremulant rate, but the development work showed that a static spectral fit was insufficient: the pitch trajectory mattered.
So if something sounds odd, that does not necessarily mean the model is wrong, but insufficient on its own. It may simply mean the model has reached the edge of what it currently represents.

# Preset Implementation Notes

Factory presets are installed to disk on first run rather than remaining locked inside the binary.

The installation process:

1. creates the Factory preset directory;
2. captures the current state;
3. applies each factory preset;
4. stores its description;
5. optionally stores its NIGHT state;
6. writes the resulting state as XML;
7. restores the original state.

Existing factory files are **never overwritten**, preserving user edits.

User presets additionally store the drawn pitch and amplitude curves.

When a preset is loaded, those curves and any NIGHT state are restored along with the parameter state.

---

# Development Warning: Model Indices

Model indices are **append-only**.

When adding a model to the source, append it to the end of its class's model array.

Do **not** insert a new model into the middle of an existing model array.

Presets store the model index rather than the model name, so inserting a model can cause existing presets to point at the wrong generator.

---

# Audio-Thread & Verification Notes

The DSP has been exercised outside JUCE against a small shim so that it can be rendered and analysed independently.

Current verification notes report:

- **378 renders at parameter extremes** across models, slots and classes.
- No crashes.
- No NaNs.
- No infinities.
- **111 controls with hover help**.
- **39 parameters created and read**.
- No allocations on the audio thread.
- No parameter writes from the audio thread.
- No blocking modal loops.
- No console output.
- No raw allocation outside JUCE's own factory functions.

These checks are engineering verification. They are not a claim that every acoustic model is scientifically complete.

---

# Signal Flow

At a high level, the signal path is:

```text
MIDI
  │
  ▼
Global Gate
  │
  ├───────────────┐
  │               │
  ▼               ▼
Slot 1           Slot 2           Slot 3
  │               │                │
  └───────────────┴────────────────┘
                  │
                  ▼
             Global Gate
                  │
                  ▼
               Habitat
                  │
                  ▼
            Width / Output
                  │
                  ▼
              Stereo Out
```

The gate is applied **before Habitat**, intentionally allowing the habitat tail to continue after the soundscape itself has been released.

The final output also uses a fixed internal loudness calibration before the user-facing Output control and soft limiting.

---

# The idea

The most useful way to approach Soundscape Ecology is probably not as a collection of "nature presets".

Pick one mechanism.

Ask what the animal, weather system or physical object is actually doing.

Then listen to what happens when one part of that mechanism changes.

A chorus becomes more interesting when individuals stop behaving identically.

A bird becomes more recognisable when its pitch moves through time.

Rain becomes more convincing when drops excite the thing they actually hit.

A habitat becomes more convincing when distance, height, reflection and weather begin affecting the same sound at once.

That is the small idea underneath the whole synthesizer:

> **Sound can be a way of studying the relationships inside a place.**

If the instrument makes you go outside and listen differently, then the experiment has already done something useful.

🌴 🐸 🌧️ 🦜 🌿

---

# References

### Soundscape ecology

- Krause, B. (1987). *Bioacoustics, habitat ambience in ecological balance.*
  Whole Earth Review 57. — introduces **biophony** and the **acoustic niche
  hypothesis**.
- Pijanowski, B. C., Villanueva-Rivera, L. J., Dumyahn, S. L., Farina, A.,
  Krause, B. L., Napoletano, B. M., Gage, S. H., & Pieretti, N. (2011).
  *Soundscape Ecology: The Science of Sound in the Landscape.* **BioScience**
  61(3), 203–216. https://doi.org/10.1525/bio.2011.61.3.6 — the
  biophony/geophony/anthrophony framework.
- Pijanowski, B. C., Farina, A., Gage, S. H., Dumyahn, S. L., & Krause, B. L.
  (2011). *What is soundscape ecology? An introduction and overview of an
  emerging new science.* **Landscape Ecology** 26, 1213–1232.
- Krause, B. (2012). *The Great Animal Orchestra.* Little, Brown.
- Schafer, R. M. (1977). *The Tuning of the World.* Knopf.

### Birdsong

- Mindlin, G. B., & Laje, R. (2005). *The Physics of Birdsong.* Springer
  (Biological and Medical Physics series). — the primary text.
- Alonso, R., Goller, F., & Mindlin, G. B. (2014). *Motor control of sound
  frequency in birdsong involves the interaction between air sac pressure and
  labial tension.* **Physical Review E** 89, 032706. — the (pressure, tension)
  parameter space and the phase-difference result that the gesture canvas is
  built on.
- Laje, R., Gardner, T. J., & Mindlin, G. B. (2002). *Neuromuscular control of
  vocal production in birdsong.* Physical Review E 65, 051921.
- Trevisan, M. A., & Mindlin, G. B. (2009). *New perspectives on the physics of
  birdsong.* **Philosophical Transactions of the Royal Society A** 367,
  3239–3254.
- Amador, A., & Mindlin, G. B. (2008). *Beyond harmonic sounds in a simple model
  for birdsong production.* Chaos 18, 043123.
- Mindlin, G. B. *Models of birdsong (physics).* **Scholarpedia** —
  freely available overview.
- Aguilera Novoa, S. `birdsongs` — open-source Python implementation of the
  motor-gesture model. https://github.com/saguileran/birdsongs

### Insects — crickets and katydids

- Bennet-Clark, H. C. (1989). *Songs and the physics of sound production.* In
  *Cricket Behavior and Neurobiology*, Cornell University Press.
- Bennet-Clark, H. C. (1999). *Resonators in insect sound production: how
  insects produce loud pure-tone songs.* Journal of Experimental Biology 202,
  3347–3357.
- Montealegre-Z, F., & Mason, A. C. (2005). *The mechanics of sound production in
  Panacanthus pallicornis: the stridulatory motor patterns.* **Journal of
  Experimental Biology** 208, 1219–1237.
- Montealegre-Z, F., Morris, G. K., & Mason, A. C. (2006). *Generation of extreme
  ultrasonics in rainforest katydids.* **Journal of Experimental Biology** 209,
  4923–4937.
- Montealegre-Z, F., et al. (2017). *Structural biomechanics determine spectral
  purity of bush-cricket calls.* **Biology Letters** — tooth density and file
  length as predictors of tonality.
- Dolbear, A. E. (1897). *The cricket as a thermometer.* **The American
  Naturalist** 31(371), 970–971. — Dolbear's law.

### Insects — cicadas

- Pringle, J. W. S. (1954). *A physiological analysis of cicada song.* Journal of
  Experimental Biology 31, 525–560.
- Young, D., & Bennet-Clark, H. C. (1995). *The role of the tymbal in cicada
  sound production.* **Journal of Experimental Biology** 198, 1001–1019.
- Bennet-Clark, H. C., & Young, D. (1992). *A model of the mechanism of sound
  production in cicadas.* Journal of Experimental Biology 173, 123–153. — the
  abdominal air sac as a Helmholtz resonator with the tympana as its neck.
- Bennet-Clark, H. C., & Young, D. (1997). *Tymbal mechanics and the control of
  song frequency in the cicada Cyclochila australasiae.* **Journal of
  Experimental Biology** 200, 1681–1694.
- Bennet-Clark, H. C., & Young, D. (1998). *Sound radiation by the bladder cicada
  Cystosoma saundersii.* Journal of Experimental Biology 201, 701–715. —
  descending frequency across successive ribs.
- Simmons, J. A., Wever, E. G., & Vernon, J. A. (1971). *Periodical cicada:
  mechanism of sound production.* **Science** 171, 212–213.
- Kanda, E. (1960). *Analysis of songs of Japanese cicadas.* **Journal of Insect
  Physiology** — pulse repetition of 200–500/s, and the finding that species
  identity lives in the rhythm rather than the spectrum.
- (2025). *The tymbal of a cicada: nature's sound-generating metastructure.*
  **Nature Communications** 16. https://doi.org/10.1038/s41467-025-65149-5 — a
  chain of bistable oscillators coupled to resonators; the first
  species-adaptable framework.
- Mori, et al. (2026). *Using Autonomous Recording Units to Detect Variation in
  Cicada Calling Pattern Across Urbanized and Green Spaces.* **Ecological
  Research** — reports *Hyalessa maculaticollis* with two distinct peaks at
  4.7 and 15 kHz.

### Bubbles, water and aeroacoustics

- Minnaert, M. (1933). *On musical air-bubbles and the sounds of running water.*
  **Philosophical Magazine** 16, 235–248. — `f ≈ 3.26 / r`.
- Leighton, T. G. (1994). *The Acoustic Bubble.* Academic Press.
- Strouhal, V. (1878). *Über eine besondere Art der Tonerregung.* Annalen der
  Physik und Chemie 241(10), 216–251. — aeolian tones and the Strouhal number.

### Synchronisation

- Kuramoto, Y. (1975). *Self-entrainment of a population of coupled non-linear
  oscillators.* In *International Symposium on Mathematical Problems in
  Theoretical Physics*, Springer.
- Strogatz, S. H. (2000). *From Kuramoto to Crawford.* Physica D 143, 1–20.
- Greenfield, M. D. (1994). *Cooperation and conflict in the evolution of signal
  interactions.* Annual Review of Ecology and Systematics 25, 97–126. — chorusing
  and synchrony in insects.

### Procedural audio

- Farnell, A. (2010). *Designing Sound.* MIT Press. — the foundational text.
  Bubbles, wind, fire and insects worked through from first principles.
- Cook, P. R. (2002). *Real Sound Synthesis for Interactive Applications.*
  A K Peters.
- Smith, J. O. *Physical Audio Signal Processing.* https://ccrma.stanford.edu/~jos/pasp/

### Listening

- Krause, B. — habitat recordings and the *Wild Sanctuary* archive.
- Watson, C. — field recordings.
- Quin, D. — polar and rainforest soundscapes.
- Teibel, I. (1969–1979). *Environments* series. — the 1970s reference point,
  and, fittingly, heavily processed rather than pure documentary.

### Conversations

- Amfivolía, interviewed by Kilohearts (2026), *Procedural Generative Soundscapes
  with Phase Plant.* — on focusing on behavioural patterns *between* sounds
  rather than the quality of individual sounds, and on correlating random
  modulation with features of the sound itself. (https://kilohearts.com/blog/procedural_generative_soundscapes_with_phase_plant)

### Coding

- Claude AI by anthropic, with many back and forth conversations, sending of own field recordings and ideas from me, aquanode.
