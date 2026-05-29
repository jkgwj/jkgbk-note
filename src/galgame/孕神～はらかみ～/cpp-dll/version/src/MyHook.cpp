#include "MyHook.h"
#include <detours/detours.h>

// 注释掉=模式A(硬编码) 取消注释=模式B(通用查找)
#define USE_MODE_B

namespace MyHook
{
    static PVOID g_DetourOriginals[16]{};
    static int   g_DetourCount = 0;

    DWORD g_BaseAddr = 0;
    DWORD g_AlphaRomOffset = 0x0241DC01;
    LPVOID g_TargetAddr = nullptr;

    BYTE g_Signature[] = {
        0x51, 0xB9, 0x06, 0x00, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x28, 0x01, 0xC8, 0xD3, 0xE0, 0x05, 0x45,
        0x45, 0x53, 0x04, 0x50, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0x04, 0x24, 0x16, 0x8B, 0x04, 0x24,
        0x83, 0xC0, 0x09, 0xC7, 0x80, 0xCA, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0, 0x49,
        0xEB, 0xD4, 0x59, 0xE9, 0xCE, 0x00, 0x00, 0x00
    };

    DWORD g_SigLen = sizeof(g_Signature);
    DWORD g_PatchValue = 0x90000CC2;
    bool  g_AlphaRomPatched = false;

    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(addr, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        return (::memcmp(addr, sig, len) == 0);
    }

