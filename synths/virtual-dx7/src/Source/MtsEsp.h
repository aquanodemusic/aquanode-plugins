/*
    MtsEsp.h  -  ODDSound MTS-ESP support for the native engine.

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
    The client library
    ----------------------------------------------------------------------------
    libMTSClient.h / .cpp live in Source/External/MTS-ESP and are compiled in by
    default (VDX7_USE_MTS_ESP defaults to 1 below). They are ODDSound's, taken
    unmodified from https://github.com/ODDSound/MTS-ESP, under the permissive
    licence reproduced alongside them in LICENSE.MTS-ESP.

    The client links against nothing: it looks for the system LIBMTS at runtime
    and reports "no master" when it is absent. A build with this switched on
    therefore still runs on a machine that has never heard of MTS-ESP. To leave
    it out of a build entirely, define VDX7_USE_MTS_ESP=0.

    On Linux the client uses dlopen, so that exporter needs -ldl.

    GPLv3.
*/
#pragma once

#include <atomic>
#include <functional>
#include <string>

#ifndef VDX7_USE_MTS_ESP
 #define VDX7_USE_MTS_ESP 1
#endif

#if VDX7_USE_MTS_ESP
 #include "External/MTS-ESP/libMTSClient.h"
#endif

namespace vdx7 {

// Registers with an MTS-ESP master if one is present in the session, and hands
// the native engine a frequency lookup plus a note filter. With no master (or
// no library) it reports itself inactive and the engine keeps whatever tuning
// it already has.
//
// setEnabled(false) shuts the whole thing off at the source: both callbacks
// start answering "no opinion" and hasMaster() starts returning false. That is
// how a loaded firmware ROM disables microtuning. The emulated engine never
// consults Tuning at all, so this is belt and braces - but the native engine is
// still fed MIDI while the emulator is driving the audio (so that switching
// backends mid-note does not strand a key down), and this keeps that shadow
// engine in the same tuning the audible one is in.
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

    // Message thread; read from the audio thread via the callbacks below.
    void setEnabled (bool shouldBeEnabled)
    {
        enabled_.store (shouldBeEnabled, std::memory_order_release);
    }

    bool isEnabled() const { return enabled_.load (std::memory_order_acquire); }

    // True only when the library is compiled in, a master is actually running,
    // AND we have not been switched off by a firmware ROM - so the UI can say
    // something honest.
    bool hasMaster() const
    {
       #if VDX7_USE_MTS_ESP
        return isEnabled() && client_ != nullptr && MTS_HasMaster (client_);
       #else
        return false;
       #endif
    }

    // Name of the scale the master is broadcasting; empty when there is none.
    // Message thread only - the returned pointer is owned by the client lib.
    std::string scaleName() const
    {
       #if VDX7_USE_MTS_ESP
        if (hasMaster())
            if (const char* n = MTS_GetScaleName (client_))
                return std::string (n);
       #endif
        return {};
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
    // happens when the library is absent, when no master has appeared yet, or
    // when a firmware ROM has switched us off.
    //
    // Safe to call from the audio thread: the lookup is an array read.
    std::function<double (int)> provider()
    {
       #if VDX7_USE_MTS_ESP
        auto* self = this;
        return [self](int note) -> double
        {
            if (self->client_ == nullptr)        return 0.0;
            if (! self->isEnabled())             return 0.0;
            if (! MTS_HasMaster (self->client_)) return 0.0;
            return MTS_NoteToFrequency (self->client_, (char) note, -1);
        };
       #else
        return {};
       #endif
    }

    // The callback handed to Tuning::setNoteFilter. A master may define a
    // keyboard map with unmapped keys; ODDSound's guidance is that a client
    // should drop those note-ons rather than sound them at some fallback pitch.
    // Answering false is the safe default, and is what happens whenever we are
    // switched off or no master is present.
    std::function<bool (int)> noteFilter()
    {
       #if VDX7_USE_MTS_ESP
        auto* self = this;
        return [self](int note) -> bool
        {
            if (self->client_ == nullptr)        return false;
            if (! self->isEnabled())             return false;
            if (! MTS_HasMaster (self->client_)) return false;
            return MTS_ShouldFilterNote (self->client_, (char) note, -1);
        };
       #else
        return {};
       #endif
    }

private:
   #if VDX7_USE_MTS_ESP
    MTSClient* client_ = nullptr;
   #endif
    std::atomic<bool> enabled_ { true };
};

} // namespace vdx7
