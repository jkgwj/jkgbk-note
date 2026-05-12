#include "hijack.h"
#include "MyHook.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(hModule);
        // 导出与转发
        real_dll_init();
        //初始化
        ::CreateThread(NULL, 0, MyHook::InitHook, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        MyHook::UninstallAll();
        if (!lpReserved)
            real_dll_free();
        break;
    default:
        break;
    }
    return TRUE;
}
