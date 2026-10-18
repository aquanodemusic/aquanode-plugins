// DXNativeFM.h - the six-operator FM engine with the DX7 feature set that the
// DX modules (DX Operator, DX Envelope, DX Pitch EG, DX LFO) are built from.
//
// It is VirtualDX7's own native engine - one copy for both, so a DX Operator
// behaves exactly like one operator of VirtualDX7's native engine, and fixes
// to that engine reach the modules too. (VirtualDX7 compiles the same header
// inside its own namespace for its GUI version in Aquanode Modular.)
#pragma once

#include "../PluginSources/virtual-dx7/Source/NativeFM.h"

namespace dxnative = vdx7native;
