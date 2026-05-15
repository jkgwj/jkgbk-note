#include "hijack.h"
#include "MyHook.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(hModule);
        real_dll_init();                                       // 加载原 version.dll 并转发
        ::CreateThread(NULL, 0, MyHook::InitHook, NULL, 0, NULL); // 拦截 LoadLibraryA 等待 arc.m 加载
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
