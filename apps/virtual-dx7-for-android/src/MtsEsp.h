/*
    MtsEsp.h  -  optional ODDSound MTS-ESP support for the native engine.

    MTS-ESP is not a MIDI message. It is a small shared library that a tuning
    master plugin and its clients both talk to, so the master can change the
    scale live and every client follows, including under held notes. That makes
    it a better fit than SysEx for anything dynamic, and it is the thing people
    usually mean by "microtuning that just works in the DAW".

    Only the native engine can use it. The emulated engine boots Yamaha's own
    firmware, and the original DX7 has no microtuning of any kind - the feature
    first appeared on the DX7II (or on a mk1 with a Grey Matter E! board). There
    is no parameter to retune and no SysEx that would reach one, so no amount of
    plumbing on our side would help. Loading a firmware ROM therefore turns
    microtuning off; clearing it turns microtuning back on. That is a property of
    the hardware being modelled, not a limitation of this code.

    ----------------------------------------------------------------------------
    Enabling it
    ----------------------------------------------------------------------------
    The client library is ODDSound's and is not bundled here. To switch it on:

      1. Get libMTSClient.h and libMTSClient.cpp from
         https://github.com/ODDSound/MTS-ESP
      2. Drop both into this folder and add the .cpp to the Projucer project.
      3. Define VDX7_USE_MTS_ESP=1 in the project's preprocessor definitions.

    With the macro undefined this header compiles to nothing at all and the
    plugin behaves exactly as before, so it is safe to leave off.

    GPLv3.
*/
#pragma once

#include <functional>

#if VDX7_USE_MTS_ESP
 #include "libMTSClient.h"
#endif

namespace vdx7 {

// Registers with an MTS-ESP master if one is present in the session, and hands
// the native engine a frequency lookup. With no master (or no library) it
// reports itself inactive and the engine keeps whatever tuning it already has.
class MtsEspClient
{
public:
    MtsEspClient()
    {
       #if VDX7_USE_MTS_ESP
        client_ = MTS_RegisterClient();
       #endif
    }

    ~MtsEspClient()
    {
       #if VDX7_USE_MTS_ESP
        if (client_ != nullptr) MTS_DeregisterClient (client_);
       #endif
    }

    MtsEspClient (const MtsEspClient&) = delete;
    MtsEspClient& operator= (const MtsEspClient&) = delete;

    // True only when the library is compiled in AND a master is actually
    // running, so the UI can say something honest.
    bool hasMaster() const
    {
       #if VDX7_USE_MTS_ESP
        return client_ != nullptr && MTS_HasMaster (client_);
       #else
        return false;
       #endif
    }

    static bool isCompiledIn()
    {
       #if VDX7_USE_MTS_ESP
        return true;
       #else
        return false;
       #endif
    }

    // The callback handed to Tuning::setFrequencyProvider. Returning 0 means
    // "no opinion", which leaves the engine on its own table - that is what
    // happens when the library is absent or no master has appeared yet.
    //
    // Safe to call from the audio thread: the lookup is an array read.
    std::function<double (int)> provider()
    {
       #if VDX7_USE_MTS_ESP
        auto* c = client_;
        return [c](int note) -> double
        {
            if (c == nullptr || ! MTS_HasMaster (c)) return 0.0;
            return MTS_NoteToFrequency (c, (char) note, -1);
        };
       #else
        return {};
       #endif
    }

private:
   #if VDX7_USE_MTS_ESP
    MTSClient* client_ = nullptr;
   #endif
};

} // namespace vdx7
