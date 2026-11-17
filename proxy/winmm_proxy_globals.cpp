#include <windows.h>

// Shared target table used by the MASM tail-jump stubs.
extern "C" void* g_WinmmTargets[180] = {};
