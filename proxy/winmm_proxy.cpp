#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>

extern "C" void* g_WinmmTargets[180];

namespace Detail {
    HMODULE g_realWinmm = nullptr;

    extern "C" uintptr_t WINAPI WinmmMissingExport()
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
} // namespace Detail

extern "C" BOOL InitializeProxy()
{
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);

    if (length == 0 || length >= MAX_PATH)
        return FALSE;

    std::wstring realPath(systemDirectory);
    realPath += L"\\winmm.dll";

    // Full absolute system path prevents loading this proxy recursively.
    g_realWinmm = LoadLibraryW(realPath.c_str());
    if (!g_realWinmm)
        return FALSE;

    for (size_t i = 0; i < 180; ++i)
        g_WinmmTargets[i] = reinterpret_cast<void*>(&WinmmMissingExport);

    // The generated table below is kept in the same order as winmm_exports.def.
    // Each name is resolved from the real system DLL.
    g_WinmmTargets[0] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "auxGetDevCapsA"));
    g_WinmmTargets[1] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "auxGetDevCapsW"));
    g_WinmmTargets[2] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "auxGetNumDevs"));
    g_WinmmTargets[3] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "auxGetVolume"));
    g_WinmmTargets[4] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "auxOutMessage"));
    g_WinmmTargets[5] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "auxSetVolume"));
    g_WinmmTargets[6] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "CloseDriver"));
    g_WinmmTargets[7] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "DefDriverProc"));
    g_WinmmTargets[8] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "DriverCallback"));
    g_WinmmTargets[9] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "DrvGetModuleHandle"));
    g_WinmmTargets[10] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "GetDriverModuleHandle"));
    g_WinmmTargets[11] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyConfigChanged"));
    g_WinmmTargets[12] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyGetDevCapsA"));
    g_WinmmTargets[13] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyGetDevCapsW"));
    g_WinmmTargets[14] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyGetNumDevs"));
    g_WinmmTargets[15] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyGetPos"));
    g_WinmmTargets[16] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyGetPosEx"));
    g_WinmmTargets[17] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyGetThreshold"));
    g_WinmmTargets[18] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joyReleaseCapture"));
    g_WinmmTargets[19] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joySetCapture"));
    g_WinmmTargets[20] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "joySetThreshold"));
    g_WinmmTargets[21] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciDriverNotify"));
    g_WinmmTargets[22] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciDriverYield"));
    g_WinmmTargets[23] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciExecute"));
    g_WinmmTargets[24] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciFreeCommandResource"));
    g_WinmmTargets[25] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetCreatorTask"));
    g_WinmmTargets[26] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetDeviceIDA"));
    g_WinmmTargets[27] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetDeviceIDFromElementIDA"));
    g_WinmmTargets[28] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetDeviceIDFromElementIDW"));
    g_WinmmTargets[29] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetDeviceIDW"));
    g_WinmmTargets[30] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetDriverData"));
    g_WinmmTargets[31] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetErrorStringA"));
    g_WinmmTargets[32] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetErrorStringW"));
    g_WinmmTargets[33] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciGetYieldProc"));
    g_WinmmTargets[34] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciLoadCommandResource"));
    g_WinmmTargets[35] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciSendCommandA"));
    g_WinmmTargets[36] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciSendCommandW"));
    g_WinmmTargets[37] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciSendStringA"));
    g_WinmmTargets[38] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciSendStringW"));
    g_WinmmTargets[39] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciSetDriverData"));
    g_WinmmTargets[40] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mciSetYieldProc"));
    g_WinmmTargets[41] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiConnect"));
    g_WinmmTargets[42] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiDisconnect"));
    g_WinmmTargets[43] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInAddBuffer"));
    g_WinmmTargets[44] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInClose"));
    g_WinmmTargets[45] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInGetDevCapsA"));
    g_WinmmTargets[46] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInGetDevCapsW"));
    g_WinmmTargets[47] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInGetErrorTextA"));
    g_WinmmTargets[48] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInGetErrorTextW"));
    g_WinmmTargets[49] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInGetID"));
    g_WinmmTargets[50] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInGetNumDevs"));
    g_WinmmTargets[51] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInMessage"));
    g_WinmmTargets[52] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInOpen"));
    g_WinmmTargets[53] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInPrepareHeader"));
    g_WinmmTargets[54] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInReset"));
    g_WinmmTargets[55] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInStart"));
    g_WinmmTargets[56] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInStop"));
    g_WinmmTargets[57] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiInUnprepareHeader"));
    g_WinmmTargets[58] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutCacheDrumPatches"));
    g_WinmmTargets[59] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutCachePatches"));
    g_WinmmTargets[60] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutClose"));
    g_WinmmTargets[61] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutGetDevCapsA"));
    g_WinmmTargets[62] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutGetDevCapsW"));
    g_WinmmTargets[63] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutGetErrorTextA"));
    g_WinmmTargets[64] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutGetErrorTextW"));
    g_WinmmTargets[65] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutGetID"));
    g_WinmmTargets[66] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutGetNumDevs"));
    g_WinmmTargets[67] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutGetVolume"));
    g_WinmmTargets[68] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutLongMsg"));
    g_WinmmTargets[69] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutMessage"));
    g_WinmmTargets[70] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutOpen"));
    g_WinmmTargets[71] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutPrepareHeader"));
    g_WinmmTargets[72] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutReset"));
    g_WinmmTargets[73] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutSetVolume"));
    g_WinmmTargets[74] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutShortMsg"));
    g_WinmmTargets[75] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiOutUnprepareHeader"));
    g_WinmmTargets[76] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamClose"));
    g_WinmmTargets[77] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamOpen"));
    g_WinmmTargets[78] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamOut"));
    g_WinmmTargets[79] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamPause"));
    g_WinmmTargets[80] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamPosition"));
    g_WinmmTargets[81] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamProperty"));
    g_WinmmTargets[82] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamRestart"));
    g_WinmmTargets[83] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "midiStreamStop"));
    g_WinmmTargets[84] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerClose"));
    g_WinmmTargets[85] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetControlDetailsA"));
    g_WinmmTargets[86] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetControlDetailsW"));
    g_WinmmTargets[87] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetDevCapsA"));
    g_WinmmTargets[88] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetDevCapsW"));
    g_WinmmTargets[89] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetID"));
    g_WinmmTargets[90] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetLineControlsA"));
    g_WinmmTargets[91] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetLineControlsW"));
    g_WinmmTargets[92] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetLineInfoA"));
    g_WinmmTargets[93] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetLineInfoW"));
    g_WinmmTargets[94] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerGetNumDevs"));
    g_WinmmTargets[95] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerMessage"));
    g_WinmmTargets[96] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerOpen"));
    g_WinmmTargets[97] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mixerSetControlDetails"));
    g_WinmmTargets[98] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmDrvInstall"));
    g_WinmmTargets[99] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmGetCurrentTask"));
    g_WinmmTargets[100] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioAdvance"));
    g_WinmmTargets[101] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioAscend"));
    g_WinmmTargets[102] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioClose"));
    g_WinmmTargets[103] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioCreateChunk"));
    g_WinmmTargets[104] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioDescend"));
    g_WinmmTargets[105] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioFlush"));
    g_WinmmTargets[106] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioGetInfo"));
    g_WinmmTargets[107] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioInstallIOProcA"));
    g_WinmmTargets[108] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioInstallIOProcW"));
    g_WinmmTargets[109] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioOpenA"));
    g_WinmmTargets[110] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioOpenW"));
    g_WinmmTargets[111] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioRead"));
    g_WinmmTargets[112] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioRenameA"));
    g_WinmmTargets[113] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioRenameW"));
    g_WinmmTargets[114] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioSeek"));
    g_WinmmTargets[115] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioSendMessage"));
    g_WinmmTargets[116] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioSetBuffer"));
    g_WinmmTargets[117] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioSetInfo"));
    g_WinmmTargets[118] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioStringToFOURCCA"));
    g_WinmmTargets[119] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioStringToFOURCCW"));
    g_WinmmTargets[120] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmioWrite"));
    g_WinmmTargets[121] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmsystemGetVersion"));
    g_WinmmTargets[122] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmTaskBlock"));
    g_WinmmTargets[123] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmTaskCreate"));
    g_WinmmTargets[124] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmTaskSignal"));
    g_WinmmTargets[125] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "mmTaskYield"));
    g_WinmmTargets[126] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "OpenDriver"));
    g_WinmmTargets[127] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "PlaySound"));
    g_WinmmTargets[128] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "PlaySoundA"));
    g_WinmmTargets[129] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "PlaySoundW"));
    g_WinmmTargets[130] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "SendDriverMessage"));
    g_WinmmTargets[131] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "sndPlaySoundA"));
    g_WinmmTargets[132] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "sndPlaySoundW"));
    g_WinmmTargets[133] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "timeBeginPeriod"));
    g_WinmmTargets[134] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "timeEndPeriod"));
    g_WinmmTargets[135] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "timeGetDevCaps"));
    g_WinmmTargets[136] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "timeGetSystemTime"));
    g_WinmmTargets[137] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "timeGetTime"));
    g_WinmmTargets[138] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "timeKillEvent"));
    g_WinmmTargets[139] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "timeSetEvent"));
    g_WinmmTargets[140] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInAddBuffer"));
    g_WinmmTargets[141] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInClose"));
    g_WinmmTargets[142] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInGetDevCapsA"));
    g_WinmmTargets[143] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInGetDevCapsW"));
    g_WinmmTargets[144] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInGetErrorTextA"));
    g_WinmmTargets[145] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInGetErrorTextW"));
    g_WinmmTargets[146] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInGetID"));
    g_WinmmTargets[147] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInGetNumDevs"));
    g_WinmmTargets[148] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInGetPosition"));
    g_WinmmTargets[149] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInMessage"));
    g_WinmmTargets[150] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInOpen"));
    g_WinmmTargets[151] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInPrepareHeader"));
    g_WinmmTargets[152] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInReset"));
    g_WinmmTargets[153] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInStart"));
    g_WinmmTargets[154] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInStop"));
    g_WinmmTargets[155] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveInUnprepareHeader"));
    g_WinmmTargets[156] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutBreakLoop"));
    g_WinmmTargets[157] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutClose"));
    g_WinmmTargets[158] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetDevCapsA"));
    g_WinmmTargets[159] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetDevCapsW"));
    g_WinmmTargets[160] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetErrorTextA"));
    g_WinmmTargets[161] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetErrorTextW"));
    g_WinmmTargets[162] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetID"));
    g_WinmmTargets[163] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetNumDevs"));
    g_WinmmTargets[164] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetPitch"));
    g_WinmmTargets[165] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetPlaybackRate"));
    g_WinmmTargets[166] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetPosition"));
    g_WinmmTargets[167] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutGetVolume"));
    g_WinmmTargets[168] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutMessage"));
    g_WinmmTargets[169] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutOpen"));
    g_WinmmTargets[170] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutPause"));
    g_WinmmTargets[171] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutPrepareHeader"));
    g_WinmmTargets[172] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutReset"));
    g_WinmmTargets[173] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutRestart"));
    g_WinmmTargets[174] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutSetPitch"));
    g_WinmmTargets[175] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutSetPlaybackRate"));
    g_WinmmTargets[176] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutSetVolume"));
    g_WinmmTargets[177] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutUnprepareHeader"));
    g_WinmmTargets[178] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "waveOutWrite"));
    g_WinmmTargets[179] = reinterpret_cast<void*>(GetProcAddress(g_realWinmm, "WOWAppExit"));

    // Do not unload g_realWinmm: exported stubs jump into it for process lifetime.
    return TRUE;
}
