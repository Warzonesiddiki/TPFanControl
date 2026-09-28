// The one place in the application that names the TVicPort DLL's entry points.
//
// T5-01. The legacy code called `::OpenTVicPort`, `::ReadPort`, `::WritePort`,
// `::TestHardAccess` and `::SetHardAccess` from wherever it happened to need
// them, which is how the same five calls came to appear in two files with
// different error handling. The DLL's surface is now described once, as a
// `core::TvicPortApi`, and everything else - the adapter, the port primitives
// the application's core path uses - is built from that description.
//
// Nothing here opens a driver. `makeTvicPortApi()` returns callables; deciding
// whether and when to call them is the caller's business, and the only two
// callers are:
//
//   * `fanstuff.cpp`, which hands the port primitives to the application's
//     `LegacyBackend` while `approot.cpp` owns the driver handle;
//   * `core::TvicPortBackend`, which owns the lifecycle and is used for the
//     baseline comparison (T4-06) once a backend has been verified.
//
// Windows only. Off Windows this header compiles, and the implementation is
// empty.
#pragma once

#include "core/tvicport_backend.h"

namespace tpfancontrol {
namespace app {

// A TvicPortApi bound to the vendor DLL linked into this application.
//
// Every callable is filled: the DLL is linked statically through
// TVicPort.lib, so if the binary links, the entry points exist. Whether the
// *driver* is present and loadable is a runtime question, and it is answered by
// calling `openDriver()` - not here.
core::TvicPortApi makeTvicPortApi();

} // namespace app
} // namespace tpfancontrol
