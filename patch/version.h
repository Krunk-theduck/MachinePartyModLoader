// THE loader version - the one place it lives. Everything else derives from
// it automatically:
//   - patch/bootstrap.gd carries the placeholder @MPML_VERSION@ and gets this
//     value injected when the modded pck is built (so bumping this line alone
//     bumps the in-game version and rebuilds the pck),
//   - the Windows version resource (version.rc) is regenerated from it by CMake.
// Bump it whenever the loader behaviour changes (bootstrap or patcher logic).
#pragma once

namespace mpml {

inline constexpr char kVersion[] = "7";

} // namespace mpml
