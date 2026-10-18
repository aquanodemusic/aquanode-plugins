#pragma once

#include "ModuleCore.h"
#include "DXNativeFM.h"

// The small phosphor displays of the DX family (DX Operator, DX
// Envelope, DX Pitch EG, DX LFO). Each one reads its module's parameters by
// name a few times a second and redraws only when they change, so it costs
// nothing while the knobs stand still. Header-only: every display is used by
// exactly one module.
namespace aquanode::dxui
{
    inline const juce::Colour phosphor { 0xff9fe870 };
    inline const juce::Colour bezelColour { 0xff101410 };

    inline int iparam (const SynthModule& m, const juce::String& id) { return (int) std::lround (m.getParameter (id)); }

    inline void drawBezel (juce::Graphics& g, juce::Rectangle<float> bezel)
    {
        g.setColour (bezelColour);
        g.fillRoundedRectangle (bezel, 4.0f);
    }

    inline void strokeTrace (juce::Graphics& g, const std::vector<float>& ys, juce::Rectangle<float> plot,
                             std::function<float (float)> yFor)
    {
        juce::Path curve;
        for (size_t i = 0; i < ys.size(); ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) juce::jmax<size_t> (1, ys.size() - 1);
            const float y = yFor (ys[i]);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (phosphor.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (3.0f));   // glow
        g.setColour (phosphor);
        g.strokePath (curve, juce::PathStrokeType (1.2f));
    }

    inline void drawKeyOff (juce::Graphics& g, juce::Rectangle<float> plot, float kx)
    {
        juce::Path line, dashed;
        line.startNewSubPath (kx, plot.getY());
        line.lineTo (kx, plot.getBottom());
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (phosphor.withAlpha (0.35f));
        g.fillPath (dashed);

        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (phosphor.withAlpha (0.55f));
        const float lx = juce::jlimit (plot.getX(), plot.getRight() - 44.0f, kx - 22.0f);
        g.drawText ("KEY OFF", juce::Rectangle<float> (lx, plot.getY() - 1.0f, 44.0f, 9.0f),
                    juce::Justification::centred, false);
    }

    // Fits a simulated trace to the display width by linear interpolation.
    inline std::vector<float> resampleTrace (const std::vector<float>& trace, int width, float floorValue)
    {
        std::vector<float> points ((size_t) juce::jmax (32, width), floorValue);
        if (trace.size() < 2)
            return points;
        const double last = (double) (trace.size() - 1);
        for (size_t x = 0; x < points.size(); ++x)
        {
            const double pos = last * (double) x / (double) (points.size() - 1);
            const auto i0 = (size_t) pos;
            const auto i1 = juce::jmin (trace.size() - 1, i0 + 1);
            const float f = (float) (pos - (double) i0);
            points[x] = trace[i0] + (trace[i1] - trace[i0]) * f;
        }
        return points;
    }

    //==========================================================================
    // Operator envelope (DX Operator, DX Envelope): the envelope for a note
    // held at middle C until it settles, released at the dashed line. Reads
    // r1..r4, l1..l4 and rateScale.
    //==========================================================================
    class EnvelopeView : public juce::Component, private juce::Timer
    {
    public:
        explicit EnvelopeView (SynthModule& m) : module (&m) { startTimerHz (10); timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            const auto bezel = getLocalBounds().toFloat();
            drawBezel (g, bezel);
            const auto plot = bezel.reduced (6.0f, 6.0f).withTrimmedBottom (9.0f);

            // 24 dB grid lines (32 DX7 level steps)
            g.setColour (phosphor.withAlpha (0.08f));
            for (float db : { -24.0f, -48.0f, -72.0f })
                g.drawHorizontalLine ((int) yFor (db, plot), plot.getX(), plot.getRight());

            if (points.empty() || totalSecs <= 0.0f)
                return;

            drawKeyOff (g, plot, plot.getX() + plot.getWidth() * keyOffSecs / totalSecs);
            strokeTrace (g, points, plot, [plot] (float db) { return yFor (db, plot); });

            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.setColour (phosphor.withAlpha (0.55f));
            g.drawText (juce::String (totalSecs, totalSecs < 10.0f ? 2 : 1) + " s",
                        bezel.withTrimmedRight (6.0f).withTrimmedTop (bezel.getHeight() - 11.0f),
                        juce::Justification::centredRight, false);
        }

