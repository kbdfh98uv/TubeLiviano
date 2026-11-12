/* Test-only incompatible libvlccore.dll. Never ship this as a dependency. */
#include <windows.h>

__declspec(dllexport) int tubeliviano_decoy_marker(void) { return 1; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE;
}
