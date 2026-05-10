#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

#pragma comment(linker, "/EXPORT:GetFileVersionInfoA=_AheadLib_GetFileVersionInfoA,@1")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoByHandle=_AheadLib_GetFileVersionInfoByHandle,@2")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExA=_AheadLib_GetFileVersionInfoExA,@3")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExW=_AheadLib_GetFileVersionInfoExW,@4")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeA=_AheadLib_GetFileVersionInfoSizeA,@5")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExA=_AheadLib_GetFileVersionInfoSizeExA,@6")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExW=_AheadLib_GetFileVersionInfoSizeExW,@7")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeW=_AheadLib_GetFileVersionInfoSizeW,@8")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoW=_AheadLib_GetFileVersionInfoW,@9")
#pragma comment(linker, "/EXPORT:VerFindFileA=_AheadLib_VerFindFileA,@10")
#pragma comment(linker, "/EXPORT:VerFindFileW=_AheadLib_VerFindFileW,@11")
#pragma comment(linker, "/EXPORT:VerInstallFileA=_AheadLib_VerInstallFileA,@12")
#pragma comment(linker, "/EXPORT:VerInstallFileW=_AheadLib_VerInstallFileW,@13")
#pragma comment(linker, "/EXPORT:VerLanguageNameA=_AheadLib_VerLanguageNameA,@14")
#pragma comment(linker, "/EXPORT:VerLanguageNameW=_AheadLib_VerLanguageNameW,@15")
#pragma comment(linker, "/EXPORT:VerQueryValueA=_AheadLib_VerQueryValueA,@16")
#pragma comment(linker, "/EXPORT:VerQueryValueW=_AheadLib_VerQueryValueW,@17")


static PVOID pfnAheadLib_GetFileVersionInfoA;
static PVOID pfnAheadLib_GetFileVersionInfoByHandle;
static PVOID pfnAheadLib_GetFileVersionInfoExA;
static PVOID pfnAheadLib_GetFileVersionInfoExW;
static PVOID pfnAheadLib_GetFileVersionInfoSizeA;
static PVOID pfnAheadLib_GetFileVersionInfoSizeExA;
static PVOID pfnAheadLib_GetFileVersionInfoSizeExW;
static PVOID pfnAheadLib_GetFileVersionInfoSizeW;
static PVOID pfnAheadLib_GetFileVersionInfoW;
static PVOID pfnAheadLib_VerFindFileA;
static PVOID pfnAheadLib_VerFindFileW;
static PVOID pfnAheadLib_VerInstallFileA;
static PVOID pfnAheadLib_VerInstallFileW;
static PVOID pfnAheadLib_VerLanguageNameA;
static PVOID pfnAheadLib_VerLanguageNameW;
static PVOID pfnAheadLib_VerQueryValueA;
static PVOID pfnAheadLib_VerQueryValueW;

//version.dll 模块句柄
static HMODULE g_hRealVersion = NULL;

static FARPROC WINAPI GetRealProc(PCSTR pszProcName)
{
    FARPROC fp = ::GetProcAddress(g_hRealVersion, pszProcName);
    if (!fp)
    {
        ::MessageBoxW(NULL, L"GetProcAddress Failed!", L"Hijack Dll", MB_OK);
        ::ExitProcess(0);
    }
    return fp;
}

static void __stdcall real_dll_init()
{
    WCHAR path[MAX_PATH];
    ::GetSystemDirectoryW(path, MAX_PATH);
    ::lstrcatW(path, L"\\version.dll");

    g_hRealVersion = ::LoadLibraryW(path);
    if (!g_hRealVersion)
    {
        ::MessageBoxW(NULL, L"Load Org Dll Failed!", L"Hijack Dll", MB_OK);
        ::ExitProcess(0);
    }

    pfnAheadLib_GetFileVersionInfoA        = GetRealProc("GetFileVersionInfoA");
    pfnAheadLib_GetFileVersionInfoByHandle = GetRealProc("GetFileVersionInfoByHandle");
    pfnAheadLib_GetFileVersionInfoExA      = GetRealProc("GetFileVersionInfoExA");
    pfnAheadLib_GetFileVersionInfoExW      = GetRealProc("GetFileVersionInfoExW");
    pfnAheadLib_GetFileVersionInfoSizeA    = GetRealProc("GetFileVersionInfoSizeA");
    pfnAheadLib_GetFileVersionInfoSizeExA  = GetRealProc("GetFileVersionInfoSizeExA");
    pfnAheadLib_GetFileVersionInfoSizeExW  = GetRealProc("GetFileVersionInfoSizeExW");
    pfnAheadLib_GetFileVersionInfoSizeW    = GetRealProc("GetFileVersionInfoSizeW");
    pfnAheadLib_GetFileVersionInfoW        = GetRealProc("GetFileVersionInfoW");
    pfnAheadLib_VerFindFileA              = GetRealProc("VerFindFileA");
    pfnAheadLib_VerFindFileW              = GetRealProc("VerFindFileW");
    pfnAheadLib_VerInstallFileA           = GetRealProc("VerInstallFileA");
    pfnAheadLib_VerInstallFileW           = GetRealProc("VerInstallFileW");
    pfnAheadLib_VerLanguageNameA          = GetRealProc("VerLanguageNameA");
    pfnAheadLib_VerLanguageNameW          = GetRealProc("VerLanguageNameW");
    pfnAheadLib_VerQueryValueA            = GetRealProc("VerQueryValueA");
    pfnAheadLib_VerQueryValueW            = GetRealProc("VerQueryValueW");
}


static void __stdcall real_dll_free()
{
    if (g_hRealVersion)
    {
        ::FreeLibrary(g_hRealVersion);
        g_hRealVersion = NULL;
    }
}


EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoA(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoByHandle(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoByHandle;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoExA(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoExA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoExW(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoExW;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoSizeA(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoSizeA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoSizeExA(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoSizeExA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoSizeExW(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoSizeExW;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoSizeW(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoSizeW;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_GetFileVersionInfoW(void)
{
    __asm jmp pfnAheadLib_GetFileVersionInfoW;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerFindFileA(void)
{
    __asm jmp pfnAheadLib_VerFindFileA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerFindFileW(void)
{
    __asm jmp pfnAheadLib_VerFindFileW;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerInstallFileA(void)
{
    __asm jmp pfnAheadLib_VerInstallFileA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerInstallFileW(void)
{
    __asm jmp pfnAheadLib_VerInstallFileW;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerLanguageNameA(void)
{
    __asm jmp pfnAheadLib_VerLanguageNameA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerLanguageNameW(void)
{
    __asm jmp pfnAheadLib_VerLanguageNameW;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerQueryValueA(void)
{
    __asm jmp pfnAheadLib_VerQueryValueA;
}

EXTERN_C __declspec(naked) void __cdecl AheadLib_VerQueryValueW(void)
{
    __asm jmp pfnAheadLib_VerQueryValueW;
}
