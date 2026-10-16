#pragma once

#include <JuceHeader.h>

#if JUCE_ANDROID

namespace aquavibrio::androidfiles
{

//==============================================================================
//  On Android the system file picker (the Storage Access Framework) hands back
//  a content:// document URI. It has no filesystem path, so
//  FileChooser::getResult() cannot represent it: it comes back as a File that
//  fails existsAsFile(), and anything built on it silently does nothing.
//
//  Everything therefore goes through getURLResult() and AndroidDocument
//  streams - the same route Aquanode Modular takes for patch import/export
//  and for its sample loader. Message thread only (call these from the
//  FileChooser callback).
//==============================================================================

// Reads the whole document the picker returned into memory. A content:// stream
// is a pipe from the ContentResolver and is not reliably seekable, so callers
// get plain bytes rather than a stream. `fileName` is the document's display
// name, extension included. False if nothing was picked or it could not be read.
inline bool read (const juce::FileChooser& fc, juce::MemoryBlock& data, juce::String& fileName)
{
    data.setSize (0);

    const auto url = fc.getURLResult();
    if (url.isEmpty())
        return false;

    auto doc = juce::AndroidDocument::fromDocument (url);
    if (! doc.hasValue())
        return false;

    auto in = doc.createInputStream();
    if (in == nullptr)
        return false;

    in->readIntoMemoryBlock (data);
    fileName = doc.getInfo().getName();
    return data.getSize() > 0;
}

// Writes bytes to the document the "create document" picker returned, replacing
// whatever it held. `writtenName` (optional) gets the document's display name.
inline bool write (const juce::FileChooser& fc, const void* bytes, size_t numBytes,
                   juce::String* writtenName = nullptr)
{
    const auto url = fc.getURLResult();
    if (url.isEmpty())
        return false;

    auto doc = juce::AndroidDocument::fromDocument (url);
    if (! doc.hasValue())
        return false;

    auto out = doc.createOutputStream();
    if (out == nullptr)
        return false;

    const bool ok = out->write (bytes, numBytes);
    out->flush();

    if (writtenName != nullptr)
        *writtenName = doc.getInfo().getName();

    return ok;
}

} // namespace aquavibrio::androidfiles

#endif // JUCE_ANDROID
