# SpectralEdit

Download builds from the [Releases](../../../../releases) page.

---

## 📖 Manual

SpectralEdit loads up to **1 minute** of audio (you can specify the starting time for longer files), allowing you to edit the spectrogram content in various creative ways.

> 💡 **FFT Settings Tip:** Different FFT Settings achieve different types of sound. Lower FFT values introduce more artifacts, which can sound incredibly rich—especially when blurred.

### 1. Navigation & View
* **Spectrogram Display:** The main window shows the frequency content of a loaded sample over time.
* **Zoom Sliders:** Linear sliders to zoom into specific time (X) or frequency (Y) ranges.
* **Scrollbars:** Navigate through the loaded audio once zoomed in.
* **Start Marker:** An orange vertical line with a triangle at the top. This sets the starting point for MIDI-triggered playback.

---

### 2. Edit Modes
*Change modes using the mode buttons to interact with the spectral data.*

#### 🔍 Select (Mode 0)
Click and drag to highlight a rectangular area of the spectrogram. Inside this region, you can use the right sidebar menu to alter the selected spectral content:
* **Rotate U/D:** Flips the selected content vertically.
* **Rotate L/R:** Flips the selected content horizontally (reverses playback for the region).
* **Rotate Knobs:** Instead of flipping, this rotates the sound content. The spectral bins wrap around, appearing at the opposite end when extending outside the region.
* **Volume:** Change the volume of the selected region.
* **Compression:** Compresses the sound in the selected region. *Note: This has a significant effect on the overall volume.*

#### ✏️ Draw (Mode 1)
Paint new frequency components directly onto the display. The painted frequencies snap directly to the FFT bins, meaning they may not always sound perfectly "in tune."
* **Thickness:** Controls the vertical width of the paint stroke.
* **Amplitude:** Controls the intensity and volume of the added signal.
* **Harmonics:** Automatically adds integer multiples of the painted frequency (rolls off at $1/n$).

#### 💧 Smear (Mode 2)
Apply a time-domain moving average to "blur" frequencies together.
* **Radius:** Controls the horizontal (time) extent of the smear effect.

#### 🎛️ Scrub (Mode 3)
Click and drag across the spectrogram to play the audio back at that exact position. Moving the cursor from right to left plays the sound in reverse.
* **Speed:** Caps the playback rate during scrubbing for smoother or more rhythmic results.
  * `0`: Plays the resampled sound at the exact speed of your mouse movement.
  * `1`: Forces the sound to always play at a steady 1x speed.

---

### 3. Playback & FFT
* **MIDI Trigger:** Play MIDI notes to trigger the resynthesized audio. Middle C (labeled **C3**) plays at the original pitch; other notes repitch the sound accordingly.
* **FFT Order:** Changes the FFT size (range: **9 to 14**, or **512 to 16,384** samples). Higher values provide better frequency resolution but lower temporal precision.
* **Status Label:** Indicates `"Compiling..."` while the engine resynthesizes your edits into a playable buffer. This process is usually very quick.

---

🎵 *A sound demo is included in the `demos/` folder.*