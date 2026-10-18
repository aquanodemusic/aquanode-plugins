#include "MidPianoModule.h"

using namespace aquanode;
using aquanode::piano::Voicing;

namespace
{
    // the voicing, measured on a recording of a brighter felt piano from C#2 to C8
    const Voicing& measuredVoicing()
    {
        static const Voicing v
        {
            { 36.0, 48.0, 60.0, 72.0, 84.0, 96.0, 108.0 },                            // anchor keys

            { 8.1e-05, 0.000107, 0.00032, 0.00092, 0.0025, 0.006, 0.014 },            // B
            { -2.5, -1.0, -1.0, -1.0, 4.0, 12.0, 25.0 },                              // stretch, cents
            { 1.3, 0.95, 0.62, 0.38, 0.18, 0.055, 0.055 },                            // length, m
            { 0.03, 0.012, 0.0063, 0.0045, 0.0039, 0.0035, 0.0035 },                  // density, kg/m
            { 0.125, 0.125, 0.125, 0.12, 0.115, 0.11, 0.11 },                         // strike point
            { 0.4878, 1.466, 1.718, 1.852, 2.739, 1.288, 11.57 },                     // sigma0, 1/s
            0.7,                                                                      // sigmaF
            29, 41,                                                                   // 1 / 2 strings up to
            { -0.15, 0.15 }, { -0.2, 0.025, 0.225 },                                  // unison detune, cents

            { 0.0049, 0.0036, 0.00297, 0.0026, 0.0023, 0.0022, 0.0022 },              // hammer mass, kg
            { 20.99, 19.75, 20.16, 19.99, 21.8, 23.0, 24.2 },                         // log K
            2.5, 0.4,                                                                 // Hertz p, Hunt-Crossley

            { 35.0, 70.0, 140.0, 300.0, 700.0, 1600.0, 4000.0, 1e+04 },               // radiation curve at, Hz
            { -30.5, -18.66, -25.67, -37.27, -48.45, -51.16, -53.06, -69.73 },        // radiation, dB
            { -2.135, -0.7422, 3.799, 12.85, 12.86, 24.69, -10.36 },                  // register, dB

            9.0, 30.0, 1500.0, 1.2,                                                   // Brightness

            { 58.3, 73.5, 77.8, 82.5, 87.3, 103.8, 116.8, 123.5, 130.9, 138.8 },      // body modes, Hz
            { 0.55, 1.0, 1.0, 0.9, 0.7, 0.7, 0.45, 0.4, 0.35, 0.3 },                  // body mode gains
            1.1, 30.0, 0.14, 0.6088,                                                  // body T60s, levels
            0.5445, 350.0, 0.5, 0.06,                                                 // knock
            0.0006, 6000.0, 0.1,                                                      // click

            7.11, 6.221, 0.045, 127,                                                  // damper, damped up to
            0.25,                                                                     // pan range
            5.1                                                                       // output, dB
        };
        return v;
    }

    ModuleDescriptor descriptor()
    {
        return PianoModel::makeDescriptor ("osc.midpiano", "Mid Piano",
            juce::String ("The piano model voiced after a brighter felt piano: more hammer and more air on top than Felt Piano, "
                          "still warm.")
                + PianoModel::controlsDescription(), 16, 30.0f, 3);
    }
}

MidPianoModule::MidPianoModule() : PianoModel (measuredVoicing()) {}

AQUANODE_REGISTER_MODULE (MidPianoModule, descriptor)
