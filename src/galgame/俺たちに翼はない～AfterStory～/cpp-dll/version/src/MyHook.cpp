#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // 保存Detour原始函数指针
    static PVOID g_DetourOriginals[16]{};
    static int   g_DetourCount = 0;

    // ot_after.exe 基址与模块大小
    DWORD g_BaseAddr = 0;
    DWORD g_ModuleSize = 0;

    // OT_AFTER DRM 特征码 — Alpha Activation 弹出前的校验跳转
    BYTE g_DRMSig[] = {
        0x03, 0x40, 0x3C, 0x8B, 0x40, 0x28, 0x85, 0xC0,
        0x0F, 0x84, 0x13, 0x00, 0x00, 0x00, 0x8B, 0x5D,
        0x08, 0x8B, 0x5B, 0x14, 0x03, 0xC3, 0x6A, 0x00,
        0xFF, 0x75, 0x0C, 0x53, 0xFF, 0xD0, 0x89, 0x45,
        0xFC, 0x61, 0x8B, 0x45, 0xFC, 0xC9, 0xC2, 0x08,
        0x00
    };

    // 补丁数据: 0F 84 13 00 00 00 (JE) → EB 17 90 90 90 90 (JMP + NOP)
    BYTE g_PatchData[] = { 0xEB, 0x17, 0x90, 0x90, 0x90, 0x90 };

    bool  g_Patched = false;

    // 保存原始 VirtualAlloc 函数指针
    static decltype(&::VirtualAlloc) g_OriginalVirtualAlloc = ::VirtualAlloc;


    // 校验特征码
    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
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
    void WritePatch(DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        DWORD oldProtect = 0;
        if (!::VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect))
            return;
        ::memcpy(addr, data, len);
        ::VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

    // 应用 Hook
    void ApplyHook(HookEntry& hk)
    {
        if (hk.done || g_DetourCount >= 16)
            return;

        if (hk.sig != nullptr && !VerifySignature(hk.sigOff, hk.sig, hk.sigLen))
            return;

        // Detour 断点模式
        if (hk.patch_mod == 1)
        {
            for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
            {
                if (hk.patchOff[i] == 0 || hk.patchData[i] == nullptr)
                    continue;

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

        // 直接写内存模式
        for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
        {
            if (hk.patchOrig[i])
            {
                if (!VerifySignature(hk.patchOff[i], hk.patchOrig[i], hk.patchLen[i]))
                    return;
            }
            WritePatch(hk.patchOff[i], hk.patchData[i], hk.patchLen[i]);
        }
        hk.done = true;
    }

    // 在 ot_after.exe 模块内存中搜索特征码，返回 RVA（0 = 未找到）
    DWORD SearchSignature(BYTE* sig, DWORD len)
    {
        BYTE* base = (BYTE*)g_BaseAddr;
        BYTE* end  = base + g_ModuleSize;
        BYTE* addr = base;

        MEMORY_BASIC_INFORMATION mbi{};

        while (addr < end)
        {
            if (!::VirtualQuery(addr, &mbi, sizeof(mbi)))
                break;

            if (mbi.State == MEM_COMMIT &&
                !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            {
                BYTE* regionStart = (BYTE*)mbi.BaseAddress;
                DWORD regionSize  = mbi.RegionSize;

                for (DWORD i = 0; i <= regionSize - len; i++)
                {
                    if (::memcmp(regionStart + i, sig, len) == 0)
                        return (DWORD)(regionStart + i - base);
                }
            }

            addr = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
        }

        return 0;
    }

    // 拦截 VirtualAlloc
    static LPVOID WINAPI VirtualAllocHandler(
        LPVOID lpAddress,
        SIZE_T dwSize,
        DWORD  flAllocationType,
        DWORD  flProtect)
    {
        LPVOID result = g_OriginalVirtualAlloc(lpAddress, dwSize,
                                                flAllocationType, flProtect);

        // DRM 解压/解密后搜索特征码并打补丁
        if (!g_Patched && result)
        {
            DWORD foundRVA = SearchSignature(g_DRMSig, sizeof(g_DRMSig));
            if (foundRVA != 0)
            {
                // 特征码偏移 +8 处是 JE 指令，改为 JMP 跳过 Alpha Activation
                WritePatch(foundRVA + 8, g_PatchData, sizeof(g_PatchData));
                g_Patched = true;
            }
        }

        return result;
    }


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 ot_after.exe 基址
        HMODULE hModule = ::GetModuleHandleW(L"ot_after.exe");
        g_BaseAddr = (DWORD)hModule;

        // 从 PE 头获取模块大小，用于内存搜索范围
        PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)hModule;
        PIMAGE_NT_HEADERS pNt  = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDos->e_lfanew);
        g_ModuleSize = pNt->OptionalHeader.SizeOfImage;

        // 安装 VirtualAlloc Hook
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
    }

}
