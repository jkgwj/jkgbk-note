#include "MyHook.h"
#include <detours/detours.h>

// 注释掉=模式A(硬编码) 取消注释=模式B(通用查找)
#define USE_MODE_B

namespace MyHook
{
    //保存Detour原始函数指针（自动管理）
    static PVOID g_DetourOriginals[16]{};
    static int   g_DetourCount = 0;

    // cs2.exe 模块基址
    DWORD g_BaseAddr = 0;

    // AlphaRom 偏移 (Sarcheck 内特征码偏移)
    DWORD g_AlphaRomOffset = 0x00B1D8CE;
    LPVOID g_TargetAddr = nullptr;

    // AlphaRom 特征码
    BYTE g_Signature[] = {
       0x51, 0xB9, 0x06, 0x00, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x28, 0x01, 0xC8,
       0xD3, 0xE0, 0x05, 0x45, 0x45, 0x53, 0x04, 0x50, 0xE8, 0x00, 0x00, 0x00,
       0x00, 0x83, 0x04, 0x24, 0x16, 0x8B, 0x04, 0x24, 0x83, 0xC0, 0x09, 0xC7,
       0x80, 0xCA, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE0, 0x49,
       0xEB, 0xD4, 0x59, 0xE9, 0xCE, 0x00, 0x00, 0x00, 0x55, 0x89, 0xE5
    };

    DWORD g_SigLen = sizeof(g_Signature);
    DWORD g_PatchValue = 0x90000CC2;
    bool  g_AlphaRomPatched = false;

    // 目标地址：0x4036BB  原指令完整特征码：75 5C 8B 15 A0 41 7C 00
    BYTE g_JumpCheckSig[] = { 0x75, 0x5C, 0x8B, 0x15, 0xA0, 0x41, 0x7C, 0x00 };
    // 补丁指令：jne -> jmp  2字节
    BYTE g_JumpCheckPatch[] = { 0xEB, 0x5C };

    // HookEntry 严格对标参考示例，3元素数组全部补满
    HookEntry g_JumpCheckHook = {
        0x36BB,                   
        g_JumpCheckSig,           
        sizeof(g_JumpCheckSig),   
        { 0x36BB },               
        { nullptr },              
        { g_JumpCheckPatch },     
        { 2 },                    
        false,                    
        0                         
    };

    // 校验特征码 (与参考模板完全一致)
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

    // 写入内存补丁 (与参考模板完全一致)
    void WritePatch(DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        DWORD oldProtect = 0;
        if (!::VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect))
            return;
        ::memcpy(addr, data, len);
        ::VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

    // 通用应用Hook函数 (与参考模板完全一致)
    void ApplyHook(HookEntry& hk)
    {
        if (hk.done || g_DetourCount >= 16)
            return;

        if (hk.sig != nullptr && !VerifySignature(hk.sigOff, hk.sig, hk.sigLen))
            return;

        // Detour跳转模式
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

        // 直接写内存补丁模式
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

    // 保存原始 VirtualAlloc 指针
    static decltype(&::VirtualAlloc) g_OriginalVirtualAlloc = ::VirtualAlloc;

#ifdef USE_MODE_B
    // Sarcheck 结构 (与参考模板完全一致)
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
            if (::memcmp(base + i, g_Signature, g_SigLen) != 0)
                continue;

            DWORD oldProtect = 0;
            if (!::VirtualProtect(base + i, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
                return;

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

    // VirtualAlloc 拦截函数 (核心调用逻辑 1:1 复刻参考模板)
    static LPVOID WINAPI VirtualAllocHandler(
        LPVOID lpAddress,
        SIZE_T dwSize,
        DWORD  flAllocationType,
        DWORD  flProtect)
    {
        LPVOID result = g_OriginalVirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);

#ifdef USE_MODE_B
        // 分支1：Sarcheck 解密 → 执行 AlphaRom 补丁 (原有逻辑不变)
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
        // 模式A硬编码地址
        if (!g_AlphaRomPatched && result)
        {
            PatchAlphaRom();
        }
#endif

        // 分支2：通用HOOK入口 (完全仿照参考模板！所有普通补丁放这里)
        // 内存解密完成后反复触发，框架内部 done 保证只执行一次
        if (result)
        {
            ApplyHook(g_JumpCheckHook); // 你的第二个Hook 在此调用
            // 后续新增更多Hook 也依次往下加即可
        }

        return result;
    }

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 cs2.exe 模块基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"cs2.exe");

#ifdef USE_MODE_B
#else
        g_TargetAddr = (LPVOID)g_AlphaRomOffset;
#endif

        // 挂载 VirtualAlloc Detour
        ::DetourRestoreAfterWith();
        ::DetourTransactionBegin();
        ::DetourUpdateThread(GetCurrentThread());
        ::DetourAttach(&(PVOID&)g_OriginalVirtualAlloc, VirtualAllocHandler);
        ::DetourTransactionCommit();
        return 0;
    }

    void __stdcall UninstallAll()
    {
        // 卸载 VirtualAlloc 劫持 (与参考模板一致)
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        ::DetourDetach(&(PVOID&)g_OriginalVirtualAlloc, VirtualAllocHandler);
        ::DetourTransactionCommit();


        WritePatch(0x36BB, g_JumpCheckSig, 2);
        g_JumpCheckHook.done = false;


        g_AlphaRomPatched = false;
        g_FoundSarcheck = false;
        g_DetourCount = 0;
    }
}