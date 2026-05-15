#include "MyHook.h"
#include <detours/detours.h>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

namespace MyHook
{
    // 原始 LoadLibraryA
    static decltype(&::LoadLibraryA) g_OriginalLoadLibraryA = ::LoadLibraryA;

    // HOOK1 特征码 (52字节) — 对应 arc.m
    BYTE g_Sig1[52] = {
        0x0F,0x84,0x3F,0x01,0x00,0x00,0x8D,0x8C,0x24,0xBC,0x00,0x00,0x00,0x55,0x51,0xE8,
        0x15,0x3F,0x00,0x00,0x83,0xC4,0x08,0x50,0x8D,0x8C,0x24,0x88,0x00,0x00,0x00,0xC6,
        0x84,0x24,0xE8,0x07,0x00,0x00,0x05,0xE8,0xFD,0x01,0x00,0x00,0x8B,0xD8,0xF7,0xDB,
        0x6A,0x00,0x1A,0xDB
    };

    // HOOK2 特征码 (54字节) — 对应 arc.m
    BYTE g_Sig2[6] = {
        0x0F,0x84,0xF1,0x00,0x00,0x00
    };

    // nop 补丁 — je 改 nop
    BYTE g_Nop6[6] = { 0x90,0x90,0x90,0x90,0x90,0x90 };
    BYTE g_Nop6_[6] = { 0x90,0x90,0x90,0x90,0x90,0x90 };

    // 两条 hook 条目
    HookEntry g_Hook1 = {
        0x2A37,     
        g_Sig1,
        52,
        g_Nop6,
        6,
        false
    };

    HookEntry g_Hook2 = {
        0x2A85,     
        g_Sig2,
        6,
        g_Nop6_,
        6,
        false
    };

    // 校验特征码
    static bool VerifySignature(HMODULE hMod, DWORD offset, BYTE* sig, DWORD len)
    {
        LPVOID addr = (LPBYTE)hMod + offset;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(addr, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        return (::memcmp(addr, sig, len) == 0);
    }

    // 写入补丁
    static void WritePatch(HMODULE hMod, DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPBYTE)hMod + offset;
        DWORD oldProtect = 0;
        if (!::VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect))
            return;
        ::memcpy(addr, data, len);
        ::VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

    // 应用单条 hook
    static void ApplyHook(HMODULE hMod, HookEntry& hk)
    {
        if (hk.done) return;
        if (!VerifySignature(hMod, hk.sigOff, hk.sig, hk.sigLen))
            return;
        WritePatch(hMod, hk.sigOff, hk.patchData, hk.patchLen);
        hk.done = true;
    }

    // arc.m 加载后尝试打补丁
    static void TryApplyHooks()
    {
        HMODULE hArc = ::GetModuleHandleW(L"arc.m");
        if (!hArc) return;

        ApplyHook(hArc, g_Hook1);
        ApplyHook(hArc, g_Hook2);
    }

    // 拦截 LoadLibraryA
    static HMODULE WINAPI LoadLibraryAHandler(LPCSTR lpLibFileName)
    {
        HMODULE result = g_OriginalLoadLibraryA(lpLibFileName);
        if (result && ::StrStrIA(lpLibFileName, "arc.m"))
            TryApplyHooks();
        return result;
    }

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 安装 LoadLibraryA 拦截
        ::DetourRestoreAfterWith();
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        ::DetourAttach(&(PVOID&)g_OriginalLoadLibraryA, LoadLibraryAHandler);
        ::DetourTransactionCommit();

        // arc.m 可能已提前加载
        TryApplyHooks();

        return 0;
    }

    void __stdcall UninstallAll()
    {
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        ::DetourDetach(&(PVOID&)g_OriginalLoadLibraryA, LoadLibraryAHandler);
        ::DetourTransactionCommit();
    }
}
