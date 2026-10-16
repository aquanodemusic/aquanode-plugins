#include "HardPianoModule.h"

using namespace aquanode;
using aquanode::piano::Voicing;

namespace
{
    // the voicing, measured on a recording of a piano with hard hammers from C#1 to C8
    const Voicing& measuredVoicing()
    {
        static const Voicing v
        {
            { 24.0, 36.0, 48.0, 60.0, 72.0, 84.0, 98.0 },                             // anchor keys

            { 8.5e-05, 4.6e-05, 0.00011, 0.00033, 0.0009, 0.0027, 0.0075 },           // B
            { -5.0, -1.0, -1.0, 0.0, 1.5, 6.0, 20.0 },                                // stretch, cents
            { 2.0, 1.5, 1.05, 0.65, 0.38, 0.19, 0.07 },                               // length, m
            { 0.06, 0.03, 0.012, 0.0063, 0.0045, 0.0039, 0.0035 },                    // density, kg/m
            { 0.125, 0.125, 0.125, 0.125, 0.12, 0.115, 0.11 },                        // strike point
            { 0.7667, 1.308, 1.13, 1.379, 3.075, 4.154, 1.595 },                      // sigma0, 1/s
            1.49e-09,                                                                 // sigmaF
            28, 44,                                                                   // 1 / 2 strings up to
            { -0.1, 0.1 }, { -0.12, 0.015, 0.15 },                                    // unison detune, cents

            { 0.01, 0.0095, 0.009, 0.0087, 0.008, 0.0073, 0.0068 },                   // hammer mass, kg
            { 24.8, 22.92, 25.51, 23.56, 25.17, 24.2, 25.3 },                         // log K
            2.5, 0.4,                                                                 // Hertz p, Hunt-Crossley

            { 35.0, 70.0, 140.0, 300.0, 700.0, 1600.0, 4000.0, 1e+04 },               // radiation curve at, Hz
            { -59.31, -33.54, -37.39, -41.32, -41.18, -44.67, -49.0, -78.0 },         // radiation, dB
            { 9.541, 8.302, 6.729, 9.259, 5.664, 4.857, 14.38 },                      // register, dB

            9.0, 30.0, 1500.0, 1.2,                                                   // Brightness

            { 58.3, 73.5, 77.8, 82.5, 87.3, 103.8, 116.8, 123.5, 130.9, 138.8 },      // body modes, Hz
            { 0.55, 1.0, 1.0, 0.9, 0.7, 0.7, 0.45, 0.4, 0.35, 0.3 },                  // body mode gains
            1.1, 30.0, 0.14, 0.202,                                                   // body T60s, levels
            0.4022, 450.0, 0.5, 0.06,                                                 // knock
            0.0, 6000.0, 0.015,                                                       // click

            10.83, 9.48, 0.045, 90,                                                   // damper, damped up to
            0.25,                                                                     // pan range
            1.9                                                                       // output, dB
        };
        return v;
    }

    ModuleDescriptor descriptor()
    {
        return PianoModel::makeDescriptor ("osc.hardpiano", "Hard Piano",
            juce::String ("The piano model voiced after a piano with hard hammers: a short, stiff blow and bright, ringing "
                          "strings.")
                + PianoModel::controlsDescription(), 17, 30.0f, 4);
    }
}

HardPianoModule::HardPianoModule() : PianoModel (measuredVoicing()) {}

AQUANODE_REGISTER_MODULE (HardPianoModule, descriptor)
