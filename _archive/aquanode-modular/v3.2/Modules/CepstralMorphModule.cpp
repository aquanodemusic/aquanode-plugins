#include "CepstralMorphModule.h"

static aquanode::ModuleDescriptor cepstralMorphDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    // NB: the sidebar puts an Effect module under the Spectral sub-heading
    // purely by this prefix (see SidebarComponent::buildRows), so the
    // "fx.spec" start is load-bearing, not cosmetic.
    d.typeId = "fx.speccepmorph";
    d.displayName = "Cepstral Morph";
    d.description =
        "Cross-synthesis at 4096-point resolution: Carrier keeps its own fine structure and phase "
        "while its spectral envelope is bent toward Modulator's. Speech into a pad makes the pad "
        "talk. The envelope comes from the real cepstrum, so Clarity is the single knob that "
        "matters: low reads broad formant shapes, 100% is the classic setting, and 200% keeps every "
        "quefrency - the envelope becomes the raw spectrum and Carrier is fully replaced. Flip "
        "swaps which input you hear. Adds 3072 samples of latency, which is not reported to the "
        "host, so keep it in series rather than parallel with a dry path.";
    d.section = ModuleSection::Effect;
    d.sockets = {
        audioIn  ("carrierIn", "Carrier"),
        audioIn  ("modIn",     "Modulator"),
        audioOut ("morphOut",  "Morph Out")
    };
    d.params = {
        makeRotary ("morph",    "Morph",     0.0f, 150.0f, 100.0f, 0, "%"),
        makeRotary ("clarity",  "Clarity",   0.0f, 200.0f, 100.0f, 0, "%"),
        makeRotary ("smooth",   "Smooth",    0.0f,  95.0f,   0.0f, 0, "%"),
        makeRotary ("maxBoost", "Max Boost", 3.0f,  72.0f,  72.0f, 1, "dB"),
        makeRotary ("dynamics", "Dynamics",  0.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("mix",      "Mix",       0.0f, 100.0f, 100.0f, 1, "%"),
        makeCombo  ("flip",     "Flip",      { "Flip: Off", "Flip: On" }, 0, 2),
        makeCombo  ("freeze",   "Freeze Mod",{ "Freeze Mod: Off", "Freeze Mod: On" }, 0, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CepstralMorphModule, cepstralMorphDescriptor)
