# SignalControl

Download builds from the [Releases](../../../../releases) page.

## Manual

SignalControl is a utility VST plugin designed to draw control voltage curves and gate signals. It outputs audio signals as automation parameter / event outputs are not reliably implemented for third party plugins in many DAWs, such as FL Studio.

- **Channel 0 (Left)**: Smooth CV signal output (values range from $0.0$ to $1.0$).
- **Channel 1 (Right)**: Gate output ($1.0$ when over a drawn section, $0.0$ over empty space/holes).

Use SignalControl in combination with my PrecisionControl VST or alternatives to specifically mute either channel if you only want the CV or Gate output respectively.

Refer to the provided screenshot to see how you'd set it up in FL Studio's patcher.

### Drawing

You can freehand draw curves into an internal buffer containing 2048 sequentially scanned points.

**Draw Mode**
Click and drag with the left mouse button to draw continuous line segments. The plugin automatically interpolates linearly between your movements at draw time.

**Erase Mode**
Hold Right-Click while dragging, or toggle the dedicated Erase button in the lower bar to clear points.

**Empty Spaces (Holes)**
Sections without data are registered as inactive. When the playhead hits an inactive zone, the Gate drops immediately to 0 and the CV output smoothly fades.

### Speed & Transformation Controls

**Rate Knob**
When Tempo Sync is deactivated, playback speed is governed by the Rate knob. Negative values cause the playhead to scan the buffer in reverse. The 100x button enables a wider range, from -1 to +1 Hertz to -100 to +100 Hertz.

**Transform Operations**
The bottom right tool row enables quick batch alterations to the buffer data.
- **Mirror X**: Flips the curve horizontally (reverses the progression sequence).
- **Mirror Y**: Inverts the curve amplitude vertically (high points become low points).
- **Move L/R (Shift X Slider)**: Horizontally shifts data positions. Points pushed off one edge seamlessly wrap. Letting go of the slider snaps it back to center.
- **Move U/D (Shift Y Slider)**: Vertically shifts value heights. Values that go above $1.0$ or below $0.0$ wrap.
- **Clear Button**: Completely wipes all curve data from the buffer, resetting it to blank state.
