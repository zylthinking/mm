
#include <windows.h>
#include <stdio.h>

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        WSADATA wsa;
        int n = WSAStartup(0x0202, &wsa);
        if (n != 0) {
            printf("faileld start up network\n");
            return FALSE;
        }
    }
    return TRUE;
}