// VirtualDX7's .syx loading for its patch version. Kept apart from
// VirtualDX7Patch.cpp, which includes the native engine and the effects for
// itself and so cannot see the plugin's processor header as well.
#include "PluginVirtualDX7Config.h"

namespace aqp_virtualdx7
{
    #include AQP_VIRTUALDX7_SRC(PluginProcessor.h)
}

namespace aquanode::plugins::virtualdx7
{
    // a DX7 bank (or single voice) into the plugin, the way its own Import does
    bool loadSysex (juce::AudioProcessor& p, const juce::File& file)
    {
        auto* vdx = dynamic_cast<aqp_virtualdx7::VDX7AudioProcessor*> (&p);
        juce::MemoryBlock mb;
        if (vdx == nullptr || ! file.loadFileAsData (mb))
            return false;
        return vdx->importSysexBank (mb.getData(), mb.getSize(), file.getFileNameWithoutExtension());
    }
}
