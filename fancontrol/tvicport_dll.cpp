// The binding from the vendored TVicPort header to core::TvicPortApi (T5-01).
//
// Not compiled, not run and not reviewed against a running driver by the
// environment this repository is developed in: there is no Windows machine and
// no MSVC here. What can be said for it is smaller and more precise than "it
// works": it calls the five functions the vendor header declares, with the
// types the vendor header declares, and it does nothing else. The first real
// evidence is the windows-build job (T1-01) and then a machine (T4-06).
//
// It deliberately does not:
//
//   * open the driver, or retry opening it. `approot.cpp` owns that, and its
//     180-second retry loop is legacy behaviour that is being measured, not
//     copied into a new layer;
//   * call SetHardAccess. The adapter records the driver's answer and asks for
//     the switch only when the caller explicitly asked for it;
//   * claim a version. The DLL exposes none through this API, and the vendor
//     header's "4.0" describes the package that shipped in 2005, not the file
//     that is loaded today. An empty version is reported as unknown, which is
//     the truth, rather than as a number somebody might quote.

#include "tvicport_dll.h"

#if defined(_WIN32)

#include "TVicPort.h"

namespace tpfancontrol {
namespace app {

core::TvicPortApi makeTvicPortApi()
{
    core::TvicPortApi api;

    api.openDriver = []() -> bool {
        return ::OpenTVicPort() != FALSE;
    };

    api.closeDriver = []() {
        ::CloseTVicPort();
    };

    api.isDriverOpen = []() -> bool {
        return ::IsDriverOpened() != FALSE;
    };

    api.testHardAccess = []() -> bool {
        return ::TestHardAccess() != FALSE;
    };

    api.setHardAccess = [](bool value) {
        ::SetHardAccess(value ? TRUE : FALSE);
    };

    // ReadPort returns a byte and has no error channel: a port read that the
    // driver did not perform is indistinguishable from one that returned 0xFF.
    // The adapter's trace therefore records that a call was made, never that a
    // value was measured, and the correlation with an independent measurement
    // is a human step (docs/HARDWARE_VERIFICATION.md section 7).
    api.readPort = [](std::uint16_t port) -> std::uint8_t {
        return static_cast<std::uint8_t>(::ReadPort(static_cast<USHORT>(port)));
    };

    api.writePort = [](std::uint16_t port, std::uint8_t value) {
        ::WritePort(static_cast<USHORT>(port), static_cast<UCHAR>(value));
    };

    // Left empty on purpose. See the header comment above.
    api.version.clear();
    return api;
}

} // namespace app
} // namespace tpfancontrol

#endif // _WIN32
