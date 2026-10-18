#include "FeltPianoModule.h"

using namespace aquanode;
using aquanode::piano::Voicing;

namespace
{
    // the voicing, measured on a recording of a muted felt upright from C1 to C7
    const Voicing& measuredVoicing()
    {
        static const Voicing v
        {
            { 24, 36, 48, 60, 72, 84, 96 },                                           // anchor keys

            { 1.0e-4, 8e-5, 1.05e-4, 3.3e-4, 8.7e-4, 1.7e-3, 3.8e-3 },               // B
            { -7, 2, -2, -1, 1.4, 5.7, 17 },                                          // stretch, cents
            { 1.7, 1.3, 0.95, 0.62, 0.38, 0.18, 0.055 },                              // length, m
            { 0.06, 0.03, 0.012, 0.0063, 0.0045, 0.0039, 0.0035 },                    // density, kg/m
            { 0.125, 0.125, 0.125, 0.125, 0.12, 0.115, 0.11 },                        // strike point
            { 0.55, 0.8, 0.75, 0.6, 0.9, 3.0, 3.8 },                                  // sigma0, 1/s
            0.9,                                                                      // sigmaF
            29, 41,                                                                   // 1 / 2 strings up to
            { -0.3, 0.3 }, { -0.4, 0.05, 0.45 },                                      // unison detune, cents

            { 6e-3, 4.9e-3, 3.6e-3, 2.97e-3, 2.6e-3, 2.3e-3, 2.2e-3 },                // hammer mass, kg
            { 18.6, 18.6, 18.6441, 19.8141, 20.9841, 22.1541, 23.3241 },              // log K
            2.5, 0.4,                                                                 // Hertz p, Hunt-Crossley

            { 60, 150, 300, 600, 1200, 2400, 4800, 9600 },
            { -23.3, -30.7, -38.2, -41.0, -42.6, -44.0, -66.4, -88.9 },               // radiation, dB
            { 6.0, 7.5, 4.9, 9.4, 10.8, 10.4, 22.9 },                                 // register, dB

            9.0, 30.0, 1500.0, 1.2,                                                   // Brightness

            { 58.3, 73.5, 77.8, 82.5, 87.3, 103.8, 116.8, 123.5, 130.9, 138.8 },     // body modes, Hz
            { 0.55, 1.0, 1.0, 0.9, 0.7, 0.7, 0.45, 0.4, 0.35, 0.3 },
            1.1, 30.0, 0.14, 1.4,                                                     // body T60s, levels
            0.0, 1000.0, 0.7, 0.03,                                                   // knock
            0.0, 6000.0, 0.015,                                                       // click

            16.0, 14.0, 0.045, 127,                                                   // damper, damped up to
            0.25,                                                                     // pan range
            0.0                                                                       // output, dB
        };
        return v;
    }

    ModuleDescriptor descriptor()
    {
        return PianoModel::makeDescriptor ("osc.feltpiano", "Felt Piano",
            juce::String ("A physical model of a muted felt upright: a felt hammer thrown at stiff, coupled unison strings, "
                          "a radiating soundboard, a case thump and felt dampers. Dark, soft and close.")
                + PianoModel::controlsDescription(), 15, 30.0f, 2);
    }
}

FeltPianoModule::FeltPianoModule() : PianoModel (measuredVoicing()) {}

AQUANODE_REGISTER_MODULE (FeltPianoModule, descriptor)
