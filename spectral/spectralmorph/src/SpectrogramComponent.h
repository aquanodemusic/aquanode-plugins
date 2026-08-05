#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MorphEngine.h"

//==============================================================================
/** Scrolling spectrogram: main input in green, sidechain in purple, additively
    blended so overlapping content reads as white.  Frequency is log-spaced on
    the vertical axis (bottom = 20 Hz, top = Nyquist). */
class SpectrogramComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit SpectrogramComponent (MorphEngine& e) : engine (e)
    {
        setOpaque (true);
        mainDb.resize (MorphEngine::kDisplayBands, -120.0f);
        sideDb.resize (MorphEngine::kDisplayBands, -120.0f);
        startTimerHz (60);
    }

    void setRange (float floorDb_, float ceilDb_) { floorDb = floorDb_; ceilDb = ceilDb_; }
    void setShowMain (bool b) { showMain = b; }
    void setShowSide (bool b) { showSide = b; }

    /** HD Visuals: renders the rolling spectrogram from the engine's
        frequency-reassigned points (sub-bin accurate, resolved to the full
        pixel height) instead of the 256 log-spaced display bands. Purely a
        rendering choice - toggling it does not touch audio in any way. */
    void setHDVisuals (bool shouldUseHD) { hdVisuals = shouldUseHD; }

    /** Turns the whole spectrogram display on/off. When off, the timer is
        stopped (saving CPU) and the component is hidden. */
    void setActive (bool shouldBeActive)
    {
        if (active == shouldBeActive)
            return;

        active = shouldBeActive;
        setVisible (active);

        if (active)
            startTimerHz (60);
        else
            stopTimer();
    }

    bool isActive() const noexcept { return active; }

    void resized() override
    {
        const int w = juce::jmax (16, getWidth());
        const int h = juce::jmax (16, getHeight());
        image = juce::Image (juce::Image::RGB, w, h, true);
        juce::Graphics g (image);
        g.fillAll (background);

        mainRowDb.assign ((size_t) h, -120.0f);
        sideRowDb.assign ((size_t) h, -120.0f);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        if (image.isValid())
            g.drawImageAt (image, 0, 0);

        // grid
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float y = freqToY (f);
            if (y > 0 && y < (float) getHeight())
            {
                g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());
                g.setColour (juce::Colours::white.withAlpha (0.30f));
                g.setFont (11.0f);
                g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k"
                                         : juce::String (f, 0),
                            4, (int) y - 14, 40, 14, juce::Justification::left);
                g.setColour (juce::Colours::white.withAlpha (0.07f));
            }
        }

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRect (getLocalBounds(), 1);
    }