    void WritePatch(DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        DWORD oldProtect = 0;
        if (!::VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
        ::memcpy(addr, data, len);
        ::VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

    void ApplyHook(HookEntry& hk)
    {
        if (hk.done || g_DetourCount >= 16) return;
        if (hk.sig != nullptr && !VerifySignature(hk.sigOff, hk.sig, hk.sigLen)) return;

        if (hk.patch_mod == 1)
        {
            for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
            {
                if (hk.patchOff[i] == 0 || hk.patchData[i] == nullptr) continue;

                LPVOID hookFunc = ::VirtualAlloc(nullptr, hk.patchLen[i], MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (!hookFunc) continue;

                memcpy(hookFunc, hk.patchData[i], hk.patchLen[i]);

                PVOID original = (PVOID)(g_BaseAddr + hk.patchOff[i]);
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());
                DetourAttach(&(PVOID&)original, hookFunc);
                DetourTransactionCommit();

                g_DetourOriginals[g_DetourCount++] = original;
            }

            hk.done = true;
            return;
        }

        for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
        {
            if (hk.patchOrig[i] && !VerifySignature(hk.patchOff[i], hk.patchOrig[i], hk.patchLen[i]))
                return;
            WritePatch(hk.patchOff[i], hk.patchData[i], hk.patchLen[i]);
        }
        hk.done = true;
    }

    // 区域检测三合一HOOK
    // 偏移: 0x31C5A / 0x31CB7 / 0x31D36
    BYTE g_RegionSig[39] = {
        0x75,0x53,0xBA,0xC0,0x01,0x60,0x00,0x8D,0x44,0x24,0x08,0xE8,0x56,0x2C,0x08,0x00,
        0x8B,0x0D,0x28,0x7E,0x07,0x01,0x53,0x68,0x50,0x02,0x60,0x00,0x8D,0x44,0x24,0x10,
        0x50,0x51,0xFF,0xD5,0x83,0xF8,0x01
    };

    BYTE g_RegionPatch1[2] = { 0xEB,0x53 };
    BYTE g_RegionPatch2[2] = { 0xEB,0x76 };
    BYTE g_RegionPatch3[2] = { 0xEB,0x66 };

    HookEntry g_RegionHook = {
        0x31C5A,
        g_RegionSig, 39,
        { 0x31C5A, 0x31CB7, 0x31D36 },
        { nullptr, nullptr, nullptr },
        { g_RegionPatch1, g_RegionPatch2, g_RegionPatch3 },
        { 2, 2, 2 },
        false, 0
    };

    static decltype(&::VirtualAlloc) g_OriginalVirtualAlloc = ::VirtualAlloc;

#ifdef USE_MODE_B
    struct SarcheckInfo
    {
        LPCSTR cpDllName;
        PDWORD pDllData;
        DWORD  nDllSize;
    };

    static LPVOID g_AddressTable[100]{};
    static bool   g_FoundSarcheck = false;

    static void PushAddress(LPVOID lpAddress)
    {
        for (auto& addr : g_AddressTable)
        {
            if ((addr == nullptr) || (addr == lpAddress))
            {
                addr = lpAddress;
                return;
            }
        }
    }

    static SarcheckInfo* QuerySarcheck()
    {
        MEMORY_BASIC_INFORMATION mbi{};

        for (auto& addr : g_AddressTable)
        {
            if (addr == nullptr) return nullptr;

            if (!::VirtualQuery(addr, &mbi, sizeof(mbi))) continue;
            if (!(mbi.Type == MEM_PRIVATE &&
                mbi.State == MEM_COMMIT &&
                mbi.AllocationProtect == PAGE_READWRITE)) continue;

            SarcheckInfo* info = (SarcheckInfo*)addr;

            if (info->cpDllName == nullptr) continue;
            if (!::VirtualQuery(info->cpDllName, &mbi, sizeof(mbi))) continue;
            if (!(mbi.Type == MEM_IMAGE &&
                mbi.State == MEM_COMMIT &&
                mbi.AllocationProtect == PAGE_EXECUTE_WRITECOPY)) continue;

            if (::strncmp("sarcheck.dll", info->cpDllName, 13)) continue;

            if (!info->pDllData) continue;
            if (((PIMAGE_DOS_HEADER)info->pDllData)->e_magic != IMAGE_DOS_SIGNATURE) continue;

            return info;
        }

        return nullptr;
    }

    void __stdcall PatchAlphaRom()
    {
        SarcheckInfo* info = QuerySarcheck();
        if (!info) return;

        PBYTE base = (PBYTE)info->pDllData;
        DWORD size = info->nDllSize;

        for (DWORD i = 0; i <= size - g_SigLen; i++)
        {
            if (::memcmp(base + i, g_Signature, g_SigLen) != 0) continue;

            DWORD oldProtect = 0;
            if (!::VirtualProtect(base + i, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) return;

            *(DWORD*)(base + i) = g_PatchValue;

            ::VirtualProtect(base + i, 4, oldProtect, &oldProtect);

            g_AlphaRomPatched = true;
            return;
        }
    }

#else

    void __stdcall PatchAlphaRom()
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(g_TargetAddr, &mbi, sizeof(mbi))) return;
        if (mbi.State != MEM_COMMIT) return;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return;

        if (::memcmp(g_TargetAddr, g_Signature, g_SigLen) != 0) return;

        DWORD oldProtect = 0;
        if (!::VirtualProtect(g_TargetAddr, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) return;

        *(DWORD*)g_TargetAddr = g_PatchValue;

        ::VirtualProtect(g_TargetAddr, 4, oldProtect, &oldProtect);

        g_AlphaRomPatched = true;
    }
#endif

    static LPVOID WINAPI VirtualAllocHandler(
        LPVOID lpAddress,
        SIZE_T dwSize,
        DWORD  flAllocationType,
        DWORD  flProtect)
    {
        LPVOID result = g_OriginalVirtualAlloc(lpAddress, dwSize,
            flAllocationType, flProtect);

#ifdef USE_MODE_B
        if (!g_FoundSarcheck && result
            && (dwSize > 8)
            && (flAllocationType == MEM_COMMIT)
            && (flProtect == PAGE_READWRITE))
        {
            PushAddress(result);

            SarcheckInfo* info = QuerySarcheck();
            if (info)
            {
                PatchAlphaRom();
                g_FoundSarcheck = true;
            }
        }
#else
        if (!g_AlphaRomPatched && result)
        {
            PatchAlphaRom();
        }
#endif

        if (result)
        {
            ApplyHook(g_RegionHook);
        }

        return result;
    }

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"reallive.exe");

#ifdef USE_MODE_B
#else
        g_TargetAddr = (LPVOID)g_AlphaRomOffset;
#endif

        ::DetourRestoreAfterWith();
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        ::DetourAttach(&(PVOID&)g_OriginalVirtualAlloc, VirtualAllocHandler);
        ::DetourTransactionCommit();
        return 0;
    }

    void __stdcall UninstallAll()
    {
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        ::DetourDetach(&(PVOID&)g_OriginalVirtualAlloc, VirtualAllocHandler);
        ::DetourTransactionCommit();

        g_RegionHook.done = false;
        g_AlphaRomPatched = false;
        g_FoundSarcheck = false;
        g_DetourCount = 0;
    }
}