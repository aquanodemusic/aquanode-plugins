#pragma once
#include <JuceHeader.h>
#include <cmath>

// ==============================================================
//  Colour palette - watery shiny blue + light grey, themeable.
//
//  Two tint colours (greys, blues) come from the colour pickers behind the
//  ALPHABETA logo. Every shade keeps its relation to its family's reference
//  shade (hue offset, saturation and brightness ratio), so the picked colour
//  becomes the main grey (bg) / main blue and all the other levels follow.
//  Text and the dark title-bar / bezel greys keep their brightness so they
//  stay readable.
//
//  The palette is process-global: with two AlphaBeta windows open at once
//  they share the theme of the one opened (or changed) last.
// ==============================================================
namespace Pal {
    inline const juce::Colour greyRef{ 0xFFCDD2DA }, blueRef{ 0xFF3AB0FF };
    inline juce::Colour greyTint = greyRef, blueTint = blueRef;
    inline int version = 0;                  // bumped on every change (for cached images)

    // maps c so that r -> p, keeping 0 and 1 fixed
    inline float relMap(float c, float r, float p) {
        if (p <= r) return r > 1.0e-4f ? c * p / r : c;
        return r < 0.9999f ? c + (1.0f - c) * (p - r) / (1.0f - r) : c;
    }
    inline juce::Colour map(juce::Colour c, juce::Colour ref, juce::Colour tint, bool keepBrightness) {
        if (tint == ref) return c;
        float h, s, v, rh, rs, rv, th, ts, tv;
        c.getHSB(h, s, v); ref.getHSB(rh, rs, rv); tint.getHSB(th, ts, tv);
        h += th - rh; h -= std::floor(h);
        s = juce::jlimit(0.0f, 1.0f, relMap(s, rs, ts));
        if (!keepBrightness) v = juce::jlimit(0.0f, 1.0f, relMap(v, rv, tv));
        return juce::Colour::fromHSV(h, s, v, c.getFloatAlpha());
    }
    inline juce::Colour g (juce::uint32 argb) { return map(juce::Colour(argb), greyRef, greyTint, false); } // grey level
    inline juce::Colour gt(juce::uint32 argb) { return map(juce::Colour(argb), greyRef, greyTint, true);  } // grey text / dark chrome
    inline juce::Colour b (juce::uint32 argb) { return map(juce::Colour(argb), blueRef, blueTint, false); } // blue level

    inline juce::Colour bg, bgDark, cell, knobTop, knobMid, knobBot, knobRim, track,
                        blue, blueDark, blueMid, blueBright, blueGlow,
                        text, textDim, textBright, titleBg, titleLine, sepBg;

    inline void apply() {
        bg = g(0xFFCDD2DA); bgDark = g(0xFFB8BDC8); cell = g(0xFFB0B5C0);
        knobTop = g(0xFFD8DCE6); knobMid = g(0xFF9EA5B2); knobBot = g(0xFF5E6570);
        knobRim = g(0xFF3A3E4A); track = g(0xFF454A56);
        // Blues - watery, luminous
        blue = b(0xFF3AB0FF); blueDark = b(0xFF1A70CC); blueMid = b(0xFF55C4FF);
        blueBright = b(0xFFAAEAFF); blueGlow = b(0x443AB0FF);
        // Text
        text = gt(0xFF1E2230); textDim = gt(0xFF606A7A); textBright = gt(0xFF8DAABB);
        // Title bar
        titleBg = gt(0xFF141820); titleLine = b(0xFF1A78CC);
        // Separator / section dividers
        sepBg = g(0xFFC5CAD5);
        ++version;
    }
    inline void setTheme(juce::Colour grey, juce::Colour blueTintIn) {
        greyTint = grey; blueTint = blueTintIn; apply();
    }
    inline const bool initialised = (apply(), true);
}