private:
    void timerCallback() override
    {
        if (! image.isValid())
            return;

        // Analysis frames arrive at the hop rate (sampleRate / hopSize), which
        // swings wildly with FFT size and overlap - anywhere from ~10 Hz (large
        // FFT, low overlap) to several hundred Hz (small FFT, high overlap).
        // Drawing "however many frames happened to queue up" ties scroll speed
        // to that rate and reads as stutter/catch-up bursts.
        //
        // Instead we always draw exactly one column per timer tick, so scroll
        // speed is locked to the 60 Hz screen refresh and stays constant no
        // matter what the analysis rate is doing:
        //  - if several frames piled up since the last tick, drain to the
        //    newest one and draw only that (old ones are simply skipped)
        //  - if no new frame has arrived yet, hold the last one and redraw it
        //    rather than freezing the scroll
        bool gotFrame = false;

        if (hdVisuals)
        {
            if (engine.popReassignedMain (raMain)) { buildRow (mainRowDb, raMain); gotFrame = true; }
            if (engine.popReassignedSide (raSide)) { buildRow (sideRowDb, raSide); gotFrame = true; }
        }
        else
        {
            while (engine.popDisplayFrame (mainDb.data(), sideDb.data()))
                gotFrame = true;
        }

        if (gotFrame)
            haveData = true;

        if (haveData)
        {
            scrollAndDrawColumn();
            repaint();
        }
    }

    /** Resolves a frame of frequency-reassigned (freq, mag) points down onto
        the pixel-row grid, one dB value per row, taking the loudest point
        that lands on (or, lightly attenuated, beside) each row. This is what
        gives HD Visuals its sub-bin sharpness instead of the 256-band blur. */
    void buildRow (std::vector<float>& row, const MorphEngine::ReassignFrame& frame)
    {
        std::fill (row.begin(), row.end(), -120.0f);
        const int h = (int) row.size();
        if (h <= 1)
            return;

        for (int i = 0; i < frame.count; ++i)
        {
            const float y = freqToY (frame.freqHz[(size_t) i]);
            const int   r = juce::roundToInt (y);
            if (r < 0 || r >= h)
                continue;

            const float db = juce::Decibels::gainToDecibels (frame.magLin[(size_t) i], -120.0f);
            row[(size_t) r] = juce::jmax (row[(size_t) r], db);

            // Light +/-1px spread so isolated points read as a line, not dust.
            if (r > 0)     row[(size_t) (r - 1)] = juce::jmax (row[(size_t) (r - 1)], db - 6.0f);
            if (r < h - 1) row[(size_t) (r + 1)] = juce::jmax (row[(size_t) (r + 1)], db - 6.0f);
        }
    }

    void scrollAndDrawColumn()
    {
        const int w = image.getWidth(), h = image.getHeight();
        image.moveImageSection (0, 0, 1, 0, w - 1, h);

        juce::Image::BitmapData bmp (image, w - 1, 0, 1, h, juce::Image::BitmapData::writeOnly);

        if (hdVisuals)
        {
            for (int y = 0; y < h; ++y)
            {
                const float m = showMain ? norm (mainRowDb[(size_t) y]) : 0.0f;
                const float s = showSide ? norm (sideRowDb[(size_t) y]) : 0.0f;
                writePixel (bmp, y, m, s);
            }
        }
        else
        {
            const int nb = MorphEngine::kDisplayBands;
            for (int y = 0; y < h; ++y)
            {
                // y = 0 is the top = Nyquist
                const float t = 1.0f - (float) y / (float) (h - 1);
                const int   b = juce::jlimit (0, nb - 1, (int) std::round (t * (nb - 1)));

                const float m = showMain ? norm (mainDb[(size_t) b]) : 0.0f;
                const float s = showSide ? norm (sideDb[(size_t) b]) : 0.0f;
                writePixel (bmp, y, m, s);
            }
        }
    }

    void writePixel (juce::Image::BitmapData& bmp, int y, float m, float s)
    {
        const float r  = mainCol.getFloatRed()   * m + sideCol.getFloatRed()   * s;
        const float gg = mainCol.getFloatGreen() * m + sideCol.getFloatGreen() * s;
        const float bl = mainCol.getFloatBlue()  * m + sideCol.getFloatBlue()  * s;

        bmp.setPixelColour (0, y, juce::Colour::fromFloatRGBA (juce::jmin (1.0f, r),
                                                              juce::jmin (1.0f, gg),
                                                              juce::jmin (1.0f, bl), 1.0f));
    }

    float norm (float db) const noexcept
    {
        const float v = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (ceilDb - floorDb));
        return std::pow (v, 0.72f);                       // slight gamma: lifts quiet detail
    }

    float freqToY (float f) const noexcept
    {
        const double nyq = juce::jmax (1000.0, engine.getSampleRate() * 0.5);
        const double t = std::log (f / 20.0) / std::log (nyq / 20.0);
        return (float) ((1.0 - t) * getHeight());
    }

    MorphEngine& engine;
    juce::Image image;
    std::vector<float> mainDb, sideDb;

    // HD Visuals: one dB value per pixel row, built from the engine's
    // frequency-reassigned points; and scratch frames reused each tick so
    // popping doesn't allocate.
    bool hdVisuals = true;
    std::vector<float> mainRowDb, sideRowDb;
    MorphEngine::ReassignFrame raMain, raSide;

    float floorDb = -84.0f, ceilDb = -6.0f;
    bool showMain = true, showSide = true;
    bool active = true;
    bool haveData = false;   // true once the first analysis frame has arrived

    const juce::Colour background { 0xff0b0d10 };
    const juce::Colour mainCol    { 0xff3cff96 };   // green  = main input
    const juce::Colour sideCol    { 0xffa855f7 };   // purple = sidechain

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramComponent)
};
