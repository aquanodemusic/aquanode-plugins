# IRForge

*An Impulse response foundry and convolver.*

IRForge is an all-in-one Cepstral Impulse Response Generation and Application Plugin.

Live-Record or Drop in any audio sample and IRForge turns it into an impulse response (IR)
you can hear immediately — no rendering a file, no loading it into a
separate convolver. Everything happens live: change a knob, hear the result
right away. Saving the IR to disk lets you keep it as well.

## What's happening

An impulse response is a short piece of audio that describes a sound
character — a room, a speaker cabinet, a piece of hardware — which you can
then "stamp" onto any other sound via convolution (the same tech behind
convolution reverbs and cabinet-simulator plugins).

You may think of an impulse response like a long exposure photography:
You hear one spectral envelope, the impulse response, which recorded what has been audible over
a period of time, compressed into a single periodogram "image" (not quite correct, but a good analogy).

IRForge builds that impulse response directly from a sample you give it,
using cepstral audio analysis (basically a spectrogram of a spectrogram). The **CHARACTER** knob encodes this:

- **Low CHARACTER** → IRForge extracts only the smooth, general tone of the
  sample — closer to "what space or system did this sound come from,"
  without much of the sample's actual pitch or notes surviving.
- **High CHARACTER** → IRForge keeps much more of the sample's actual
  harmonic detail — its pitch, its timbre, its "chord." Convolving with this
  version imposes the sample's whole character onto whatever you feed it —
  this is what can turn plain noise or a drum hit into something that rings
  with a chord or a distinct tone.

The interesting thing is that you can not only record the impulse of some ambient space, but audio that is far longer than
only an impulse. You can drop a whole song into IRForge and hear the sonic fingerprint of the complete song as a whole.

CHARACTER defaults high (75%) because most people load a sample specifically
wanting that character to come through, not a neutral, washed-out version of
it.

Credit: this technique of turning a sample into a minimum-phase impulse
response was popularised by puriFIR.

## How a sound moves through the plugin

1. You load or record a **source sample**, and crop it to the part you want
   (this is the single most important decision — which bit of the sample you
   use is basically the whole musical choice).
2. IRForge analyzes that snippet and builds an **impulse response** from it,
   shaped by CHARACTER and the other Forge/Shape controls.
3. Your **live input** is convolved with that IR in real time — this is what
   you actually hear coming out of the plugin.
4. **Mix** blends the processed (wet) signal against your unprocessed (dry)
   input, and **Output** trims the level of the wet signal on its own.

Every knob you turn — CHARACTER, FFT size, Length, Decay, or anything in the
Shape section — regenerates the impulse response automatically in the
background. You don't need to press anything to hear a change; it happens
within a fraction of a second of moving a knob.

## Parameters

| Section | Parameter | What it does |
|---|---|---|
| Forge | **Character** | The main control. Low = softer, more generic tone. High = keeps the sample's actual pitch/harmonic detail. |
| Forge | **FFT 2^n** | Analysis detail used to build the IR. Higher settings resolve finer detail but cost more CPU. |
| Forge | **Length** | How long the resulting impulse response is. |
| Forge | **Decay** | Adds an extra fade-out shape on top of the IR. |
| Forge | **Linear Phase** | An alternate reconstruction mode with different phase behaviour, see below. |
| Shape | **Stretch** | Time-stretches the IR without changing its pitch content. |
| Shape | **Predelay** | Adds a short gap of silence before the IR starts. |
| Shape | **Low Cut / High Cut** | Filters applied to the IR itself (not your input signal). |
| Shape | **Tilt** | Tilts the IR's tone brighter or darker. |
| Shape | **Width** | Stereo width of the IR. |
| Shape | **Reverse** | Plays the IR backwards. |
| Output | **Mix** | Blend between your dry input and the processed sound. |
| Output | **Output** | Trims the level of the processed (wet) sound only — your dry signal is never affected by this knob, so it's safe to push Output up to compensate for a quiet effect without your dry sound suddenly getting loud. |
| Output | **Gain Match** | Keeps overall loudness consistent as you change CHARACTER, so you're comparing tone, not volume. |

## Linear Phase 

Linear Phase reconstructs the impulse response with linear-phase filtering instead of the default minimum-phase mode, preserving the timing relationship between frequencies rather than shifting them earlier in time. This can sound more transparent on some material, but often introduces pre-ringing and a different overall character—compare both modes by ear to see which suits the source.

## Recording your own source

You don't need an external sample — hit **Record** to capture whatever is
coming into the plugin directly as your source material. While recording,
the effect is bypassed so you always hear your dry input while it's being
captured. Stop recording and it's instantly available as your source, without needing to save a file and reload it.

## The display

- **Top: source waveform** — shows your loaded/recorded sample, with
  draggable handles to crop the section you want to convert.
- **Bottom: resulting IR** — shows the frequency content (top) and volume
  envelope (bottom strip) of the impulse response IRForge has built, so you
  can see the effect of every knob as well as hear it. This updates live any
  time the IR changes, even if its length doesn't.