    private:
        static float yFor (float db, juce::Rectangle<float> plot)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (db + dxnative::kEgRangeDb) / dxnative::kEgRangeDb);
            return plot.getBottom() - t * plot.getHeight();
        }

        void timerCallback() override
        {
            auto* m = module.get();
            if (m == nullptr)
                return;

            uint8_t r[4], l[4];
            for (int i = 0; i < 4; ++i)
            {
                r[i] = (uint8_t) juce::jlimit (0, 99, iparam (*m, "r" + juce::String (i + 1)));
                l[i] = (uint8_t) juce::jlimit (0, 99, iparam (*m, "l" + juce::String (i + 1)));
            }
            const int rs = juce::jlimit (0, 7, iparam (*m, "rateScale"));
            juce::String key;
            for (int i = 0; i < 4; ++i) key << (int) r[i] << ',' << (int) l[i] << ',';
            key << rs;
            if (key == lastKey)
                return;
            lastKey = key;

            // Run the engine's own envelope: hold until it has settled (or
            // 10 s), then release until it has finished - or settled, since a
            // patch with L4 above 0 holds its release level forever, as on a DX7.
            dxnative::OperatorEnv e;
            e.configure (r, l, dxnative::rateScaleOffset (rs, 60));
            e.reset (-dxnative::kEgRangeDb);
            e.keyOn();

            constexpr float dt = 0.0005f;              // fine enough for a 12 ms R=99 segment
            constexpr int settledSteps = 160;          // 80 ms without change
            trace.clear();
            auto run = [&] (bool releasing)
            {
                float prev = 1.0e9f;
                int still = 0;
                for (int i = 0; i < 20000; ++i)
                {
                    if (releasing && e.finished())
                        break;
                    const float db = e.tick (dt);
                    trace.push_back (db);
                    still = std::abs (db - prev) < 1.0e-4f ? still + 1 : 0;
                    prev = db;
                    if (i > 100 && still > settledSteps)
                        break;
                }
            };
            run (false);
            keyOffSecs = (float) trace.size() * dt;
            e.keyOff();
            run (true);
            trace.push_back (e.levelDb());
            totalSecs = (float) trace.size() * dt;
            refit();
        }

        void refit()
        {
            points = resampleTrace (trace, getWidth() - 12, -dxnative::kEgRangeDb);
            repaint();
        }

        void resized() override { refit(); }

        juce::WeakReference<SynthModule> module;
        std::vector<float> trace, points;
        float keyOffSecs { 0.0f }, totalSecs { 0.0f };
        juce::String lastKey;
    };

    //==========================================================================
    // Pitch envelope (DX Pitch EG): pitch in semitones over time, the centre
    // line is the played pitch. Reads r1..r4, l1..l4 and transpose.
    //==========================================================================
    class PitchEnvView : public juce::Component, private juce::Timer
    {
    public:
        explicit PitchEnvView (SynthModule& m) : module (&m) { startTimerHz (10); timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            const auto bezel = getLocalBounds().toFloat();
            drawBezel (g, bezel);
            const auto plot = bezel.reduced (6.0f, 6.0f).withTrimmedBottom (9.0f);

            g.setColour (phosphor.withAlpha (0.12f));
            g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.setColour (phosphor.withAlpha (0.55f));
            g.drawText ("+-" + juce::String (span, 0) + " st", bezel.reduced (6.0f, 2.0f), juce::Justification::topRight, false);

            if (flat)
            {
                g.drawText ("no pitch movement", plot, juce::Justification::centred, false);
                return;
            }
            if (points.empty() || totalSecs <= 0.0f)
                return;

            drawKeyOff (g, plot, plot.getX() + plot.getWidth() * keyOffSecs / totalSecs);
            const float s = span;
            strokeTrace (g, points, plot, [plot, s] (float st)
            {
                return plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, st / s) * plot.getHeight() * 0.5f;
            });
            g.setColour (phosphor.withAlpha (0.55f));
            g.drawText (juce::String (totalSecs, totalSecs < 10.0f ? 2 : 1) + " s",
                        bezel.withTrimmedRight (6.0f).withTrimmedTop (bezel.getHeight() - 11.0f),
                        juce::Justification::centredRight, false);
        }

    private:
        void timerCallback() override
        {
            auto* m = module.get();
            if (m == nullptr)
                return;

            uint8_t r[4], l[4];
            juce::String key;
            for (int i = 0; i < 4; ++i)
            {
                r[i] = (uint8_t) juce::jlimit (0, 99, iparam (*m, "r" + juce::String (i + 1)));
                l[i] = (uint8_t) juce::jlimit (0, 99, iparam (*m, "l" + juce::String (i + 1)));
                key << (int) r[i] << ',' << (int) l[i] << ',';
            }
            if (key == lastKey)
                return;
            lastKey = key;

            dxnative::PitchEnv e;
            e.configure (r, l);
            flat = e.isIdle();
            trace.clear();
            if (! flat)
            {
                e.keyOn();
                constexpr float dt = 0.001f;
                auto run = [&] (int maxSteps)
                {
                    float prev = 1.0e9f;
                    int still = 0;
                    for (int i = 0; i < maxSteps; ++i)
                    {
                        const float st = e.tick (dt);
                        trace.push_back (st);
                        still = std::abs (st - prev) < 1.0e-4f ? still + 1 : 0;
                        prev = st;
                        if (i > 50 && still > 120)
                            break;
                    }
                };
                run (8000);
                keyOffSecs = (float) trace.size() * 0.001f;
                e.keyOff();
                run (8000);
                totalSecs = (float) trace.size() * 0.001f;

                float peak = 1.0f;
                for (auto v : trace) peak = juce::jmax (peak, std::abs (v));
                span = peak <= 2.0f ? 2.0f : peak <= 12.0f ? 12.0f : peak <= 24.0f ? 24.0f : 48.0f;
            }
            points = resampleTrace (trace, getWidth() - 12, 0.0f);
            repaint();
        }

        void resized() override { points = resampleTrace (trace, getWidth() - 12, 0.0f); }

        juce::WeakReference<SynthModule> module;
        std::vector<float> trace, points;
        float keyOffSecs { 0.0f }, totalSecs { 0.0f }, span { 12.0f };
        bool flat { true };
        juce::String lastKey;
    };

    //==========================================================================
    // LFO (DX LFO): two seconds of the wave with its delay and fade-in, and
    // the speed in Hz. Reads wave, speed and delay.
    //==========================================================================
    class LfoView : public juce::Component, private juce::Timer
    {
    public:
        explicit LfoView (SynthModule& m) : module (&m) { startTimerHz (10); timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            const auto bezel = getLocalBounds().toFloat();
            drawBezel (g, bezel);
            const auto plot = bezel.reduced (6.0f, 8.0f).withTrimmedBottom (7.0f);

            g.setColour (phosphor.withAlpha (0.10f));
            g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

            if (! points.empty())
                strokeTrace (g, points, plot, [plot] (float v)
                {
                    return plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * plot.getHeight() * 0.5f;
                });

            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.setColour (phosphor.withAlpha (0.6f));
            g.drawText (caption, bezel.reduced (6.0f, 1.0f), juce::Justification::topLeft, false);
            g.drawText (juce::String (windowSecs, 1) + " s", bezel.reduced (6.0f, 1.0f),
                        juce::Justification::bottomRight, false);
        }

    private:
        static float wave (int w, float p, float sh)
        {
            switch (w)
            {
                case 0:  return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
                case 1:  return 1.0f - 2.0f * p;
                case 2:  return 2.0f * p - 1.0f;
                case 3:  return (p < 0.5f) ? 1.0f : -1.0f;
                case 4:  return std::sin (dxnative::kTwoPi * p);
                default: return sh;
            }
        }

        void timerCallback() override
        {
            auto* m = module.get();
            if (m == nullptr)
                return;

            const int w = juce::jlimit (0, 5, iparam (*m, "wave"));
            const int speed = juce::jlimit (0, 99, iparam (*m, "speed"));
            const int delay = juce::jlimit (0, 99, iparam (*m, "delay"));
            const juce::String key = juce::String (w) + "," + juce::String (speed) + "," + juce::String (delay);
            if (key == lastKey && ! points.empty())
                return;
            lastKey = key;

            static const char* names[] = { "Triangle", "Saw Down", "Saw Up", "Square", "Sine", "S/H" };
            const float hz = dxnative::lfoSpeedToHz (speed);
            float hold = 0.0f, fade = 0.0f;
            dxnative::lfoDelayTimes (delay, hold, fade);
            caption = juce::String (names[w]) + "  " + juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz";

            // show at least the delay plus a few cycles, at most 8 s
            windowSecs = juce::jlimit (1.0f, 8.0f, hold + fade + juce::jmax (0.5f, 4.0f / hz));
            const int n = juce::jmax (64, getWidth() - 12);
            points.assign ((size_t) n, 0.0f);
            juce::Random rng (7);
            float sh = 0.0f, lastCycle = -1.0f;
            for (int i = 0; i < n; ++i)
            {
                const float t = windowSecs * (float) i / (float) (n - 1);
                const float cyc = t * hz;
                if (std::floor (cyc) != lastCycle) { lastCycle = std::floor (cyc); sh = rng.nextFloat() * 2.0f - 1.0f; }
                const float fadeGain = t < hold ? 0.0f : (fade <= 0.0f ? 1.0f : juce::jmin (1.0f, (t - hold) / fade));
                points[(size_t) i] = wave (w, cyc - std::floor (cyc), sh) * fadeGain * 0.9f;
            }
            repaint();
        }

        void resized() override { lastKey.clear(); timerCallback(); }

        juce::WeakReference<SynthModule> module;
        std::vector<float> points;
        juce::String caption, lastKey;
        float windowSecs { 2.0f };
    };
}
